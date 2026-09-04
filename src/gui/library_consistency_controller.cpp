#include "library_consistency_controller.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>

#include "domain/junk_cue.hpp"
#include "domain/track_scope.hpp"
#include "gui/library_catalog_cache.hpp"
#include "gui/local_file_url.hpp"
#include "gui/write_guard.hpp"
#include "infrastructure/backup/filesystem_backup_store.hpp"
#include "infrastructure/backup/stick_write_lock.hpp"
#include "infrastructure/bulk_write_strategy.hpp"
#include "infrastructure/durable_file_write.hpp"
#include "infrastructure/engine/libdjinterop_engine_cleanup_writer.hpp"
#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/logging/file_operation_log.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/rekordbox/pdb_lookup.hpp"
#include "infrastructure/rekordbox/rekordbox_cleanup_writer.hpp"
#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"
#include "infrastructure/scratch_dir_guard.hpp"

namespace seabass::gui
{

namespace fs = std::filesystem;
using domain::LibraryConsistencyIssue;
using infrastructure::ScratchDirGuard;

namespace
{
// Every domain::Track already carries which catalog it came from.
// Every reader in this codebase sets it. All tracks in one issue are
// guaranteed to be from the same catalog (LibraryConsistencyChecker is
// only ever called once per format), so the survivor (when present) or
// the first broken track both name it identically.
QString issueFormat(const LibraryConsistencyIssue &issue)
{
    if (issue.survivor) {
        return QString::fromStdString(issue.survivor->format);
    }
    if (!issue.brokenGroup.empty()) {
        return QString::fromStdString(issue.brokenGroup.front().format);
    }
    return {};
}
}  // namespace

LibraryConsistencyIssueListModel::LibraryConsistencyIssueListModel(QObject *parent) : QAbstractListModel(parent) {}

int LibraryConsistencyIssueListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_issues.size());
}

namespace
{

QVariantMap brokenTrackToMap(const domain::Track &track)
{
    QVariantMap m;
    // Same "side" key every other trackToMap()-style helper in this
    // codebase uses (sync_controller.cpp, duplicates_controller.cpp,
    // cleanup_controller.cpp) -- TrackWaveformCard reads track.side, not
    // track.format, for both its Play wiring and WaveformView's format
    // hint. Unused by this file's own existing callers (the missing-file
    // detail view passes format separately), added now for the new
    // TrackWaveformCard usage in the memory-cue section below.
    m["side"] = QString::fromStdString(track.format);
    m["sourceId"] = QString::fromStdString(track.sourceId);
    m["title"] = QString::fromStdString(track.title);
    m["artist"] = QString::fromStdString(track.artist);
    m["filePath"] = QString::fromStdString(track.filePath);
    m["artworkPath"] = toLocalFileUrl(track.artworkPath);
    m["durationMs"] = track.durationSeconds * 1000.0;
    QVariantList cues;
    for (const auto &c : track.cues) {
        QVariantMap cueMap;
        cueMap["kind"] = c.kind == domain::CuePoint::Kind::Hot ? QStringLiteral("hot") : QStringLiteral("memory");
        cueMap["hotCueNumber"] = c.hotCueNumber;
        cueMap["positionMs"] = c.positionMs;
        cueMap["color"] = QString::fromStdString(c.color);
        cues << cueMap;
    }
    m["cues"] = cues;
    return m;
}

}  // namespace

QVariant LibraryConsistencyIssueListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= m_issues.size()) {
        return {};
    }
    const auto &issue = m_issues[static_cast<size_t>(index.row())];
    switch (role) {
    case KindRole:
        switch (issue.kind) {
        case LibraryConsistencyIssue::Kind::Repairable:
            return QStringLiteral("repairable");
        case LibraryConsistencyIssue::Kind::Conflict:
            return QStringLiteral("conflict");
        case LibraryConsistencyIssue::Kind::Missing:
            return QStringLiteral("missing");
        }
        return {};
    case FormatRole:
        return issueFormat(issue);
    case SurvivorRole:
        return issue.survivor ? brokenTrackToMap(*issue.survivor) : QVariantMap();
    case BrokenTracksRole: {
        QVariantList list;
        for (const auto &t : issue.brokenGroup) {
            list << brokenTrackToMap(t);
        }
        return list;
    }
    case CueMergeNeededRole:
        return !issue.survivorCues.empty();
    default:
        return {};
    }
}

QHash<int, QByteArray> LibraryConsistencyIssueListModel::roleNames() const
{
    return {
        {KindRole, "kind"},
        {FormatRole, "format"},
        {SurvivorRole, "survivor"},
        {BrokenTracksRole, "brokenTracks"},
        {CueMergeNeededRole, "cueMergeNeeded"},
    };
}

void LibraryConsistencyIssueListModel::clear()
{
    beginResetModel();
    m_issues.clear();
    endResetModel();
}

void LibraryConsistencyIssueListModel::appendIssues(std::vector<domain::LibraryConsistencyIssue> issues)
{
    if (issues.empty()) {
        return;
    }
    int first = static_cast<int>(m_issues.size());
    int last = first + static_cast<int>(issues.size()) - 1;
    beginInsertRows(QModelIndex(), first, last);
    m_issues.insert(m_issues.end(), std::make_move_iterator(issues.begin()), std::make_move_iterator(issues.end()));
    endInsertRows();
}

void LibraryConsistencyIssueListModel::removeIssueAt(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= m_issues.size()) {
        return;
    }
    beginRemoveRows(QModelIndex(), index, index);
    m_issues.erase(m_issues.begin() + index);
    endRemoveRows();
}

JunkCueIssueListModel::JunkCueIssueListModel(QObject *parent) : QAbstractListModel(parent) {}

int JunkCueIssueListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_issues.size());
}

