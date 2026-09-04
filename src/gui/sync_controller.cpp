#include "sync_controller.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>

#include "application/ports/backup_store.hpp"
#include "application/use_cases/scan_library.hpp"
#include "application/use_cases/sync_libraries.hpp"
#include "domain/cross_source_sync_conflict.hpp"
#include "domain/track_scope.hpp"
#include "gui/library_catalog_cache.hpp"
#include "gui/qt_progress_reporter.hpp"
#include "gui/write_guard.hpp"
#include "infrastructure/backup/filesystem_backup_store.hpp"
#include "infrastructure/backup/stick_write_lock.hpp"
#include "infrastructure/bulk_write_strategy.hpp"
#include "infrastructure/durable_file_write.hpp"
#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/engine/libdjinterop_engine_reader.hpp"
#include "infrastructure/logging/file_operation_log.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/onelibrary/onelibrary_reader.hpp"
#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"
#include "infrastructure/rekordbox/pdb_lookup.hpp"
#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"
#include "infrastructure/scratch_dir_guard.hpp"

namespace seabass::gui
{

namespace fs = std::filesystem;
using domain::SyncPlan;

namespace
{

using infrastructure::hasRoomForWholeFileReplace;
using infrastructure::ScratchDirGuard;

// Mirrors cli/main.cpp's describeCues() exactly.
QString describeCues(const std::vector<domain::CuePoint> &cues)
{
    int hot = 0;
    int memory = 0;
    for (const auto &cue : cues) {
        (cue.kind == domain::CuePoint::Kind::Hot ? hot : memory)++;
    }
    QString result = QString("%1 hot").arg(hot);
    if (memory > 0) {
        result += QString(", %1 memory (not written - Engine writer only handles hot cues)").arg(memory);
    }
    return result;
}

}  // namespace

SyncPlanListModel::SyncPlanListModel(QObject *parent) : QAbstractListModel(parent) {}

int SyncPlanListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_plans.size());
}

namespace
{

// No waveform field here -- see sync_controller.hpp's own comment on
// SyncPlanListModel::setPlans() for why: QML fetches a track's waveform
// on demand via PlaybackController::waveformFor() instead of this
// controller decoding one eagerly for every actionable track up front.
QVariantMap trackToMap(const domain::Track &track)
{
    QVariantMap m;
    m["side"] = QString::fromStdString(track.format);
    m["sourceId"] = QString::fromStdString(track.sourceId);
    m["title"] = QString::fromStdString(track.title);
    m["artist"] = QString::fromStdString(track.artist);
    m["filePath"] = QString::fromStdString(track.filePath);
    m["artworkPath"] = QString::fromStdString(track.artworkPath);
    m["durationMs"] = track.durationSeconds * 1000.0;

    QVariantList cues;
    for (const auto &c : track.cues) {
        QVariantMap cueMap;
        cueMap["kind"] = c.kind == domain::CuePoint::Kind::Hot ? QStringLiteral("hot") : QStringLiteral("memory");
        cueMap["hotCueNumber"] = c.hotCueNumber;
        cueMap["positionMs"] = c.positionMs;
        cueMap["isLoop"] = c.isLoop;
        cueMap["loopEndMs"] = c.loopEndMs;
        cueMap["color"] = QString::fromStdString(c.color);
        cueMap["comment"] = QString::fromStdString(c.comment);
        cues << cueMap;
    }
    m["cues"] = cues;
    return m;
}

}  // namespace

QVariant SyncPlanListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= m_plans.size()) {
        return {};
    }
    const auto &plan = m_plans[static_cast<size_t>(index.row())];
    bool toB = plan.direction == SyncPlan::Direction::ToB;
    const domain::Track &source = toB ? plan.match.trackA : plan.match.trackB;
    const domain::Track &target = toB ? plan.match.trackB : plan.match.trackA;
    switch (role) {
    case SourceFormatRole:
        return QString::fromStdString(source.format);
    case TargetFormatRole:
        return QString::fromStdString(target.format);
    case FilenameRole:
        return QString::fromStdString(source.filename);
    case DescriptionRole:
        return describeCues(plan.cuesToApply);
    case ConflictRole:
        return plan.kind == SyncPlan::Kind::Conflict;
    case TracksRole:
        return QVariantList{trackToMap(plan.match.trackA), trackToMap(plan.match.trackB)};
    default:
        return {};
    }
}

