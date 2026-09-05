#include "local_cue_controller.hpp"

#include <QStringList>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <set>
#include <unordered_map>

#include "application/ports/backup_store.hpp"
#include "application/ports/cue_writer.hpp"
#include "application/ports/operation_log.hpp"
#include "domain/track_matching.hpp"
#include "gui/library_catalog_cache.hpp"
#include "gui/onelibrary_cue_writer_adapter.hpp"
#include "gui/qt_progress_reporter.hpp"
#include "gui/write_guard.hpp"
#include "infrastructure/backup/filesystem_backup_store.hpp"
#include "infrastructure/backup/stick_write_lock.hpp"
#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/local/local_cue_store.hpp"
#include "infrastructure/logging/file_operation_log.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/rekordbox/pdb_lookup.hpp"
#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"

namespace seabass::gui
{

namespace fs = std::filesystem;
using domain::RestoreCandidate;

namespace
{

QString describeCues(const std::vector<domain::CuePoint> &cues)
{
    int hot = 0;
    int memory = 0;
    for (const auto &cue : cues) {
        (cue.kind == domain::CuePoint::Kind::Hot ? hot : memory)++;
    }
    QString result = QString("%1 hot").arg(hot);
    if (memory > 0) {
        result += QString(", %1 memory").arg(memory);
    }
    return result;
}

// path is the PIONEER root for both "rekordbox" and "onelibrary" --
// exportLibrary.db lives alongside export.pdb (see LocalCuePage.qml's
// currentPath()), same convention LibraryCatalogCache's own callers
// elsewhere already use.
std::vector<domain::Track> scanStick(const QString &format, const QString &path,
                                      std::shared_ptr<QtProgressReporter> reporter)
{
    return LibraryCatalogCache::instance().tracksFor(format.toStdString(), path.toStdString(), *reporter);
}

}  // namespace

RestoreCandidateListModel::RestoreCandidateListModel(QObject *parent) : QAbstractListModel(parent) {}

int RestoreCandidateListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_candidates.size());
}