QVariant JunkCueIssueListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= m_issues.size()) {
        return {};
    }
    const auto &issue = m_issues[static_cast<size_t>(index.row())];
    switch (role) {
    case FormatRole:
        return QString::fromStdString(issue.track.format);
    case TitleRole:
        return QString::fromStdString(issue.track.title);
    case ArtistRole:
        return QString::fromStdString(issue.track.artist);
    case TrackRole:
        return brokenTrackToMap(issue.track);
    default:
        return {};
    }
}

QHash<int, QByteArray> JunkCueIssueListModel::roleNames() const
{
    return {
        {FormatRole, "format"},
        {TitleRole, "title"},
        {ArtistRole, "artist"},
        {TrackRole, "track"},
    };
}

void JunkCueIssueListModel::clear()
{
    beginResetModel();
    m_issues.clear();
    endResetModel();
}

void JunkCueIssueListModel::appendIssues(std::vector<domain::JunkCueIssue> issues)
{
    if (issues.empty()) {
        return;
    }
    int first = static_cast<int>(m_issues.size());
    int last = first + static_cast<int>(issues.size()) - 1;
    beginInsertRows(QModelIndex(), first, last);
    m_issues.insert(m_issues.end(), std::make_move_iterator(issues.begin()), std::make_move_iterator(issues.end()));
    endInsertRows();
}

void JunkCueIssueListModel::removeAt(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= m_issues.size()) {
        return;
    }
    beginRemoveRows(QModelIndex(), index, index);
    m_issues.erase(m_issues.begin() + index);
    endRemoveRows();
}