QHash<int, QByteArray> SyncPlanListModel::roleNames() const
{
    return {
        {SourceFormatRole, "sourceFormat"},
        {TargetFormatRole, "targetFormat"},
        {FilenameRole, "filename"},
        {DescriptionRole, "description"},
        {ConflictRole, "conflict"},
        {TracksRole, "tracks"},
    };
}

void SyncPlanListModel::setPlans(std::vector<domain::SyncPlan> plans)
{
    beginResetModel();
    m_plans = std::move(plans);
    endResetModel();
}

void SyncPlanListModel::addPlan(domain::SyncPlan plan)
{
    int row = static_cast<int>(m_plans.size());
    beginInsertRows(QModelIndex(), row, row);
    m_plans.push_back(std::move(plan));
    endInsertRows();
}

namespace
{

std::chrono::system_clock::time_point fileMtime(const std::string &path)
{
    return std::chrono::clock_cast<std::chrono::system_clock>(std::filesystem::last_write_time(path));
}

// Builds SyncTaskResult::playlistNames/playlistTrackCounts from the union
// of every catalog's own (unfiltered) tracks -- called before any
// TrackScope filtering below, so picking a playlist never shrinks the
// picker's own list of choices. A given playlist name's count is the max
// across whichever catalogs have it, not a sum: the same playlist
// typically exists independently in each catalog present on a stick with
// near-identical membership, and summing would roughly double-count it
// whenever two catalogs are present, without meaning "distinct real
// tracks" (that would need real cross-catalog matching, not just a
// display count).
void collectPlaylistSummary(const std::vector<domain::Track> &rekordboxTracks,
                             const std::vector<domain::Track> &engineTracks,
                             const std::vector<domain::Track> &oneLibraryTracks, SyncTaskResult &result)
{
    std::unordered_map<std::string, int> maxCountByName;
    auto tally = [&](const std::vector<domain::Track> &tracks) {
        std::unordered_map<std::string, int> countThisCatalog;
        for (const auto &track : tracks) {
            for (const auto &playlist : track.playlists) {
                countThisCatalog[playlist.name]++;
            }
        }
        for (const auto &[name, count] : countThisCatalog) {
            int &best = maxCountByName[name];
            best = std::max(best, count);
        }
    };
    tally(rekordboxTracks);
    tally(engineTracks);
    tally(oneLibraryTracks);

    std::set<std::string> sortedNames;
    for (const auto &[name, count] : maxCountByName) {
        sortedNames.insert(name);
    }
    for (const auto &name : sortedNames) {
        QString qName = QString::fromStdString(name);
        result.playlistNames << qName;
        result.playlistTrackCounts[qName] = maxCountByName[name];
    }
}

// Runs entirely on a background thread (see SyncController::analyze()) -
// no access to the controller itself. Scans whichever of the three
// catalogs are present (via the shared LibraryCatalogCache -- a repeat
// analyze()/Re-Analyze on an unchanged stick pays no disk-read cost at
// all), scopes them to playlistName when it's non-empty, then runs the
// exact same real diff+direction logic (domain::SyncLibraries) once per
// pair actually available on this stick, combining every pair's
// actionable plans into one list.
SyncTaskResult runAnalyzeTask(QString rekordboxPath, QString enginePath, QString playlistName,
                               std::shared_ptr<QtProgressReporter> reporter)
{
    SyncTaskResult result;
    try {
        bool hasRekordbox = !rekordboxPath.isEmpty();
        bool hasEngine = !enginePath.isEmpty();
        bool hasOneLibrary = false;

        std::vector<domain::Track> rekordboxTracks, engineTracks, oneLibraryTracks;
        std::chrono::system_clock::time_point rekordboxMtime, engineMtime, oneLibraryMtime;

        auto &catalogCache = LibraryCatalogCache::instance();

        if (hasRekordbox) {
            rekordboxTracks = catalogCache.tracksFor("rekordbox", rekordboxPath.toStdString(), *reporter);
            rekordboxMtime = fileMtime((fs::path(rekordboxPath.toStdString()) / "rekordbox" / "export.pdb").string());
            hasOneLibrary = infrastructure::onelibrary::OneLibraryCueWriter::existsFor(rekordboxPath.toStdString());
        }
        if (hasEngine) {
            engineTracks = catalogCache.tracksFor("engine", enginePath.toStdString(), *reporter);
            // Streaming tracks (TIDAL) have no real local file. Never
            // sync cues onto/from one. See domain::Track::streamingSource's
            // own doc comment.
            engineTracks.erase(std::remove_if(engineTracks.begin(), engineTracks.end(),
                                               [](const domain::Track &t) { return !t.streamingSource.empty(); }),
                                engineTracks.end());
            engineMtime = fileMtime((fs::path(enginePath.toStdString()) / "Database2" / "m.db").string());
        }
        if (hasOneLibrary) {
            oneLibraryTracks = catalogCache.tracksFor("onelibrary", rekordboxPath.toStdString(), *reporter);
            oneLibraryMtime =
                fileMtime(infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(rekordboxPath.toStdString()));
        }

        collectPlaylistSummary(rekordboxTracks, engineTracks, oneLibraryTracks, result);

        if (!playlistName.isEmpty()) {
            domain::TrackScope scope = domain::TrackScope::playlist(playlistName.toStdString());
            rekordboxTracks = domain::filterByScope(rekordboxTracks, scope);
            engineTracks = domain::filterByScope(engineTracks, scope);
            oneLibraryTracks = domain::filterByScope(oneLibraryTracks, scope);
        }

        result.rekordboxTrackCount = static_cast<int>(rekordboxTracks.size());
        result.engineTrackCount = static_cast<int>(engineTracks.size());
        result.oneLibraryTrackCount = static_cast<int>(oneLibraryTracks.size());

        std::vector<SyncPlan> actionable;
        auto addPairPlans = [&](const std::vector<domain::Track> &tracksA, const std::vector<domain::Track> &tracksB,
                                 std::chrono::system_clock::time_point mtimeA,
                                 std::chrono::system_clock::time_point mtimeB) {
            for (auto &plan : application::SyncLibraries().execute(tracksA, tracksB, mtimeA, mtimeB)) {
                if (plan.direction != SyncPlan::Direction::None) {
                    actionable.push_back(std::move(plan));
                }
            }
        };
        if (hasRekordbox && hasEngine) {
            addPairPlans(rekordboxTracks, engineTracks, rekordboxMtime, engineMtime);
        }
        if (hasRekordbox && hasOneLibrary) {
            addPairPlans(rekordboxTracks, oneLibraryTracks, rekordboxMtime, oneLibraryMtime);
        }
        if (hasEngine && hasOneLibrary) {
            addPairPlans(engineTracks, oneLibraryTracks, engineMtime, oneLibraryMtime);
        }

        // Two different pairs can independently target the same third
        // catalog's track (e.g. both rekordbox and Engine have cues
        // OneLibrary lacks) -- neither pairwise SyncPlanner can see the
        // other pair, so it can't know this is happening. Split those
        // out into unresolved conflicts (requires a manual pick, see
        // SyncController::resolveConflict()) before anything below
        // treats `actionable` as safe to apply directly.
        auto conflictSplit = domain::CrossSourceConflictDetector::detect(actionable);
        actionable = std::move(conflictSplit.nonConflicting);
        result.conflicts = std::move(conflictSplit.conflicts);

        // Waveforms are deliberately NOT loaded here -- see
        // sync_controller.hpp's own comment on SyncPlanList Model::
        // setPlans() for why eagerly decoding one per actionable track
        // (thousands, on a real library where most of it is actionable)
        // was the actual cause of a real "scanning takes forever" report,
        // confirmed at over 5 minutes on real removable media for a
        // ~1400-track library, against ~4 seconds for everything else in
        // this function combined. QML fetches a waveform on demand
        // instead, only for whichever rows are actually rendered.
        result.plans = std::move(actionable);
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

}  // namespace

SyncController::SyncController(QObject *parent) : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<SyncTaskResult>::finished, this, &SyncController::onAnalyzeFinished);
    connect(&m_writeWatcher, &QFutureWatcher<SyncWriteResult>::finished, this, &SyncController::onWriteFinished);
}

