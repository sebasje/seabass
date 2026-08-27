#include "local_cue_controller.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include <filesystem>
#include <memory>
#include <set>

#include "application/ports/backup_store.hpp"
#include "application/ports/cue_writer.hpp"
#include "application/ports/operation_log.hpp"
#include "application/use_cases/scan_library.hpp"
#include "domain/track_matching.hpp"
#include "gui/qt_progress_reporter.hpp"
#include "infrastructure/backup/filesystem_backup_store.hpp"
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
    case DescriptionRole:
        return describeCues(candidate.localTrack.cues);
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
LocalCueTaskResult runBackupTask(QString format, QString path, QString stickLabel,
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

}  // namespace

LocalCueController::LocalCueController(QObject *parent) : QObject(parent)
{
    connect(&m_backupWatcher, &QFutureWatcher<LocalCueTaskResult>::finished, this,
            &LocalCueController::onBackupFinished);
    connect(&m_analyzeWatcher, &QFutureWatcher<LocalCueTaskResult>::finished, this,
            &LocalCueController::onAnalyzeFinished);
}

void LocalCueController::backupToComputer(const QString &format, const QString &path, const QString &stickLabel)
{
    if (m_busy) {
        return;
    }
    setErrorMessage({});
    setStatusMessage({});
    setScanProgress(0, 0);
    setBusy(true);

    auto reporter = std::make_shared<QtProgressReporter>();
    connect(reporter.get(), &QtProgressReporter::started, this,
            [this](const QString &, int total) { setScanProgress(0, total); });
    connect(reporter.get(), &QtProgressReporter::progressed, this,
            [this](int current) { setScanProgress(current, m_scanTotal); });

    m_backupWatcher.setFuture(QtConcurrent::run(runBackupTask, format, path, stickLabel, reporter));
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

    auto reporter = std::make_shared<QtProgressReporter>();
    connect(reporter.get(), &QtProgressReporter::started, this,
            [this](const QString &, int total) { setScanProgress(0, total); });
    connect(reporter.get(), &QtProgressReporter::progressed, this,
            [this](int current) { setScanProgress(current, m_scanTotal); });

    m_analyzeWatcher.setFuture(QtConcurrent::run(runAnalyzeRestoreTask, format, path, reporter));
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
// data file, then writes each candidate's cues -- mirrors
// SyncController::apply()'s per-format backup/write wiring.
void LocalCueController::applyRestore()
{
    if (m_busy) {
        return;
    }
    setErrorMessage({});
    setStatusMessage({});
    setBusy(true);

    const auto &candidates = m_model.candidates();

    try {
        fs::path stickRoot = fs::path(m_path.toStdString()).parent_path();
        infrastructure::backup::FilesystemBackupStore backupStore((stickRoot / ".djconvert-backups").string());
        infrastructure::logging::FileOperationLog log((stickRoot / ".djconvert.log").string());

        std::unique_ptr<application::CueWriter> writer;
        std::set<std::string> backedUpFiles;
        if (m_format == "rekordbox") {
            writer = std::make_unique<infrastructure::rekordbox::RekordboxCueWriter>(m_path.toStdString());
            // rekordbox stores cues per-track (ANLZ files) -- back up the
            // whole stick's rekordbox export.pdb-adjacent tree isn't
            // practical file-by-file here, so (like Sync's rekordbox
            // path) back up each track's own file as it's written.
        } else {
            writer = std::make_unique<infrastructure::engine::LibdjinteropEngineCueWriter>(m_path.toStdString());
            std::string engineDbFile = (fs::path(m_path.toStdString()) / "Database2" / "m.db").string();
            auto record = backupStore.backup({engineDbFile}, "local-restore");
            log.record("local-restore: backed up before restoring cues from local backup -> " + record.path);
        }

        int cuesWritten = 0;
        for (const auto &candidate : candidates) {
            if (m_format == "rekordbox") {
                auto analyzePath = infrastructure::rekordbox::findAnlzPathForTrackId(
                    m_path.toStdString(), static_cast<uint32_t>(std::stoul(candidate.stickTrack.sourceId)));
                if (analyzePath) {
                    std::string extPath =
                        infrastructure::rekordbox::extAnlzPath(m_path.toStdString(), *analyzePath);
                    if (backedUpFiles.insert(extPath).second) {
                        auto record = backupStore.backup({extPath}, "local-restore");
                        log.record("local-restore: backed up before restoring cues from local backup -> " +
                                   record.path);
                    }
                }
            }
            writer->writeHotCues(candidate.stickTrack.sourceId, candidate.localTrack.cues);
            cuesWritten += static_cast<int>(candidate.localTrack.cues.size());
            log.record("local-restore: restored " + std::to_string(candidate.localTrack.cues.size()) +
                       " cue(s) onto track id=" + candidate.stickTrack.sourceId + " (\"" +
                       candidate.stickTrack.title + "\") from local backup");
        }

        setStatusMessage(
            QString("Restored cues onto %1 track(s): %2 cue(s) total").arg(candidates.size()).arg(cuesWritten));
    } catch (const std::exception &e) {
        setErrorMessage(QString::fromStdString(e.what()));
        setBusy(false);
        return;
    }
    setBusy(false);

    analyzeRestore(m_format, m_path);
}

void LocalCueController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
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