namespace
{

std::vector<domain::Track> scanTracks(const QString &format, const QString &path,
                                       std::shared_ptr<QtProgressReporter> reporter)
{
    return LibraryCatalogCache::instance().tracksFor(format.toStdString(), path.toStdString(), *reporter);
}

// This format's own playlist membership tally, unfiltered -- called on
// the full track list before any TrackScope filtering below, so picking
// a playlist in JunkCuePage.qml's picker never shrinks its own list of
// choices. Mirrors the tally half of SyncController's own
// collectPlaylistSummary(), just for one format at a time (see
// LibraryConsistencyController::mergePlaylistSummary() for how each
// format's contribution gets folded into the cross-catalog union).
void tallyPlaylists(const std::vector<domain::Track> &tracks, LibraryConsistencyScanResult &result)
{
    std::map<std::string, int> countByName;
    for (const auto &track : tracks) {
        for (const auto &playlist : track.playlists) {
            countByName[playlist.name]++;
        }
    }
    for (const auto &[name, count] : countByName) {
        QString qName = QString::fromStdString(name);
        result.playlistNames << qName;
        result.playlistTrackCounts[qName] = count;
    }
}

// Runs entirely on a background thread (see LibraryConsistencyController::
// scanNextPendingFormat()) - no access to the controller itself. Scans
// exactly one format; the controller chains one of these per present
// catalog to get the progressive, format-at-a-time behavior. playlistName
// empty scans/checks the whole format's library, same as before this
// parameter existed; a real name scopes both the junk-cue list and the
// consistency check to just that playlist's tracks, via domain::TrackScope
// -- same seam SyncController::runAnalyzeTask already uses.
LibraryConsistencyScanResult runScanTask(QString format, QString path, QString playlistName,
                                          std::shared_ptr<QtProgressReporter> reporter)
{
    LibraryConsistencyScanResult result;
    try {
        auto tracks = scanTracks(format, path, reporter);

        tallyPlaylists(tracks, result);

        if (!playlistName.isEmpty()) {
            tracks = domain::filterByScope(tracks, domain::TrackScope::playlist(playlistName.toStdString()));
        }

        // Junk-cue detection doesn't care about file existence at all,
        // computed on the full (post-scope) track list before the
        // healthy/broken split below moves tracks out of it. Streaming
        // tracks are excluded here too, same "never touch these" policy
        // as every other consistency action in this class (see Track::
        // streamingSource's own doc comment).
        for (auto &issue : domain::JunkCueFinder::find(tracks)) {
            if (issue.track.streamingSource.empty()) {
                result.junkCues.push_back(std::move(issue));
            }
        }

        std::vector<domain::Track> healthy;
        std::vector<domain::Track> broken;
        for (auto &t : tracks) {
            // Streaming tracks (Engine/TIDAL) have no real local file by
            // design, neither healthy nor broken, just not a local-
            // file consistency concern at all. See
            // domain::Track::streamingSource's own doc comment.
            if (!t.streamingSource.empty()) {
                continue;
            }
            std::error_code ec;
            bool exists = !t.filePath.empty() && fs::exists(t.filePath, ec);
            (exists ? healthy : broken).push_back(std::move(t));
        }
        result.issues = domain::LibraryConsistencyChecker::check(healthy, broken);
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

// Runs entirely on a background thread (see LibraryConsistencyController::
// repairNextPendingFormat()), repairs every given issue, all from the
// same single format/path.
LibraryConsistencyWriteResult runRepairTask(QString format, QString path,
                                             std::vector<LibraryConsistencyIssue> issues)
{
    LibraryConsistencyWriteResult result;
    QString refusal = refuseIfRekordboxRunning();
    if (!refusal.isEmpty()) {
        result.errorMessage = refusal;
        return result;
    }
    try {
        fs::path stickRoot = fs::path(path.toStdString()).parent_path();
        infrastructure::backup::StickWriteLock lock((stickRoot / ".seabass-backups" / ".write.lock").string());
        infrastructure::backup::FilesystemBackupStore backupStore((stickRoot / ".seabass-backups").string());
        infrastructure::logging::FileOperationLog log((stickRoot / ".seabass.log").string());

        int cuesMerged = 0;
        int rowsRemoved = 0;

        if (format == "rekordbox") {
            std::string pioneerRoot = path.toStdString();
            infrastructure::rekordbox::RekordboxCueWriter cueWriter(pioneerRoot);
            bool hasOneLib = infrastructure::onelibrary::OneLibraryCueWriter::existsFor(pioneerRoot);

            std::string pdbPath = pioneerRoot + "/rekordbox/export.pdb";
            auto record = backupStore.backup({pdbPath}, "consistency-repair");
            log.record("consistency: backed up export.pdb -> " + record.path);
            std::set<std::string> backedUpAnlz;

            // Only row removal below touches export.pdb (a single shared
            // file) -- cue merges go through RekordboxCueWriter into
            // small per-track .ANLZ files instead, already cheap enough
            // not to need staging (see rekordbox_cue_writer.cpp's own
            // comment on the same tradeoff for the PCOB gap). So the
            // item count driving the staging decision is rows-to-remove
            // only, same shape as sync_controller.cpp's engine/
            // onelibrary branches.
            int rowsToRemove = 0;
            for (const auto &issue : issues) {
                if (issue.survivor) {
                    rowsToRemove += static_cast<int>(issue.brokenGroup.size());
                }
            }
            std::error_code sizeEc;
            auto existingBytes = fs::file_size(pdbPath, sizeEc);
            infrastructure::BulkWriteStrategyInputs strategyInputs;
            strategyInputs.itemCount = rowsToRemove;
            strategyInputs.existingFileBytes = sizeEc ? 0 : existingBytes;
            bool useWholeFile = !sizeEc && infrastructure::shouldUseWholeFileReplace(strategyInputs) &&
                                 infrastructure::hasRoomForWholeFileReplace(fs::path(pdbPath).parent_path(), existingBytes);

            std::optional<fs::path> scratchDir;
            std::optional<ScratchDirGuard> scratchGuard;
            std::string cleanupRoot = pioneerRoot;
            if (useWholeFile) {
                scratchDir = fs::temp_directory_path() /
                             ("seabass-repair-scratch-rekordbox-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
                std::error_code cleanupEc;
                fs::remove_all(*scratchDir, cleanupEc);
                fs::create_directories(*scratchDir / "rekordbox");
                fs::copy_file(pdbPath, *scratchDir / "rekordbox" / "export.pdb");
                scratchGuard.emplace(*scratchDir);
                cleanupRoot = scratchDir->string();
                log.record("consistency: removing " + std::to_string(rowsToRemove) +
                           " row(s) against a local scratch copy first (export.pdb is " +
                           std::to_string(existingBytes) + " bytes)");
            }
            infrastructure::rekordbox::RekordboxCleanupWriter cleanupWriter(cleanupRoot);

            for (const auto &issue : issues) {
                if (!issue.survivor) {
                    continue;
                }
                const auto &survivor = *issue.survivor;
                if (!issue.survivorCues.empty()) {
                    auto analyzePath = infrastructure::rekordbox::findAnlzPathForTrackId(
                        pioneerRoot, static_cast<uint32_t>(std::stoul(survivor.sourceId)));
                    if (analyzePath) {
                        std::string extPath = infrastructure::rekordbox::extAnlzPath(pioneerRoot, *analyzePath);
                        if (backedUpAnlz.insert(extPath).second) {
                            auto anlzRecord = backupStore.backup({extPath}, "consistency-repair");
                            log.record("consistency: backed up -> " + anlzRecord.path);
                        }
                    }
                    cueWriter.writeHotCues(survivor.sourceId, issue.survivorCues);
                    cuesMerged += static_cast<int>(issue.survivorCues.size());
                    log.record("consistency: merged cues onto survivor id=" + survivor.sourceId);

                    // Best-effort mirror, same convention as Clean Up's
                    // own survivor-cue mirror block.
                    if (hasOneLib && !survivor.filePath.empty()) {
                        try {
                            infrastructure::onelibrary::OneLibraryCueWriter oneLibWriter(pioneerRoot);
                            oneLibWriter.writeCuesForPath(survivor.filePath, issue.survivorCues);
                        } catch (const std::exception &e) {
                            log.record(std::string("consistency: OneLibrary cue mirror failed: ") + e.what());
                        }
                    }
                }
                for (const auto &broken : issue.brokenGroup) {
                    cleanupWriter.removeTrackReplacingWith(broken.sourceId, survivor.sourceId);
                    log.record("consistency: removed broken row id=" + broken.sourceId + " (\"" + broken.title +
                                "\"), replaced by survivor id=" + survivor.sourceId);
                    rowsRemoved++;

                    if (hasOneLib && !broken.filePath.empty() && !survivor.filePath.empty()) {
                        try {
                            infrastructure::onelibrary::OneLibraryCueWriter oneLibWriter(pioneerRoot);
                            // Reassigns playlist membership onto the
                            // survivor instead of dropping it -- see
                            // OneLibraryCueWriter::
                            // removeTrackByPathReplacingWith()'s own
                            // comment.
                            oneLibWriter.removeTrackByPathReplacingWith(broken.filePath, survivor.filePath);
                        } catch (const std::exception &e) {
                            log.record(std::string("consistency: OneLibrary row removal failed: ") + e.what());
                        }
                    }
                }
            }

            if (scratchDir && !infrastructure::copyFileDurablyAtomic(
                                   (*scratchDir / "rekordbox" / "export.pdb").string(), pdbPath)) {
                throw std::runtime_error(
                    "consistency: failed to commit the scratch-repaired export.pdb back onto the stick");
            }
        } else if (format == "engine") {
            std::string engineLibraryPath = path.toStdString();
            std::string engineDbFile = (fs::path(engineLibraryPath) / "Database2" / "m.db").string();
            auto record = backupStore.backup({engineDbFile}, "consistency-repair");
            log.record("consistency: backed up m.db -> " + record.path);

            // Both cue merges and row removal hit the same shared m.db
            // here (unlike rekordbox above, where cue merges go to
            // small per-track files instead) -- so every write in this
            // branch counts toward the staging decision.
            int engineWriteCount = 0;
            for (const auto &issue : issues) {
                if (!issue.survivor) {
                    continue;
                }
                if (!issue.survivorCues.empty()) {
                    engineWriteCount++;
                }
                engineWriteCount += static_cast<int>(issue.brokenGroup.size());
            }
            std::error_code sizeEc;
            auto existingBytes = fs::file_size(engineDbFile, sizeEc);
            infrastructure::BulkWriteStrategyInputs strategyInputs;
            strategyInputs.itemCount = engineWriteCount;
            strategyInputs.existingFileBytes = sizeEc ? 0 : existingBytes;
            bool useWholeFile =
                !sizeEc && infrastructure::shouldUseWholeFileReplace(strategyInputs) &&
                infrastructure::hasRoomForWholeFileReplace(fs::path(engineDbFile).parent_path(), existingBytes);

            std::optional<fs::path> scratchDir;
            std::optional<ScratchDirGuard> scratchGuard;
            std::string writeTargetPath = engineLibraryPath;
            if (useWholeFile) {
                scratchDir = fs::temp_directory_path() /
                             ("seabass-repair-scratch-engine-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
                std::error_code cleanupEc;
                fs::remove_all(*scratchDir, cleanupEc);
                fs::create_directories(*scratchDir / "Database2");
                fs::copy_file(engineDbFile, *scratchDir / "Database2" / "m.db");
                scratchGuard.emplace(*scratchDir);
                writeTargetPath = scratchDir->string();
                log.record("consistency: applying " + std::to_string(engineWriteCount) +
                           " engine update(s) to a local scratch copy first (m.db is " +
                           std::to_string(existingBytes) + " bytes)");
            }

            infrastructure::engine::LibdjinteropEngineCueWriter cueWriter(writeTargetPath);
            infrastructure::engine::LibdjinteropEngineCleanupWriter cleanupWriter(writeTargetPath);

            for (const auto &issue : issues) {
                if (!issue.survivor) {
                    continue;
                }
                const auto &survivor = *issue.survivor;
                if (!issue.survivorCues.empty()) {
                    cueWriter.writeHotCues(survivor.sourceId, issue.survivorCues);
                    cuesMerged += static_cast<int>(issue.survivorCues.size());
                    log.record("consistency: merged cues onto survivor id=" + survivor.sourceId);
                }
                for (const auto &broken : issue.brokenGroup) {
                    cleanupWriter.removeTrackReplacingWith(broken.sourceId, survivor.sourceId);
                    log.record("consistency: removed broken row id=" + broken.sourceId + " (\"" + broken.title +
                                "\"), replaced by survivor id=" + survivor.sourceId);
                    rowsRemoved++;
                }
            }

            if (scratchDir && !infrastructure::copyFileDurablyAtomic(
                                   (*scratchDir / "Database2" / "m.db").string(), engineDbFile)) {
                throw std::runtime_error(
                    "consistency: failed to commit the scratch-repaired engine library back onto the stick");
            }
        } else if (format == "onelibrary") {
            std::string pioneerRoot = path.toStdString();
            std::string dbFile = infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(pioneerRoot);
            auto record = backupStore.backup({dbFile}, "consistency-repair");
            log.record("consistency: backed up exportLibrary.db -> " + record.path);

            int oneLibWriteCount = 0;
            for (const auto &issue : issues) {
                if (!issue.survivor) {
                    continue;
                }
                if (!issue.survivorCues.empty()) {
                    oneLibWriteCount++;
                }
                oneLibWriteCount += static_cast<int>(issue.brokenGroup.size());
            }
            std::error_code sizeEc;
            auto existingBytes = fs::file_size(dbFile, sizeEc);
            infrastructure::BulkWriteStrategyInputs strategyInputs;
            strategyInputs.itemCount = oneLibWriteCount;
            strategyInputs.existingFileBytes = sizeEc ? 0 : existingBytes;
            bool useWholeFile = !sizeEc && infrastructure::shouldUseWholeFileReplace(strategyInputs) &&
                                 infrastructure::hasRoomForWholeFileReplace(fs::path(dbFile).parent_path(), existingBytes);

            std::optional<fs::path> scratchDir;
            std::optional<ScratchDirGuard> scratchGuard;
            std::string writeTargetRoot = pioneerRoot;
            if (useWholeFile) {
                scratchDir = fs::temp_directory_path() /
                             ("seabass-repair-scratch-onelibrary-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
                std::error_code cleanupEc;
                fs::remove_all(*scratchDir, cleanupEc);
                fs::create_directories(*scratchDir / "rekordbox");
                fs::copy_file(dbFile, *scratchDir / "rekordbox" / "exportLibrary.db");
                scratchGuard.emplace(*scratchDir);
                writeTargetRoot = scratchDir->string();
                log.record("consistency: applying " + std::to_string(oneLibWriteCount) +
                           " OneLibrary update(s) to a local scratch copy first (exportLibrary.db is " +
                           std::to_string(existingBytes) + " bytes)");
            }

            infrastructure::onelibrary::OneLibraryCueWriter writer(writeTargetRoot,
                                                                    fs::path(pioneerRoot).parent_path().string());
            for (const auto &issue : issues) {
                if (!issue.survivor) {
                    continue;
                }
                const auto &survivor = *issue.survivor;
                if (!issue.survivorCues.empty()) {
                    writer.writeCuesForPath(survivor.filePath, issue.survivorCues);
                    cuesMerged += static_cast<int>(issue.survivorCues.size());
                    log.record("consistency: merged cues onto survivor \"" + survivor.title + "\"");
                }
                for (const auto &broken : issue.brokenGroup) {
                    writer.removeTrackByPathReplacingWith(broken.filePath, survivor.filePath);
                    log.record("consistency: removed broken row \"" + broken.title + "\"");
                    rowsRemoved++;
                }
            }

            if (scratchDir && !infrastructure::copyFileDurablyAtomic(
                                   (*scratchDir / "rekordbox" / "exportLibrary.db").string(), dbFile)) {
                throw std::runtime_error(
                    "consistency: failed to commit the scratch-repaired OneLibrary database back onto the stick");
            }
        } else {
            result.errorMessage = "Unknown library format: " + format;
            return result;
        }

        result.statusMessage =
            QString("Repaired %1 row(s)%2.")
                .arg(rowsRemoved)
                .arg(cuesMerged > 0 ? QString(", merged %1 cue(s) onto survivor(s)").arg(cuesMerged) : QString());
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

// Runs entirely on a background thread (see LibraryConsistencyController::
// deleteOrphan()). OneLibrary only, see class comment.
LibraryConsistencyWriteResult runDeleteOrphanTask(QString path, LibraryConsistencyIssue issue)
{
    LibraryConsistencyWriteResult result;
    QString refusal = refuseIfRekordboxRunning();
    if (!refusal.isEmpty()) {
        result.errorMessage = refusal;
        return result;
    }
    try {
        fs::path stickRoot = fs::path(path.toStdString()).parent_path();
        infrastructure::backup::StickWriteLock lock((stickRoot / ".seabass-backups" / ".write.lock").string());
        infrastructure::backup::FilesystemBackupStore backupStore((stickRoot / ".seabass-backups").string());
        infrastructure::logging::FileOperationLog log((stickRoot / ".seabass.log").string());

        std::string pioneerRoot = path.toStdString();
        std::string dbFile = infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(pioneerRoot);
        auto record = backupStore.backup({dbFile}, "consistency-delete-orphan");
        log.record("consistency: backed up exportLibrary.db -> " + record.path);

        infrastructure::onelibrary::OneLibraryCueWriter writer(pioneerRoot);
        int removed = 0;
        for (const auto &broken : issue.brokenGroup) {
            writer.removeTrackByPath(broken.filePath);
            log.record("consistency: deleted orphaned OneLibrary row \"" + broken.title + "\"");
            removed++;
        }
        result.statusMessage = QString("Deleted %1 orphaned row(s).").arg(removed);
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

// Runs entirely on a background thread (see LibraryConsistencyController::
// removeJunkCue()/removeAllJunkCues()). Rewrites each given track's full
// cue list with its offending 0:00 memory cue(s) removed, mirroring the
// survivor-cue write in runRepairTask() above: the same per-format
// writer, the same backup-first/write-lock/log conventions, and the same
// OneLibrary best-effort mirror when the row is rekordbox. One backup
// covers the whole batch (tagged "(bulk)" in its comment when there's
// more than one track), same as runRepairTask()'s single backup-then-
// loop-writes shape.
LibraryConsistencyWriteResult runRemoveJunkCuesTask(QString format, QString path, std::vector<domain::Track> tracks)
{
    LibraryConsistencyWriteResult result;
    QString refusal = refuseIfRekordboxRunning();
    if (!refusal.isEmpty()) {
        result.errorMessage = refusal;
        return result;
    }
    if (tracks.empty()) {
        return result;
    }
    try {
        fs::path stickRoot = fs::path(path.toStdString()).parent_path();
        infrastructure::backup::StickWriteLock lock((stickRoot / ".seabass-backups" / ".write.lock").string());
        infrastructure::backup::FilesystemBackupStore backupStore((stickRoot / ".seabass-backups").string());
        infrastructure::logging::FileOperationLog log((stickRoot / ".seabass.log").string());

        std::string backupComment = tracks.size() > 1 ? "junk-cue-cleanup (bulk)" : "junk-cue-cleanup";
        int removed = 0;

        auto cuesWithoutJunk = [](const domain::Track &track) {
            std::vector<domain::CuePoint> remainingCues;
            for (const auto &c : track.cues) {
                if (!(c.kind == domain::CuePoint::Kind::Memory && c.positionMs == 0.0)) {
                    remainingCues.push_back(c);
                }
            }
            return remainingCues;
        };

        if (format == "rekordbox") {
            std::string pioneerRoot = path.toStdString();
            auto record = backupStore.backup({pioneerRoot + "/rekordbox/export.pdb"}, backupComment);
            log.record("junk-cue: backed up export.pdb -> " + record.path);
            std::set<std::string> backedUpAnlz;
            bool hasOneLib = infrastructure::onelibrary::OneLibraryCueWriter::existsFor(pioneerRoot);
            infrastructure::rekordbox::RekordboxCueWriter writer(pioneerRoot);

            for (const auto &track : tracks) {
                auto analyzePath = infrastructure::rekordbox::findAnlzPathForTrackId(
                    pioneerRoot, static_cast<uint32_t>(std::stoul(track.sourceId)));
                if (analyzePath) {
                    std::string extPath = infrastructure::rekordbox::extAnlzPath(pioneerRoot, *analyzePath);
                    if (backedUpAnlz.insert(extPath).second) {
                        auto anlzRecord = backupStore.backup({extPath}, backupComment);
                        log.record("junk-cue: backed up -> " + anlzRecord.path);
                    }
                }
                auto remainingCues = cuesWithoutJunk(track);
                writer.writeHotCues(track.sourceId, remainingCues);
                log.record("junk-cue: removed 0:00 memory cue from \"" + track.title + "\" (id=" + track.sourceId +
                            ")");
                removed++;

                if (hasOneLib && !track.filePath.empty()) {
                    try {
                        infrastructure::onelibrary::OneLibraryCueWriter oneLibWriter(pioneerRoot);
                        oneLibWriter.writeCuesForPath(track.filePath, remainingCues);
                    } catch (const std::exception &e) {
                        log.record(std::string("junk-cue: OneLibrary cue mirror failed: ") + e.what());
                    }
                }
            }
        } else if (format == "engine") {
            std::string engineLibraryPath = path.toStdString();
            std::string engineDbFile = (fs::path(engineLibraryPath) / "Database2" / "m.db").string();
            auto record = backupStore.backup({engineDbFile}, backupComment);
            log.record("junk-cue: backed up m.db -> " + record.path);
            infrastructure::engine::LibdjinteropEngineCueWriter writer(engineLibraryPath);

            for (const auto &track : tracks) {
                auto remainingCues = cuesWithoutJunk(track);
                writer.writeHotCues(track.sourceId, remainingCues);
                log.record("junk-cue: removed 0:00 memory cue from \"" + track.title + "\" (id=" + track.sourceId +
                            ")");
                removed++;
            }
        } else if (format == "onelibrary") {
            std::string pioneerRoot = path.toStdString();
            std::string dbFile = infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(pioneerRoot);
            auto record = backupStore.backup({dbFile}, backupComment);
            log.record("junk-cue: backed up exportLibrary.db -> " + record.path);
            infrastructure::onelibrary::OneLibraryCueWriter writer(pioneerRoot);

            for (const auto &track : tracks) {
                auto remainingCues = cuesWithoutJunk(track);
                writer.writeCuesForPath(track.filePath, remainingCues);
                log.record("junk-cue: removed 0:00 memory cue from \"" + track.title + "\"");
                removed++;
            }
        } else {
            result.errorMessage = "Unknown library format: " + format;
            return result;
        }

        result.statusMessage = removed == 1
            ? QString("Removed the 0:00 memory cue from \"%1\".").arg(QString::fromStdString(tracks.front().title))
            : QString("Removed %1 memory cue(s) at 0:00.").arg(removed);
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

}  // namespace

LibraryConsistencyController::LibraryConsistencyController(QObject *parent) : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<LibraryConsistencyScanResult>::finished, this,
            &LibraryConsistencyController::onScanFinished);
    connect(&m_writeWatcher, &QFutureWatcher<LibraryConsistencyWriteResult>::finished, this,
            &LibraryConsistencyController::onWriteFinished);
}

std::shared_ptr<QtProgressReporter> LibraryConsistencyController::makeReporter()
{
    auto reporter = std::make_shared<QtProgressReporter>();
    connect(reporter.get(), &QtProgressReporter::started, this,
            [this](const QString &, int total) { setScanProgress(0, total); });
    connect(reporter.get(), &QtProgressReporter::progressed, this,
            [this](int current) { setScanProgress(current, m_scanTotal); });
    return reporter;
}

QString LibraryConsistencyController::pathForFormat(const QString &format) const
{
    return format == "engine" ? m_enginePath : m_rekordboxPath;
}

void LibraryConsistencyController::scan(const QString &rekordboxPath, const QString &enginePath,
                                          const QString &playlistName)
{
    if (m_busy) {
        return;
    }
    m_rekordboxPath = rekordboxPath;
    m_enginePath = enginePath;
    m_currentPlaylistName = playlistName;
    m_model.clear();
    m_junkCueModel.clear();
    // Deliberately NOT clearing m_playlistNames/m_playlistTrackCounts
    // here: this scan() call is itself reachable synchronously from
    // inside PlaylistPickerCombo's own row-delegate onClicked handler
    // (JunkCuePage.qml's onPlaylistPicked calls straight back into
    // scan() before that handler's own next line, popup.close(), runs).
    // Clearing them here used to fire issuesChanged() synchronously mid-
    // click, which re-evaluated JunkCuePage.qml's playlistPickerModel
    // and reset the combo's own popup model out from under the very
    // delegate item whose click handler was still executing -- Quick
    // then crashed on a later frame's polishItems() pass, dereferencing
    // that now-destroyed item (confirmed via gdb: SIGSEGV in
    // QQuickItem::setY -> QQuickWindowPrivate::polishItems(), and the
    // popup visibly failing to close, both symptoms of the corrupted
    // item tree). These two never actually needed clearing mid-page-
    // lifetime anyway: rekordboxPath/enginePath don't change within one
    // page's life, only playlistName does, and a playlist's existence/
    // track count doesn't change from a cue repair -- see this
    // property's own doc comment ("picking one never shrinks the
    // picker's own list of choices"), which this now actually holds
    // for every scan() call, not just the first.
    emit issuesChanged();
    setErrorMessage({});
    setStatusMessage({});
    setScanProgress(0, 0);

    m_pendingScanFormats.clear();
    if (!rekordboxPath.isEmpty()) {
        m_pendingScanFormats.push_back("rekordbox");
    }
    if (!enginePath.isEmpty()) {
        m_pendingScanFormats.push_back("engine");
    }
    if (!rekordboxPath.isEmpty() &&
        infrastructure::onelibrary::OneLibraryCueWriter::existsFor(rekordboxPath.toStdString())) {
        m_pendingScanFormats.push_back("onelibrary");
    }

    setBusy(true);
    scanNextPendingFormat();
}

void LibraryConsistencyController::scanNextPendingFormat()
{
    if (m_pendingScanFormats.empty()) {
        setScanningFormat({});
        setBusy(false);
        return;
    }
    QString format = m_pendingScanFormats.front();
    m_pendingScanFormats.erase(m_pendingScanFormats.begin());
    setScanningFormat(format);
    m_watcher.setFuture(
        QtConcurrent::run(runScanTask, format, pathForFormat(format), m_currentPlaylistName, makeReporter()));
}

void LibraryConsistencyController::onScanFinished()
{
    LibraryConsistencyScanResult result = m_watcher.result();
    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
    } else {
        m_model.appendIssues(std::move(result.issues));
        m_junkCueModel.appendIssues(std::move(result.junkCues));
        mergePlaylistSummary(result.playlistNames, result.playlistTrackCounts);
        emit issuesChanged();
    }
    scanNextPendingFormat();
}

void LibraryConsistencyController::mergePlaylistSummary(const QStringList &names, const QVariantMap &counts)
{
    for (const auto &name : names) {
        int count = counts.value(name).toInt();
        if (m_playlistTrackCounts.contains(name)) {
            m_playlistTrackCounts[name] = std::max(m_playlistTrackCounts.value(name).toInt(), count);
        } else {
            m_playlistNames << name;
            m_playlistTrackCounts[name] = count;
        }
    }
    // Formats scan (and complete) one at a time, each contributing its
    // own already-sorted slice -- re-sorting the whole list after every
    // merge keeps the picker's overall order stable/alphabetical rather
    // than however the per-format slices happened to interleave.
    m_playlistNames.sort();
}

int LibraryConsistencyController::repairableCount() const
{
    int n = 0;
    for (const auto &issue : m_model.issues()) {
        if (issue.kind == LibraryConsistencyIssue::Kind::Repairable) {
            n++;
        }
    }
    return n;
}

void LibraryConsistencyController::repairAll()
{
    if (m_busy) {
        return;
    }
    std::map<QString, std::vector<LibraryConsistencyIssue>> byFormat;
    m_pendingRepairedIndices.clear();
    m_pendingRepairTouchedRekordbox = false;
    const auto &issues = m_model.issues();
    for (size_t i = 0; i < issues.size(); ++i) {
        const auto &issue = issues[i];
        if (issue.kind == LibraryConsistencyIssue::Kind::Repairable) {
            QString format = issueFormat(issue);
            byFormat[format].push_back(issue);
            m_pendingRepairedIndices.push_back(static_cast<int>(i));
            if (format == "rekordbox") {
                m_pendingRepairTouchedRekordbox = true;
            }
        }
    }
    if (byFormat.empty()) {
        return;
    }

    m_pendingRepairs.clear();
    for (auto &[format, issuesForFormat] : byFormat) {
        m_pendingRepairs.emplace_back(format, std::move(issuesForFormat));
    }

    m_pendingWriteKind = PendingWriteKind::Repair;
    setErrorMessage({});
    setStatusMessage({});
    setBusy(true);
    setWriting(true);

    // Kick off the first format's repair; onWriteFinished() pops and
    // starts the rest of m_pendingRepairs as each one completes.
    QString format = m_pendingRepairs.front().first;
    auto pendingIssues = std::move(m_pendingRepairs.front().second);
    m_pendingRepairs.erase(m_pendingRepairs.begin());
    m_writeWatcher.setFuture(QtConcurrent::run(runRepairTask, format, pathForFormat(format), pendingIssues));
}

void LibraryConsistencyController::repairOne(int index)
{
    if (m_busy) {
        return;
    }
    const auto &issues = m_model.issues();
    if (index < 0 || static_cast<size_t>(index) >= issues.size()) {
        return;
    }
    const auto &issue = issues[static_cast<size_t>(index)];
    if (issue.kind != LibraryConsistencyIssue::Kind::Repairable) {
        return;
    }
    QString format = issueFormat(issue);

    m_pendingRepairs.clear();
    m_pendingRepairs.emplace_back(format, std::vector<LibraryConsistencyIssue>{issue});
    m_pendingRepairedIndices = {index};
    m_pendingRepairTouchedRekordbox = (format == "rekordbox");

    m_pendingWriteKind = PendingWriteKind::Repair;
    setErrorMessage({});
    setStatusMessage({});
    setBusy(true);
    setWriting(true);
    // Repairing one item just means the pending-repair batch has exactly
    // one (format, [issue]) entry, reuse the exact same chain as
    // repairAll() rather than a parallel single-item write path.
    auto pendingIssues = std::move(m_pendingRepairs.front().second);
    m_pendingRepairs.erase(m_pendingRepairs.begin());
    m_writeWatcher.setFuture(QtConcurrent::run(runRepairTask, format, pathForFormat(format), pendingIssues));
}

void LibraryConsistencyController::deleteOrphan(int index)
{
    if (m_busy) {
        return;
    }
    const auto &issues = m_model.issues();
    if (index < 0 || static_cast<size_t>(index) >= issues.size()) {
        return;
    }
    const auto &issue = issues[static_cast<size_t>(index)];
    if (issue.kind != LibraryConsistencyIssue::Kind::Missing || issueFormat(issue) != "onelibrary") {
        return;
    }
    LibraryConsistencyIssue issueCopy = issue;

    m_pendingWriteKind = PendingWriteKind::Repair;
    m_pendingRepairedIndices = {index};
    m_pendingRepairTouchedRekordbox = false;  // deleteOrphan is OneLibrary-only, see the check above
    setErrorMessage({});
    setStatusMessage({});
    setBusy(true);
    setWriting(true);
    m_writeWatcher.setFuture(QtConcurrent::run(runDeleteOrphanTask, m_rekordboxPath, std::move(issueCopy)));
}

void LibraryConsistencyController::removeJunkCue(int index)
{
    if (m_busy) {
        return;
    }
    const auto &issues = m_junkCueModel.issues();
    if (index < 0 || static_cast<size_t>(index) >= issues.size()) {
        return;
    }
    domain::Track track = issues[static_cast<size_t>(index)].track;
    QString format = QString::fromStdString(track.format);

    m_pendingWriteKind = PendingWriteKind::RemoveOneJunkCue;
    m_pendingJunkCueRemovalIndex = index;
    setErrorMessage({});
    setStatusMessage({});
    setBusy(true);
    setWriting(true);
    m_writeWatcher.setFuture(QtConcurrent::run(runRemoveJunkCuesTask, format, pathForFormat(format),
                                                std::vector<domain::Track>{std::move(track)}));
}

void LibraryConsistencyController::removeAllJunkCues()
{
    if (m_busy) {
        return;
    }
    std::map<QString, std::vector<domain::Track>> byFormat;
    for (const auto &issue : m_junkCueModel.issues()) {
        byFormat[QString::fromStdString(issue.track.format)].push_back(issue.track);
    }
    if (byFormat.empty()) {
        return;
    }

    m_pendingJunkCueRemovals.clear();
    for (auto &[format, tracks] : byFormat) {
        m_pendingJunkCueRemovals.emplace_back(format, std::move(tracks));
    }

    m_pendingWriteKind = PendingWriteKind::RemoveAllJunkCues;
    setErrorMessage({});
    setStatusMessage({});
    setBusy(true);
    setWriting(true);

    // Kick off the first format's removal batch; onWriteFinished() pops
    // and starts the rest of m_pendingJunkCueRemovals as each completes,
    // same chaining shape as repairAll()/m_pendingRepairs.
    QString format = m_pendingJunkCueRemovals.front().first;
    auto tracks = std::move(m_pendingJunkCueRemovals.front().second);
    m_pendingJunkCueRemovals.erase(m_pendingJunkCueRemovals.begin());
    m_writeWatcher.setFuture(QtConcurrent::run(runRemoveJunkCuesTask, format, pathForFormat(format), tracks));
}

void LibraryConsistencyController::ignoreJunkCue(int index)
{
    m_junkCueModel.removeAt(index);
}

void LibraryConsistencyController::ignoreAllJunkCues()
{
    m_junkCueModel.clear();
}

void LibraryConsistencyController::onWriteFinished()
{
    LibraryConsistencyWriteResult result = m_writeWatcher.result();
    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
    } else if (!result.statusMessage.isEmpty()) {
        setStatusMessage(result.statusMessage);
    }

    if (!m_pendingRepairs.empty()) {
        QString format = m_pendingRepairs.front().first;
        auto pendingIssues = std::move(m_pendingRepairs.front().second);
        m_pendingRepairs.erase(m_pendingRepairs.begin());
        m_writeWatcher.setFuture(QtConcurrent::run(runRepairTask, format, pathForFormat(format), pendingIssues));
        return;
    }
    if (!m_pendingJunkCueRemovals.empty()) {
        QString format = m_pendingJunkCueRemovals.front().first;
        auto tracks = std::move(m_pendingJunkCueRemovals.front().second);
        m_pendingJunkCueRemovals.erase(m_pendingJunkCueRemovals.begin());
        m_writeWatcher.setFuture(QtConcurrent::run(runRemoveJunkCuesTask, format, pathForFormat(format), tracks));
        return;
    }

    setBusy(false);
    setWriting(false);

    // A write may have touched any/all of the three catalogs -- rather
    // than plumbing through exactly which one(s) a given batch actually
    // wrote to, just invalidate all three; even the local-update
    // branches below that skip the immediate re-scan still need this,
    // so a *later* scan (switching playlists, reopening the page) can't
    // read stale cached tracks from before the write within the same
    // mtime-granularity window. Same convention as SyncController::
    // onWriteFinished().
    auto &catalogCache = LibraryCatalogCache::instance();
    catalogCache.invalidate("rekordbox", m_rekordboxPath.toStdString());
    catalogCache.invalidate("engine", m_enginePath.toStdString());
    catalogCache.invalidate("onelibrary", m_rekordboxPath.toStdString());

    if (m_pendingWriteKind == PendingWriteKind::RemoveOneJunkCue) {
        // Removing a memory cue at 0:00 can't change file existence or
        // which broken row matches which survivor -- the only two
        // things LibraryConsistencyChecker actually looks at -- so
        // there's nothing a full re-scan of the whole catalog from
        // removable media could reveal here. Traced with gdb: a re-scan
        // after every single removal was the entire cost of "removing
        // stray cues is slow" (confirmed via repeated stack sampling
        // during a real removal -- 100% of the sampled time was inside
        // runScanTask/KaitaiRekordboxReader::readAll, never inside the
        // write itself). Just drop the row locally instead, same as the
        // no-write Ignore path already does.
        m_junkCueModel.removeAt(m_pendingJunkCueRemovalIndex);
        m_pendingJunkCueRemovalIndex = -1;
    } else if (m_pendingWriteKind == PendingWriteKind::RemoveAllJunkCues) {
        m_junkCueModel.clear();
    } else if (m_pendingRepairTouchedRekordbox) {
        // A rekordbox repair mirrors its cue merge/row removal into
        // OneLibrary best-effort (see runRepairTask's "rekordbox"
        // branch), and m_model combines every format's issues into one
        // list -- that mirror write can silently stale an already-
        // listed OneLibrary issue in ways only a fresh
        // LibraryConsistencyChecker::check() run across every format
        // could catch. Re-scan for real here, staying scoped to
        // whatever playlist was selected rather than reverting to "All
        // tracks". Every other repair/delete has no such cross-catalog
        // side effect (verified: DuplicateTrackFinder groups by
        // filename/title+artist+duration only, never cues or row
        // existence, so repairing one group can't reclassify another)
        // and is handled by the local removal below instead.
        scan(m_rekordboxPath, m_enginePath, m_currentPlaylistName);
    } else {
        std::sort(m_pendingRepairedIndices.rbegin(), m_pendingRepairedIndices.rend());
        for (int idx : m_pendingRepairedIndices) {
            m_model.removeIssueAt(idx);
        }
        m_pendingRepairedIndices.clear();
        emit issuesChanged();  // repairableCount reads m_model.issues()
    }
}

void LibraryConsistencyController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void LibraryConsistencyController::setWriting(bool writing)
{
    if (m_writing == writing) {
        return;
    }
    m_writing = writing;
    emit writingChanged();
}

void LibraryConsistencyController::setScanProgress(int current, int total)
{
    if (m_scanCurrent == current && m_scanTotal == total) {
        return;
    }
    m_scanCurrent = current;
    m_scanTotal = total;
    emit scanProgressChanged();
}

void LibraryConsistencyController::setScanningFormat(const QString &format)
{
    if (m_scanningFormat == format) {
        return;
    }
    m_scanningFormat = format;
    emit scanningFormatChanged();
}

void LibraryConsistencyController::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

void LibraryConsistencyController::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) {
        return;
    }
    m_statusMessage = message;
    emit statusMessageChanged();
}

}  // namespace seabass::gui