void SyncController::analyze(const QString &rekordboxPath, const QString &enginePath, const QString &playlistName)
{
    if (m_busy) {
        return;  // never overlap two analyses
    }
    m_rekordboxPath = rekordboxPath;
    m_enginePath = enginePath;
    m_currentPlaylistName = playlistName;
    setErrorMessage({});
    setStatusMessage({});
    setScanProgress(0, 0);
    setBusy(true);

    m_watcher.setFuture(QtConcurrent::run(runAnalyzeTask, rekordboxPath, enginePath, playlistName, makeReporter()));
}

// See ScanController::scan() for why the reporter is owned by the task
// (via shared_ptr) rather than by this controller.
std::shared_ptr<QtProgressReporter> SyncController::makeReporter()
{
    auto reporter = std::make_shared<QtProgressReporter>();
    connect(reporter.get(), &QtProgressReporter::started, this,
            [this](const QString &, int total) { setScanProgress(0, total); });
    connect(reporter.get(), &QtProgressReporter::progressed, this,
            [this](int current) { setScanProgress(current, m_scanTotal); });
    return reporter;
}

void SyncController::onAnalyzeFinished()
{
    SyncTaskResult result = m_watcher.result();

    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
        setBusy(false);
        return;
    }

    m_rekordboxTrackCount = result.rekordboxTrackCount;
    m_engineTrackCount = result.engineTrackCount;
    m_oneLibraryTrackCount = result.oneLibraryTrackCount;
    m_playlistNames = std::move(result.playlistNames);
    m_playlistTrackCounts = std::move(result.playlistTrackCounts);
    m_model.setPlans(std::move(result.plans));
    recomputeDirectionCounts();
    // A rescan is a fresh snapshot -- any conflict resolved against the
    // previous one no longer means anything (the plan it produced is
    // already gone too, replaced by whatever this scan found), so this
    // never tries to carry old resolutions forward.
    m_conflicts = std::move(result.conflicts);
    rebuildUnresolvedConflictsList();
    setBusy(false);
}

