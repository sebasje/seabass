#include "local_cue_controller.hpp"

#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <set>

#include "application/ports/backup_store.hpp"
#include "application/ports/cue_writer.hpp"
#include "application/ports/operation_log.hpp"
#include "application/use_cases/scan_library.hpp"
#include "domain/track_matching.hpp"
#include "gui/qt_progress_reporter.hpp"
#include "gui/write_guard.hpp"
#include "infrastructure/backup/filesystem_backup_store.hpp"
#include "infrastructure/backup/stick_write_lock.hpp"
#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/engine/libdjinterop_engine_reader.hpp"
#include "infrastructure/local/local_cue_store.hpp"
#include "infrastructure/logging/file_operation_log.hpp"
#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"
#include "infrastructure/rekordbox/pdb_lookup.hpp"
#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"

namespace djconvert::gui
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

std::vector<domain::Track> scanStick(const QString &format, const QString &path,
                                      std::shared_ptr<QtProgressReporter> reporter)
{
    if (format == "rekordbox") {
        infrastructure::rekordbox::KaitaiRekordboxReader reader(path.toStdString());
        reader.setProgressReporter(*reporter);
        return application::ScanLibrary(reader).execute();
    }
    infrastructure::engine::LibdjinteropEngineReader reader(path.toStdString());
    reader.setProgressReporter(*reporter);
    return application::ScanLibrary(reader).execute();
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

// Runs entirely on a background thread -- no access to the controller.
LocalCueTaskResult runBackupTask(QString format, QString path, QString stickLabel, QString description,
                                  std::shared_ptr<QtProgressReporter> reporter)
{
    LocalCueTaskResult result;
    try {
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
        result.tracksAffected = withCues;
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
        infrastructure::backup::StickWriteLock lock((stickRoot / ".djconvert-backups" / ".write.lock").string());
        infrastructure::backup::FilesystemBackupStore backupStore((stickRoot / ".djconvert-backups").string());
        infrastructure::logging::FileOperationLog log((stickRoot / ".djconvert.log").string());

        std::unique_ptr<application::CueWriter> writer;
        std::set<std::string> backedUpFiles;
        if (format == "rekordbox") {
            writer = std::make_unique<infrastructure::rekordbox::RekordboxCueWriter>(path.toStdString());
            // rekordbox stores cues per-track (ANLZ files) -- back up each
            // track's own file as it's written, same as Sync's rekordbox
            // path.
        } else {
            writer = std::make_unique<infrastructure::engine::LibdjinteropEngineCueWriter>(path.toStdString());
            std::string engineDbFile = (fs::path(path.toStdString()) / "Database2" / "m.db").string();
            auto record = backupStore.backup({engineDbFile}, "local-restore");
            log.record("local-restore: backed up before restoring cues from local backup -> " + record.path);
            result.backups.push_back({QString::fromStdString(fs::path(record.path).parent_path().string()),
                                       QString::fromStdString(record.id)});
        }

        reporter->start("Merging cues", candidates.size());
        size_t done = 0;
        int cuesWritten = 0;
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
            reporter->tick(++done);
        }
        reporter->finish();

        result.statusMessage =
            QString("Merged cues onto %1 track(s): %2 new cue(s) added").arg(candidates.size()).arg(cuesWritten);
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

void LocalCueController::backupToComputer(const QString &format, const QString &path, const QString &stickLabel,
                                           const QString &description)
{
    if (m_busy) {
        return;
    }
    setErrorMessage({});
    setStatusMessage({});
    setScanProgress(0, 0);
    setBusy(true);

    m_backupWatcher.setFuture(
        QtConcurrent::run(runBackupTask, format, path, stickLabel, description, makeReporter()));
}

void LocalCueController::onBackupFinished()
{
    LocalCueTaskResult result = m_backupWatcher.result();

    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
    } else {
        setStatusMessage(QString("Backed up cues for %1 track(s) to this computer").arg(result.tracksAffected));
    }
    setBusy(false);
}

void LocalCueController::analyzeRestore(const QString &format, const QString &path)
{
    if (m_busy) {
        return;
    }
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
        return;
    }
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
    }
}

bool LocalCueController::deleteSnapshot(qint64 id)
{
    try {
        infrastructure::local::LocalCueStore store;
        return store.deleteSnapshot(id);
    } catch (const std::exception &e) {
        setErrorMessage(QString::fromStdString(e.what()));
        return false;
    }
}

void LocalCueController::onAnalyzeFinished()
{
    LocalCueTaskResult result = m_analyzeWatcher.result();

    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
        setBusy(false);
        return;
    }

    m_stickTrackCount = result.stickTrackCount;
    m_localTrackCount = result.localTrackCount;
    m_model.setCandidates(std::move(result.candidates));
    emit analysisChanged();
    setBusy(false);
}

// Phase 2 (the confirmation gate) + phase 3 (apply): backs up the stick's
// data file, then writes each candidate's cues -- see runApplyRestoreTask()
// for the actual backup/write logic, run on a background thread.
void LocalCueController::applyRestore()
{
    if (m_busy) {
        return;
    }
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
    } else {
        setStatusMessage(result.statusMessage);
    }
    setBusy(false);
    setWriting(false);

    analyzeRestore(m_format, m_path);
}

void LocalCueController::undoLastOperation()
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

}  // namespace djconvert::gui