QVariant RestoreCandidateListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= m_candidates.size()) {
        return {};
    }
    const auto &candidate = m_candidates[static_cast<size_t>(index.row())];
    switch (role) {
    case FilenameRole:
        return QString::fromStdString(candidate.stickTrack.filename);
    case TitleRole:
        return QString::fromStdString(candidate.stickTrack.title);
    case ArtistRole:
        return QString::fromStdString(candidate.stickTrack.artist);
    case DescriptionRole: {
        // mergeCues() appends additions after the stick's own (unchanged)
        // cues, in order -- so everything past that boundary is exactly
        // what this candidate would add.
        auto existingCount = candidate.stickTrack.cues.size();
        std::vector<domain::CuePoint> added(candidate.mergedCues.begin() +
                                                 static_cast<std::ptrdiff_t>(existingCount),
                                             candidate.mergedCues.end());
        return describeCues(added);
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> RestoreCandidateListModel::roleNames() const
{
    return {
        {FilenameRole, "filename"},
        {TitleRole, "title"},
        {ArtistRole, "artist"},
        {DescriptionRole, "description"},
    };
}

void RestoreCandidateListModel::setCandidates(std::vector<domain::RestoreCandidate> candidates)
{
    beginResetModel();
    m_candidates = std::move(candidates);
    endResetModel();
}

namespace
{

// Backs up one format's side of the stick into the local store -- shared
// by both branches of runBackupTask() below.
int backupOneFormat(const QString &format, const QString &path, const QString &stickLabel,
                     const QString &description, std::shared_ptr<QtProgressReporter> reporter)
{
    auto tracks = scanStick(format, path, reporter);
    int withCues = 0;
    for (const auto &track : tracks) {
        if (!track.cues.empty()) {
            withCues++;
        }
    }

    infrastructure::local::LocalCueStore store;
    store.upsert(tracks, format.toStdString(), stickLabel.toStdString());
    store.createSnapshot(tracks, format.toStdString(), stickLabel.toStdString(), description.toStdString());
    return withCues;
}

// Runs entirely on a background thread -- no access to the controller.
// Backs up whichever of rekordboxPath/enginePath/oneLibraryPath is
// non-empty, so a stick with more than one format gets all of them
// backed up from a single "Backup Now" click rather than requiring the
// user to switch formats and click once per format. oneLibraryPath is
// only ever rekordboxPath's own PIONEER root re-passed (exportLibrary.db
// lives alongside export.pdb there) -- passed as a separate, possibly-
// empty argument rather than derived here so the caller's own
// OneLibraryCueWriter::existsFor() check (needs a real file check, not
// just "is rekordboxPath set") decides whether it's actually present.
LocalCueTaskResult runBackupTask(QString stickLabel, QString description, QString rekordboxPath, QString enginePath,
                                  QString oneLibraryPath, std::shared_ptr<QtProgressReporter> reporter)
{
    LocalCueTaskResult result;
    try {
        if (!rekordboxPath.isEmpty()) {
            result.tracksAffectedRekordbox = backupOneFormat("rekordbox", rekordboxPath, stickLabel, description, reporter);
        }
        if (!enginePath.isEmpty()) {
            result.tracksAffectedEngine = backupOneFormat("engine", enginePath, stickLabel, description, reporter);
        }
        if (!oneLibraryPath.isEmpty()) {
            result.tracksAffectedOneLibrary =
                backupOneFormat("onelibrary", oneLibraryPath, stickLabel, description, reporter);
        }
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

LocalCueTaskResult runAnalyzeRestoreTask(QString format, QString path, std::shared_ptr<QtProgressReporter> reporter)
{
    LocalCueTaskResult result;
    try {
        auto stickTracks = scanStick(format, path, reporter);

        infrastructure::local::LocalCueStore store;
        auto localTracks = store.readAll();

        auto matches = domain::matchTracks(stickTracks, localTracks);
        result.candidates = domain::LocalRestorePlanner::plan(matches);
        result.stickTrackCount = static_cast<int>(stickTracks.size());
        result.localTrackCount = static_cast<int>(localTracks.size());
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

LocalCueTaskResult runAnalyzeSnapshotRestoreTask(qint64 snapshotId, QString format, QString path,
                                                  std::shared_ptr<QtProgressReporter> reporter)
{
    LocalCueTaskResult result;
    try {
        auto stickTracks = scanStick(format, path, reporter);

        infrastructure::local::LocalCueStore store;
        auto snapshotTracks = store.readSnapshot(snapshotId);

        auto matches = domain::matchTracks(stickTracks, snapshotTracks);
        result.candidates = domain::LocalRestorePlanner::plan(matches);
        result.stickTrackCount = static_cast<int>(stickTracks.size());
        result.localTrackCount = static_cast<int>(snapshotTracks.size());
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

// Runs entirely on a background thread (see LocalCueController::
// applyRestore()) -- mirrors SyncController::runApplyTask()'s per-format
// backup/write wiring, scoped to the merge direction (never touches the
// local database, only the stick).
LocalCueWriteResult runApplyRestoreTask(QString format, QString path, std::vector<RestoreCandidate> candidates,
                                         std::shared_ptr<QtProgressReporter> reporter)
{
    LocalCueWriteResult result;
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

        std::unique_ptr<application::CueWriter> writer;
        std::set<std::string> backedUpFiles;
        bool writeToOneLibrary = false;
        if (format == "rekordbox") {
            writer = std::make_unique<infrastructure::rekordbox::RekordboxCueWriter>(path.toStdString());
            // rekordbox stores cues per-track (ANLZ files) -- back up each
            // track's own file as it's written, same as Sync's rekordbox
            // path.
            //
            // Best-effort secondary write target, alongside the primary
            // export.pdb write below -- see OneLibraryCueWriter's class
            // comment and docs/onelibrary-format.md. Back it up once
            // upfront (same pattern as Engine's m.db below), rather than
            // per-candidate, since it's one shared file.
            writeToOneLibrary = infrastructure::onelibrary::OneLibraryCueWriter::existsFor(path.toStdString());
            if (writeToOneLibrary) {
                const std::string oneLibDbPath =
                    infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(path.toStdString());
                if (backedUpFiles.insert(oneLibDbPath).second) {
                    const auto record = backupStore.backup({oneLibDbPath}, "local-restore");
                    log.record("local-restore: backed up before restoring cues from local backup -> " + record.path);
                    result.backups.push_back({QString::fromStdString(fs::path(record.path).parent_path().string()),
                                               QString::fromStdString(record.id)});
                }
            }
        } else if (format == "engine") {
            writer = std::make_unique<infrastructure::engine::LibdjinteropEngineCueWriter>(path.toStdString());
            const std::string engineDbFile = (fs::path(path.toStdString()) / "Database2" / "m.db").string();
            const auto record = backupStore.backup({engineDbFile}, "local-restore");
            log.record("local-restore: backed up before restoring cues from local backup -> " + record.path);
            result.backups.push_back({QString::fromStdString(fs::path(record.path).parent_path().string()),
                                       QString::fromStdString(record.id)});
        } else {
            // onelibrary: OneLibraryCueWriter identifies a track by file
            // path, not sourceId (see its own class comment -- content_id
            // is a separate id space), so the adapter below builds the
            // sourceId->filePath map every other candidate loop here
            // already has on hand, letting the loop below stay identical
            // for all three formats.
            std::unordered_map<std::string, std::string> sourceIdToPath;
            for (const auto &candidate : candidates) {
                sourceIdToPath[candidate.stickTrack.sourceId] = candidate.stickTrack.filePath;
            }
            writer = std::make_unique<OneLibraryCueWriterAdapter>(path.toStdString(), std::move(sourceIdToPath));
            std::string oneLibDbPath = infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(path.toStdString());
            auto record = backupStore.backup({oneLibDbPath}, "local-restore");
            log.record("local-restore: backed up before restoring cues from local backup -> " + record.path);
            result.backups.push_back({QString::fromStdString(fs::path(record.path).parent_path().string()),
                                       QString::fromStdString(record.id)});
        }

        reporter->start("Merging cues", candidates.size());
        size_t done = 0;
        int cuesWritten = 0;
        QStringList oneLibraryWarnings;
        std::unique_ptr<infrastructure::onelibrary::OneLibraryCueWriter> oneLibWriter;
        if (writeToOneLibrary) {
            try {
                oneLibWriter = std::make_unique<infrastructure::onelibrary::OneLibraryCueWriter>(path.toStdString());
            } catch (const std::exception &e) {
                oneLibraryWarnings << QString("could not open OneLibrary: %1").arg(QString::fromStdString(e.what()));
                oneLibWriter.reset();
            }
        }
        for (const auto &candidate : candidates) {
            if (format == "rekordbox") {
                auto analyzePath = infrastructure::rekordbox::findAnlzPathForTrackId(
                    path.toStdString(), static_cast<uint32_t>(std::stoul(candidate.stickTrack.sourceId)));
                if (analyzePath) {
                    std::string extPath = infrastructure::rekordbox::extAnlzPath(path.toStdString(), *analyzePath);
                    if (backedUpFiles.insert(extPath).second) {
                        auto record = backupStore.backup({extPath}, "local-restore");
                        log.record("local-restore: backed up before restoring cues from local backup -> " +
                                   record.path);
                        result.backups.push_back({QString::fromStdString(fs::path(record.path).parent_path().string()),
                                                   QString::fromStdString(record.id)});
                    }
                }
            }
            // mergedCues is the *complete* cue list to end up with -- the
            // stick's own cues, untouched, plus whichever of the backup's
            // cues filled a gap. writeHotCues() replaces the whole set, so
            // passing anything less would silently drop what's already
            // there.
            writer->writeHotCues(candidate.stickTrack.sourceId, candidate.mergedCues);
            int added = static_cast<int>(candidate.mergedCues.size() - candidate.stickTrack.cues.size());
            cuesWritten += added;
            log.record("local-restore: merged " + std::to_string(added) +
                       " new cue(s) onto track id=" + candidate.stickTrack.sourceId + " (\"" +
                       candidate.stickTrack.title + "\") from local backup");

            if (oneLibWriter && !candidate.stickTrack.filePath.empty()) {
                try {
                    oneLibWriter->writeCuesForPath(candidate.stickTrack.filePath, candidate.mergedCues);
                    log.record("local-restore: also wrote merged cues into OneLibrary (id=" +
                               candidate.stickTrack.sourceId + ")");
                } catch (const std::exception &e) {
                    oneLibraryWarnings << QString("\"%1\": %2")
                                              .arg(QString::fromStdString(candidate.stickTrack.title))
                                              .arg(QString::fromStdString(e.what()));
                    log.record("local-restore: OneLibrary cue write failed for \"" + candidate.stickTrack.title +
                               "\": " + e.what());
                }
            }

            reporter->tick(++done);
        }
        reporter->finish();

        result.statusMessage =
            QString("Merged cues onto %1 track(s): %2 new cue(s) added").arg(candidates.size()).arg(cuesWritten);
        if (!oneLibraryWarnings.isEmpty()) {
            result.statusMessage += QString(" (%1 OneLibrary cue write(s) failed -- the primary write above still "
                                             "succeeded and was not affected: %2)")
                                         .arg(oneLibraryWarnings.size())
                                         .arg(oneLibraryWarnings.join("; "));
        }
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

// Runs entirely on a background thread (see LocalCueController::
// undoLastOperation()). An empty `backups` result always means "nothing
// left to undo" -- the caller's undo trail is stale either way.
LocalCueWriteResult runUndoTask(std::vector<UndoableBackup> backups, std::shared_ptr<QtProgressReporter> reporter)
{
    LocalCueWriteResult result;
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
        std::sort(dirs.begin(), dirs.end());
        dirs.erase(std::unique(dirs.begin(), dirs.end()), dirs.end());
        std::vector<std::unique_ptr<infrastructure::backup::StickWriteLock>> locks;
        for (const auto &dir : dirs) {
            locks.push_back(std::make_unique<infrastructure::backup::StickWriteLock>(dir + "/.write.lock"));
        }

        reporter->start("Undoing", backups.size());
        for (auto it = backups.rbegin(); it != backups.rend(); ++it) {
            infrastructure::backup::FilesystemBackupStore store(it->backupDir.toStdString());
            if (store.restore(it->id.toStdString())) {
                restored++;
            }
            reporter->tick(static_cast<size_t>(restored));
        }
        reporter->finish();
        result.statusMessage = QString("Undone -- restored %1 file(s) to their state before the last merge").arg(restored);
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

}  // namespace

LocalCueController::LocalCueController(QObject *parent) : QObject(parent)
{
    connect(&m_backupWatcher, &QFutureWatcher<LocalCueTaskResult>::finished, this,
            &LocalCueController::onBackupFinished);
    connect(&m_analyzeWatcher, &QFutureWatcher<LocalCueTaskResult>::finished, this,
            &LocalCueController::onAnalyzeFinished);
    connect(&m_writeWatcher, &QFutureWatcher<LocalCueWriteResult>::finished, this,
            &LocalCueController::onWriteFinished);
}

void LocalCueController::backupToComputer(const QString &stickLabel, const QString &description,
                                           const QString &rekordboxPath, const QString &enginePath,
                                           const QString &oneLibraryPath)
{
    if (m_busy) {
        emit actionFeedback("Still busy with another operation on this stick -- try again once it finishes.", true);
        return;
    }
    setErrorMessage({});
    setStatusMessage({});
    setScanProgress(0, 0);
    setBusy(true);

    // oneLibraryPath is rekordboxPath itself, re-passed only when the
    // caller has confirmed exportLibrary.db actually exists there (see
    // OneLibraryCueWriter::existsFor()) -- runBackupTask() only backs it
    // up when this is non-empty.
    m_backupWatcher.setFuture(QtConcurrent::run(runBackupTask, stickLabel, description, rekordboxPath, enginePath,
                                                 oneLibraryPath, makeReporter()));
}

void LocalCueController::onBackupFinished()
{
    LocalCueTaskResult result = m_backupWatcher.result();

    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
        emit actionFeedback(result.errorMessage, true);
    } else {
        QStringList parts;
        if (result.tracksAffectedRekordbox >= 0) {
            parts << QString("%1 track(s) (Rekordbox)").arg(result.tracksAffectedRekordbox);
        }
        if (result.tracksAffectedEngine >= 0) {
            parts << QString("%1 track(s) (Engine)").arg(result.tracksAffectedEngine);
        }
        if (result.tracksAffectedOneLibrary >= 0) {
            parts << QString("%1 track(s) (OneLibrary)").arg(result.tracksAffectedOneLibrary);
        }
        QString message = QString("Backed up cues to this computer: %1").arg(parts.join(", "));
        setStatusMessage(message);
        emit actionFeedback(message, false);
    }
    setBusy(false);
}

void LocalCueController::analyzeRestore(const QString &format, const QString &path, bool reportFeedback)
{
    if (m_busy) {
        if (reportFeedback) {
            emit actionFeedback("Still busy with another operation on this stick -- try again once it finishes.",
                                 true);
        }
        return;
    }
    m_analyzeReportsFeedback = reportFeedback;
    m_format = format;
    m_path = path;
    setErrorMessage({});
    setStatusMessage({});
    setScanProgress(0, 0);
    setBusy(true);

    m_analyzeWatcher.setFuture(QtConcurrent::run(runAnalyzeRestoreTask, format, path, makeReporter()));
}

void LocalCueController::analyzeSnapshotRestore(qint64 snapshotId, const QString &format, const QString &path)
{
    if (m_busy) {
        emit actionFeedback("Still busy with another operation on this stick -- try again once it finishes.", true);
        return;
    }
    m_analyzeReportsFeedback = true;
    m_format = format;
    m_path = path;
    setErrorMessage({});
    setStatusMessage({});
    setScanProgress(0, 0);
    setBusy(true);

    m_analyzeWatcher.setFuture(
        QtConcurrent::run(runAnalyzeSnapshotRestoreTask, snapshotId, format, path, makeReporter()));
}

// See ScanController::scan() for why the reporter is owned by the task
// (via shared_ptr) rather than by this controller.
std::shared_ptr<QtProgressReporter> LocalCueController::makeReporter()
{
    auto reporter = std::make_shared<QtProgressReporter>();
    connect(reporter.get(), &QtProgressReporter::started, this,
            [this](const QString &, int total) { setScanProgress(0, total); });
    connect(reporter.get(), &QtProgressReporter::progressed, this,
            [this](int current) { setScanProgress(current, m_scanTotal); });
    return reporter;
}

QVariantList LocalCueController::listSnapshots()
{
    QVariantList result;
    try {
        infrastructure::local::LocalCueStore store;
        for (const auto &summary : store.listSnapshots()) {
            QVariantMap m;
            m["id"] = static_cast<qint64>(summary.id);
            m["createdAt"] = QString::fromStdString(summary.createdAt);
            m["stickLabel"] = QString::fromStdString(summary.stickLabel);
            m["sourceFormat"] = QString::fromStdString(summary.sourceFormat);
            m["description"] = QString::fromStdString(summary.description);
            m["trackCount"] = summary.trackCount;
            m["cueCount"] = summary.cueCount;
            m["uncompressedSizeBytes"] = static_cast<qulonglong>(summary.uncompressedSizeBytes);
            m["compressedSizeBytes"] = static_cast<qulonglong>(summary.compressedSizeBytes);
            m["schemaVersion"] = summary.schemaVersion;
            result << m;
        }
    } catch (const std::exception &e) {
        setErrorMessage(QString::fromStdString(e.what()));
        emit actionFeedback(QString::fromStdString(e.what()), true);
    }
    return result;
}

void LocalCueController::setSnapshotDescription(qint64 id, const QString &description)
{
    try {
        infrastructure::local::LocalCueStore store;
        store.setSnapshotDescription(id, description.toStdString());
    } catch (const std::exception &e) {
        setErrorMessage(QString::fromStdString(e.what()));
        emit actionFeedback(QString::fromStdString(e.what()), true);
    }
}

bool LocalCueController::deleteSnapshot(qint64 id)
{
    try {
        infrastructure::local::LocalCueStore store;
        return store.deleteSnapshot(id);
    } catch (const std::exception &e) {
        setErrorMessage(QString::fromStdString(e.what()));
        emit actionFeedback(QString::fromStdString(e.what()), true);
        return false;
    }
}

bool LocalCueController::hasOneLibrary(const QString &pioneerRoot) const
{
    return infrastructure::onelibrary::OneLibraryCueWriter::existsFor(pioneerRoot.toStdString());
}

void LocalCueController::onAnalyzeFinished()
{
    LocalCueTaskResult result = m_analyzeWatcher.result();
    bool reportFeedback = m_analyzeReportsFeedback;
    m_analyzeReportsFeedback = false;

    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
        if (reportFeedback) {
            emit actionFeedback(result.errorMessage, true);
        }
        setBusy(false);
        return;
    }

    m_stickTrackCount = result.stickTrackCount;
    m_localTrackCount = result.localTrackCount;
    int candidateCount = static_cast<int>(result.candidates.size());
    m_model.setCandidates(std::move(result.candidates));
    emit analysisChanged();
    // Otherwise the only feedback after a real, sometimes multi-second
    // scan (see BusyOverlay's "Analyzing backups...") was the restore
    // candidate list quietly changing -- easy to read as "nothing
    // happened" rather than "here's what I found", especially right
    // after clicking a specific snapshot's "Restore From Here" (whose
    // result lands in the third section down, off the part of the page
    // that button click was in). Only for a user-initiated analyze
    // (reportFeedback) -- the automatic re-scan onWriteFinished() runs
    // right after a write already gets its own actionFeedback for the
    // write itself, and a page-load/format-switch scan was never asked
    // for in the first place.
    if (reportFeedback) {
        if (candidateCount > 0) {
            emit actionFeedback(
                QString("Found %1 track(s) with cues this backup can add -- review the list below, then "
                        "\"Merge Onto %1 Track(s)\" to apply.")
                    .arg(candidateCount),
                false);
        } else {
            emit actionFeedback("Nothing to merge: either the stick already has every cue this backup offers, "
                                 "or none of its tracks match one backed up on this computer.",
                                 false);
        }
    }
    setBusy(false);
}

// Phase 2 (the confirmation gate) + phase 3 (apply): backs up the stick's
// data file, then writes each candidate's cues -- see runApplyRestoreTask()
// for the actual backup/write logic, run on a background thread.
void LocalCueController::applyRestore()
{
    if (m_busy) {
        emit actionFeedback("Still busy with another operation on this stick -- try again once it finishes.", true);
        return;
    }
    m_pendingWriteKind = PendingWriteKind::Apply;
    setErrorMessage({});
    setStatusMessage({});
    setScanProgress(0, 0);
    setBusy(true);
    setWriting(true);
    m_lastBackups.clear();
    emit canUndoChanged();

    m_writeWatcher.setFuture(
        QtConcurrent::run(runApplyRestoreTask, m_format, m_path, m_model.candidates(), makeReporter()));
}

// Common completion path for applyRestore()/undoLastOperation() -- they
// only differ in which background task fed the watcher.
void LocalCueController::onWriteFinished()
{
    LocalCueWriteResult result = m_writeWatcher.result();
    m_lastBackups = std::move(result.backups);
    emit canUndoChanged();

    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
        emit actionFeedback(result.errorMessage, true);
    } else {
        setStatusMessage(result.statusMessage);
        emit actionFeedback(result.statusMessage, false);
    }
    setBusy(false);
    setWriting(false);

    // Even the local-update branch below (which skips the immediate
    // re-scan) still needs this, so a *later* scan (reopening the page,
    // switching formats and back) can't read stale cached tracks from
    // before the write within an mtime-granularity window.
    auto &catalogCache = LibraryCatalogCache::instance();
    catalogCache.invalidate(m_format.toStdString(), m_path.toStdString());
    if (m_format == "rekordbox") {
        // runApplyRestoreTask() also best-effort writes cues into
        // OneLibrary's exportLibrary.db when one exists alongside
        // export.pdb -- keep its cache entry honest too.
        catalogCache.invalidate("onelibrary", m_path.toStdString());
    }

    if (m_pendingWriteKind == PendingWriteKind::Apply) {
        // applyRestore() always writes every candidate currently in
        // m_model, and LocalRestorePlanner::mergeCues() only ever adds
        // cues the stick is missing -- so once this write succeeds,
        // every listed candidate is already fully merged and there's
        // nothing left for this same backup to offer. No re-scan can
        // find anything new here; just clear the list locally.
        m_model.setCandidates({});
        emit analysisChanged();
    } else {
        // Undo restores the stick's prior file bytes directly; the
        // candidate list for "what's missing" was already discarded
        // locally by the apply that preceded it, so there's nothing to
        // patch back in without a real re-scan. Silent (reportFeedback
        // defaults to false): this is an automatic background refresh
        // following the write above, not something the user clicked --
        // the actionFeedback just emitted already covers what happened.
        analyzeRestore(m_format, m_path);
    }
}

void LocalCueController::undoLastOperation()
{
    if (m_busy) {
        emit actionFeedback("Still busy with another operation on this stick -- try again once it finishes.", true);
        return;
    }
    if (m_lastBackups.empty()) {
        emit actionFeedback("Nothing to undo.", true);
        return;
    }
    m_pendingWriteKind = PendingWriteKind::Undo;
    setErrorMessage({});
    setStatusMessage({});
    setScanProgress(0, 0);
    setBusy(true);
    setWriting(true);

    // m_lastBackups stays intact (canUndo stays true) until the task
    // finishes and onWriteFinished() replaces it with the (empty) result.
    m_writeWatcher.setFuture(QtConcurrent::run(runUndoTask, m_lastBackups, makeReporter()));
}

void LocalCueController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void LocalCueController::setWriting(bool writing)
{
    if (m_writing == writing) {
        return;
    }
    m_writing = writing;
    emit writingChanged();
}

void LocalCueController::setScanProgress(int current, int total)
{
    if (m_scanCurrent == current && m_scanTotal == total) {
        return;
    }
    m_scanCurrent = current;
    m_scanTotal = total;
    emit scanProgressChanged();
}

void LocalCueController::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

void LocalCueController::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) {
        return;
    }
    m_statusMessage = message;
    emit statusMessageChanged();
}

}  // namespace seabass::gui