namespace
{

QString summarizeCueCounts(const std::vector<domain::CuePoint> &cues)
{
    int hot = 0;
    int hotLoop = 0;
    int memory = 0;
    for (const auto &cue : cues) {
        if (cue.kind == domain::CuePoint::Kind::Hot) {
            (cue.isLoop ? hotLoop : hot)++;
        } else {
            memory++;
        }
    }
    QStringList parts;
    if (hot > 0) {
        parts << QString("%1 hot cue(s)").arg(hot);
    }
    if (hotLoop > 0) {
        parts << QString("%1 hot loop(s)").arg(hotLoop);
    }
    if (memory > 0) {
        parts << QString("%1 memory cue(s)").arg(memory);
    }
    return parts.isEmpty() ? "no cues" : parts.join(", ");
}

}  // namespace

void SyncController::rebuildUnresolvedConflictsList()
{
    QVariantList list;
    for (const auto &conflict : m_conflicts) {
        // Same trackToMap() shape (cues, artwork, play-ready
        // filePath/sourceId) the ordinary plan list already renders with
        // TrackWaveformCard -- so a conflict can be investigated the
        // same way any other plan already is, not just read as a
        // one-line summary. Waveform itself is fetched on demand by
        // QML, not included here (see trackToMap()'s own comment).
        domain::Track sourceATrack = conflict.sourceA;
        sourceATrack.cues = conflict.cuesFromA;
        domain::Track sourceBTrack = conflict.sourceB;
        sourceBTrack.cues = conflict.cuesFromB;

        QVariantMap m;
        m["targetPath"] = QString::fromStdString(conflict.target.filePath);
        m["targetTitle"] = QString::fromStdString(conflict.target.title);
        m["targetArtist"] = QString::fromStdString(conflict.target.artist);
        m["targetFormat"] = QString::fromStdString(conflict.target.format);
        m["sourceAFormat"] = QString::fromStdString(conflict.sourceA.format);
        m["sourceASummary"] = summarizeCueCounts(conflict.cuesFromA);
        m["sourceAHasJunkCue"] = conflict.sourceAHasJunkCue;
        m["sourceATrack"] = trackToMap(sourceATrack);
        m["sourceBFormat"] = QString::fromStdString(conflict.sourceB.format);
        m["sourceBSummary"] = summarizeCueCounts(conflict.cuesFromB);
        m["sourceBHasJunkCue"] = conflict.sourceBHasJunkCue;
        m["sourceBTrack"] = trackToMap(sourceBTrack);
        list << m;
    }
    m_unresolvedConflicts = list;
    emit conflictsChanged();
}

void SyncController::resolveConflict(int index, bool useSourceA)
{
    if (index < 0 || static_cast<size_t>(index) >= m_conflicts.size()) {
        return;
    }
    const domain::CrossSourceSyncConflict &conflict = m_conflicts[static_cast<size_t>(index)];

    domain::SyncPlan plan;
    plan.kind = domain::SyncPlan::Kind::AOnly;
    plan.match.trackA = useSourceA ? conflict.sourceA : conflict.sourceB;
    plan.match.trackB = conflict.target;
    plan.direction = domain::SyncPlan::Direction::ToB;
    plan.cuesToApply = useSourceA ? conflict.cuesFromA : conflict.cuesFromB;
    m_model.addPlan(std::move(plan));
    recomputeDirectionCounts();

    m_conflicts.erase(m_conflicts.begin() + index);
    rebuildUnresolvedConflictsList();
}

void SyncController::recomputeDirectionCounts()
{
    std::map<std::pair<std::string, std::string>, int> counts;
    for (const auto &plan : m_model.plans()) {
        bool toB = plan.direction == SyncPlan::Direction::ToB;
        const std::string &sourceFormat = toB ? plan.match.trackA.format : plan.match.trackB.format;
        const std::string &targetFormat = toB ? plan.match.trackB.format : plan.match.trackA.format;
        counts[{sourceFormat, targetFormat}]++;
    }
    QVariantList list;
    for (const auto &[key, count] : counts) {
        QVariantMap m;
        m["sourceFormat"] = QString::fromStdString(key.first);
        m["targetFormat"] = QString::fromStdString(key.second);
        m["count"] = count;
        list << m;
    }
    m_directionCounts = list;
    emit analysisChanged();
}

namespace
{

// Acquires one StickWriteLock per distinct directory (sorted first so two
// concurrent multi-lock callers always acquire in the same order, and
// deduped so locking the same directory twice, e.g. rekordbox and
// OneLibrary sharing one stick root's .seabass-backups, never self-
// deadlocks). Held for the caller's whole scope via RAII.
std::vector<std::unique_ptr<infrastructure::backup::StickWriteLock>> acquireStickLocks(std::vector<std::string> dirs)
{
    std::sort(dirs.begin(), dirs.end());
    dirs.erase(std::unique(dirs.begin(), dirs.end()), dirs.end());
    std::vector<std::unique_ptr<infrastructure::backup::StickWriteLock>> locks;
    locks.reserve(dirs.size());
    for (const auto &dir : dirs) {
        locks.push_back(std::make_unique<infrastructure::backup::StickWriteLock>(dir + "/.write.lock"));
    }
    return locks;
}

QString formatLabel(const std::string &format)
{
    if (format == "engine") return "Engine";
    if (format == "onelibrary") return "OneLibrary";
    return "Rekordbox";
}

// One plan's cues, already resolved to a concrete target track plus the
// human-readable identity of where they came from (for logging).
struct WriteItem
{
    domain::Track targetTrack;
    std::vector<domain::CuePoint> cuesToApply;
    std::string sourceFormat;
    std::string sourceTitle;
    std::string sourceSourceId;
};

// Ports cli/main.cpp's runSyncCommand phase-3 block, generalized to any
// number of target catalogs instead of a fixed rekordbox/Engine pair:
// plans are grouped by which catalog is actually receiving the write
// (regardless of which pair they came from), so writing to e.g.
// "rekordbox" happens exactly once per stick even if some of those plans
// came from the rekordbox<->Engine pair and others from rekordbox<->
// OneLibrary. Runs entirely on a background thread (see
// SyncController::startApply()).
SyncWriteResult runApplyTask(QString rekordboxPath, QString enginePath, std::vector<SyncPlan> actionablePlans,
                              std::shared_ptr<QtProgressReporter> reporter)
{
    SyncWriteResult result;
    QString refusal = refuseIfRekordboxRunning();
    if (!refusal.isEmpty()) {
        result.errorMessage = refusal;
        return result;
    }
    try {
        // rekordbox and OneLibrary share the same PIONEER root/path.
        auto pathForFormat = [&](const std::string &format) { return format == "engine" ? enginePath : rekordboxPath; };

        std::map<std::string, std::vector<WriteItem>> byTargetFormat;
        for (const auto &plan : actionablePlans) {
            bool toB = plan.direction == SyncPlan::Direction::ToB;
            const domain::Track &target = toB ? plan.match.trackB : plan.match.trackA;
            const domain::Track &source = toB ? plan.match.trackA : plan.match.trackB;
            byTargetFormat[target.format].push_back({target, plan.cuesToApply, source.format, source.title, source.sourceId});
        }

        std::vector<std::string> lockDirs;
        if (!rekordboxPath.isEmpty()) {
            lockDirs.push_back((fs::path(rekordboxPath.toStdString()).parent_path() / ".seabass-backups").string());
        }
        if (!enginePath.isEmpty()) {
            lockDirs.push_back((fs::path(enginePath.toStdString()).parent_path() / ".seabass-backups").string());
        }
        auto locks = acquireStickLocks(lockDirs);

        reporter->start("Writing cues", actionablePlans.size());
        size_t written = 0;
        QStringList summaryParts;

        for (const auto &[targetFormat, items] : byTargetFormat) {
            QString path = pathForFormat(targetFormat);
            std::string stickRoot = fs::path(path.toStdString()).parent_path().string();
            infrastructure::backup::FilesystemBackupStore backupStore((fs::path(stickRoot) / ".seabass-backups").string());
            infrastructure::logging::FileOperationLog log((fs::path(stickRoot) / ".seabass.log").string());
            int cuesCopied = 0;

            try {
            if (targetFormat == "rekordbox") {
                infrastructure::rekordbox::RekordboxCueWriter writer(path.toStdString());
                std::string pioneerRoot = path.toStdString();
                auto record = backupStore.backup({pioneerRoot + "/rekordbox/export.pdb"}, "sync");
                log.record("sync: backed up export.pdb -> " + record.path);
                result.backups.push_back({QString::fromStdString(fs::path(record.path).parent_path().string()),
                                           QString::fromStdString(record.id)});
                std::set<std::string> backedUpAnlz;
                for (const auto &item : items) {
                    auto analyzePath = infrastructure::rekordbox::findAnlzPathForTrackId(
                        pioneerRoot, static_cast<uint32_t>(std::stoul(item.targetTrack.sourceId)));
                    if (analyzePath) {
                        std::string extPath = infrastructure::rekordbox::extAnlzPath(pioneerRoot, *analyzePath);
                        if (backedUpAnlz.insert(extPath).second) {
                            auto anlzRecord = backupStore.backup({extPath}, "sync");
                            log.record("sync: backed up -> " + anlzRecord.path);
                        }
                    }
                    writer.writeHotCues(item.targetTrack.sourceId, item.cuesToApply);
                    cuesCopied += static_cast<int>(item.cuesToApply.size());
                    log.record("sync: copied cues (" + describeCues(item.cuesToApply).toStdString() + ") from " +
                               item.sourceFormat + " track \"" + item.sourceTitle + "\" (id=" + item.sourceSourceId +
                               ") to rekordbox track id=" + item.targetTrack.sourceId);
                    reporter->tick(++written);
                }
            } else if (targetFormat == "engine") {
                std::string engineDbFile = (fs::path(path.toStdString()) / "Database2" / "m.db").string();
                auto record = backupStore.backup({engineDbFile}, "sync");
                log.record("sync: backed up m.db -> " + record.path);
                result.backups.push_back({QString::fromStdString(fs::path(record.path).parent_path().string()),
                                           QString::fromStdString(record.id)});

                std::error_code sizeEc;
                auto existingBytes = fs::file_size(engineDbFile, sizeEc);
                infrastructure::BulkWriteStrategyInputs strategyInputs;
                strategyInputs.itemCount = static_cast<int>(items.size());
                strategyInputs.existingFileBytes = sizeEc ? 0 : existingBytes;
                bool useWholeFile = !sizeEc && infrastructure::shouldUseWholeFileReplace(strategyInputs) &&
                                     hasRoomForWholeFileReplace(fs::path(engineDbFile).parent_path(), existingBytes);

                std::optional<fs::path> scratchDir;
                std::optional<ScratchDirGuard> scratchGuard;
                std::string writeTargetPath = path.toStdString();
                if (useWholeFile) {
                    scratchDir = fs::temp_directory_path() /
                                 ("seabass-sync-scratch-engine-" +
                                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
                    std::error_code cleanupEc;
                    fs::remove_all(*scratchDir, cleanupEc);
                    fs::create_directories(*scratchDir / "Database2");
                    fs::copy_file(engineDbFile, *scratchDir / "Database2" / "m.db");
                    scratchGuard.emplace(*scratchDir);
                    writeTargetPath = scratchDir->string();
                    log.record("sync: applying " + std::to_string(items.size()) +
                               " engine cue update(s) to a local scratch copy first (m.db is " +
                               std::to_string(existingBytes) + " bytes)");
                }

                infrastructure::engine::LibdjinteropEngineCueWriter writer(writeTargetPath);
                for (const auto &item : items) {
                    writer.writeHotCues(item.targetTrack.sourceId, item.cuesToApply);
                    cuesCopied += static_cast<int>(item.cuesToApply.size());
                    log.record("sync: copied cues (" + describeCues(item.cuesToApply).toStdString() + ") from " +
                               item.sourceFormat + " track \"" + item.sourceTitle + "\" (id=" + item.sourceSourceId +
                               ") to engine track id=" + item.targetTrack.sourceId);
                    reporter->tick(++written);
                }

                if (scratchDir &&
                    !infrastructure::copyFileDurablyAtomic((*scratchDir / "Database2" / "m.db").string(), engineDbFile)) {
                    throw std::runtime_error(
                        "sync: failed to commit the scratch-built engine library back onto the stick");
                }
            } else if (targetFormat == "onelibrary") {
                std::string dbFile = infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(path.toStdString());
                auto record = backupStore.backup({dbFile}, "sync");
                log.record("sync: backed up exportLibrary.db -> " + record.path);
                result.backups.push_back({QString::fromStdString(fs::path(record.path).parent_path().string()),
                                           QString::fromStdString(record.id)});

                std::error_code sizeEc;
                auto existingBytes = fs::file_size(dbFile, sizeEc);
                infrastructure::BulkWriteStrategyInputs strategyInputs;
                strategyInputs.itemCount = static_cast<int>(items.size());
                strategyInputs.existingFileBytes = sizeEc ? 0 : existingBytes;
                bool useWholeFile = !sizeEc && infrastructure::shouldUseWholeFileReplace(strategyInputs) &&
                                     hasRoomForWholeFileReplace(fs::path(dbFile).parent_path(), existingBytes);

                std::optional<fs::path> scratchDir;
                std::optional<ScratchDirGuard> scratchGuard;
                std::string writeTargetRoot = path.toStdString();
                if (useWholeFile) {
                    scratchDir = fs::temp_directory_path() /
                                 ("seabass-sync-scratch-onelibrary-" +
                                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
                    std::error_code cleanupEc;
                    fs::remove_all(*scratchDir, cleanupEc);
                    fs::create_directories(*scratchDir / "rekordbox");
                    fs::copy_file(dbFile, *scratchDir / "rekordbox" / "exportLibrary.db");
                    scratchGuard.emplace(*scratchDir);
                    writeTargetRoot = scratchDir->string();
                    log.record("sync: applying " + std::to_string(items.size()) +
                               " OneLibrary cue update(s) to a local scratch copy first (exportLibrary.db is " +
                               std::to_string(existingBytes) + " bytes)");
                }

                // realStickRoot is passed explicitly (not left to the
                // writer's own pioneerRoot-parent default) because
                // writeTargetRoot may be the scratch copy above, whose
                // parent is just a temp directory, not the stick --
                // content.path lookups need the *real* stick root
                // regardless of where the database file itself is being
                // read from right now.
                infrastructure::onelibrary::OneLibraryCueWriter writer(
                    writeTargetRoot, fs::path(path.toStdString()).parent_path().string());
                for (const auto &item : items) {
                    writer.writeCuesForPath(item.targetTrack.filePath, item.cuesToApply);
                    cuesCopied += static_cast<int>(item.cuesToApply.size());
                    log.record("sync: copied cues (" + describeCues(item.cuesToApply).toStdString() + ") from " +
                               item.sourceFormat + " track \"" + item.sourceTitle + "\" to OneLibrary track path=" +
                               item.targetTrack.filePath);
                    reporter->tick(++written);
                }

                if (scratchDir && !infrastructure::copyFileDurablyAtomic(
                                       (*scratchDir / "rekordbox" / "exportLibrary.db").string(), dbFile)) {
                    throw std::runtime_error(
                        "sync: failed to commit the scratch-built OneLibrary database back onto the stick");
                }
            }

            } catch (const std::exception &e) {
                log.record("sync: FAILED writing " + targetFormat + " (" + std::to_string(items.size()) +
                           " item(s) queued, " + std::to_string(cuesCopied) + " cue(s) actually copied first): " +
                           e.what());
                throw;
            }

            summaryParts << QString("%1 track(s) to %2 (%3 cue(s))")
                                .arg(items.size())
                                .arg(formatLabel(targetFormat))
                                .arg(cuesCopied);
        }
        reporter->finish();
        result.statusMessage = "Synced " + summaryParts.join(", ");
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

// Runs entirely on a background thread (see SyncController::
// undoLastOperation()). An empty `backups` result always means "nothing
// left to undo," whether that's because everything restored cleanly or
// because an error stopped the loop partway, either way the caller's
// undo trail is now stale and should be cleared.
SyncWriteResult runUndoTask(std::vector<UndoableBackup> backups, std::shared_ptr<QtProgressReporter> reporter)
{
    SyncWriteResult result;
    QString refusal = refuseIfRekordboxRunning();
    if (!refusal.isEmpty()) {
        result.errorMessage = refusal;
        return result;
    }
    int restored = 0;
    try {
        std::vector<std::string> dirs;
        for (const auto &backup : backups) {
            dirs.push_back(backup.backupDir.toStdString());
        }
        auto locks = acquireStickLocks(dirs);

        reporter->start("Undoing", backups.size());
        // Reverse order: if anything ever depends on write order, undoing
        // most-recent-first is the safer default.
        for (auto it = backups.rbegin(); it != backups.rend(); ++it) {
            infrastructure::backup::FilesystemBackupStore store(it->backupDir.toStdString());
            if (store.restore(it->id.toStdString())) {
                restored++;
            }
            reporter->tick(static_cast<size_t>(restored));
        }
        reporter->finish();
        result.statusMessage = QString("Undone - restored %1 file(s) to their state before the last sync").arg(restored);
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

}  // namespace

// Phase 2 (the confirmation gate) + phase 3: writes every plan currently in
// the model. See runApplyTask() for the actual backup/write logic.
void SyncController::apply()
{
    if (m_busy) {
        return;
    }
    std::vector<SyncPlan> actionable;
    for (const auto &plan : m_model.plans()) {
        if (plan.direction != SyncPlan::Direction::None) {
            actionable.push_back(plan);
        }
    }
    startApply(std::move(actionable));
}

void SyncController::applyOne(int index)
{
    if (m_busy) {
        return;
    }
    const auto &plans = m_model.plans();
    if (index < 0 || static_cast<size_t>(index) >= plans.size()) {
        return;
    }
    const SyncPlan &plan = plans[static_cast<size_t>(index)];
    if (plan.direction == SyncPlan::Direction::None) {
        return;
    }
    startApply({plan});
}

void SyncController::startApply(std::vector<SyncPlan> plans)
{
    setErrorMessage({});
    setStatusMessage({});
    setScanProgress(0, 0);
    setBusy(true);
    setWriting(true);
    m_lastBackups.clear();
    emit canUndoChanged();

    m_writeWatcher.setFuture(QtConcurrent::run(runApplyTask, m_rekordboxPath, m_enginePath, std::move(plans), makeReporter()));
}

// Common completion path for apply()/applyOne()/undoLastOperation().
// All three just differ in which background task fed the watcher.
void SyncController::onWriteFinished()
{
    SyncWriteResult result = m_writeWatcher.result();
    m_lastBackups = std::move(result.backups);
    emit canUndoChanged();

    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
    } else {
        setStatusMessage(result.statusMessage);
    }
    setBusy(false);
    setWriting(false);

    // A write may have touched any/all of the three catalogs -- rather
    // than plumbing through exactly which ones a given apply actually
    // wrote to, just invalidate all three; an unnecessary re-scan on an
    // untouched catalog is cheap next to the cost of showing stale data.
    auto &catalogCache = LibraryCatalogCache::instance();
    catalogCache.invalidate("rekordbox", m_rekordboxPath.toStdString());
    catalogCache.invalidate("engine", m_enginePath.toStdString());
    catalogCache.invalidate("onelibrary", m_rekordboxPath.toStdString());

    // Re-analyze so the model reflects the now-consistent state, staying
    // scoped to whatever playlist was selected rather than reverting to
    // "All tracks".
    analyze(m_rekordboxPath, m_enginePath, m_currentPlaylistName);
}

void SyncController::undoLastOperation()
{
    if (m_busy || m_lastBackups.empty()) {
        return;
    }
    setErrorMessage({});
    setStatusMessage({});
    setScanProgress(0, 0);
    setBusy(true);
    setWriting(true);

    // m_lastBackups stays intact (canUndo stays true) until the task
    // finishes and onWriteFinished() replaces it with the (empty) result,
    // same visible behavior as the old synchronous version, which only
    // cleared it after the restore loop completed.
    m_writeWatcher.setFuture(QtConcurrent::run(runUndoTask, m_lastBackups, makeReporter()));
}

void SyncController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void SyncController::setWriting(bool writing)
{
    if (m_writing == writing) {
        return;
    }
    m_writing = writing;
    emit writingChanged();
}

void SyncController::setScanProgress(int current, int total)
{
    if (m_scanCurrent == current && m_scanTotal == total) {
        return;
    }
    m_scanCurrent = current;
    m_scanTotal = total;
    emit scanProgressChanged();
}

void SyncController::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

void SyncController::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) {
        return;
    }
    m_statusMessage = message;
    emit statusMessageChanged();
}

}  // namespace seabass::gui
