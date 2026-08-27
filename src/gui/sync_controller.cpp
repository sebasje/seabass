#include "sync_controller.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include <chrono>
#include <filesystem>
#include <set>

#include "application/ports/backup_store.hpp"
#include "application/use_cases/scan_library.hpp"
#include "application/use_cases/sync_libraries.hpp"
#include "gui/qt_progress_reporter.hpp"
#include "infrastructure/backup/filesystem_backup_store.hpp"
#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/engine/libdjinterop_engine_reader.hpp"
#include "infrastructure/engine/libdjinterop_waveform_reader.hpp"
#include "infrastructure/logging/file_operation_log.hpp"
#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"
#include "infrastructure/rekordbox/pdb_lookup.hpp"
#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"
#include "infrastructure/rekordbox/rekordbox_waveform_reader.hpp"

namespace djconvert::gui
{

namespace fs = std::filesystem;
using domain::SyncPlan;

namespace
{

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
        result += QString(", %1 memory (not written -- Engine writer only handles hot cues)").arg(memory);
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

QVariantMap trackToMap(const domain::Track &track, const QString &side,
                        const std::unordered_map<std::string, std::vector<double>> &waveformsByKey)
{
    QVariantMap m;
    m["side"] = side;
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
        cueMap["color"] = QString::fromStdString(c.color);
        cues << cueMap;
    }
    m["cues"] = cues;

    std::string key = (side == QStringLiteral("rekordbox") ? "rb:" : "en:") + track.sourceId;
    QVariantList waveform;
    auto it = waveformsByKey.find(key);
    if (it != waveformsByKey.end()) {
        for (double v : it->second) {
            waveform << v;
        }
    }
    m["waveform"] = waveform;
    return m;
}

}  // namespace

QVariant SyncPlanListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= m_plans.size()) {
        return {};
    }
    const auto &plan = m_plans[static_cast<size_t>(index.row())];
    bool toEngine = plan.direction == SyncPlan::Direction::ToEngine;
    switch (role) {
    case DirectionRole:
        return toEngine ? QStringLiteral("toEngine") : QStringLiteral("toRekordbox");
    case FilenameRole:
        return QString::fromStdString(toEngine ? plan.match.rekordboxTrack.filename
                                                 : plan.match.engineTrack.filename);
    case DescriptionRole:
        return describeCues(plan.cuesToApply);
    case ConflictRole:
        return plan.kind == SyncPlan::Kind::Conflict;
    case TracksRole:
        return QVariantList{trackToMap(plan.match.rekordboxTrack, QStringLiteral("rekordbox"), m_waveformsByKey),
                             trackToMap(plan.match.engineTrack, QStringLiteral("engine"), m_waveformsByKey)};
    default:
        return {};
    }
}

QHash<int, QByteArray> SyncPlanListModel::roleNames() const
{
    return {
        {DirectionRole, "direction"},
        {FilenameRole, "filename"},
        {DescriptionRole, "description"},
        {ConflictRole, "conflict"},
        {TracksRole, "tracks"},
    };
}

void SyncPlanListModel::setPlans(std::vector<domain::SyncPlan> plans,
                                  std::unordered_map<std::string, std::vector<double>> waveformsByKey)
{
    beginResetModel();
    m_plans = std::move(plans);
    m_waveformsByKey = std::move(waveformsByKey);
    endResetModel();
}

namespace
{

std::chrono::system_clock::time_point fileMtime(const std::string &path)
{
    return std::chrono::clock_cast<std::chrono::system_clock>(std::filesystem::last_write_time(path));
}

// Runs entirely on a background thread (see SyncController::analyze()) --
// no access to the controller itself.
SyncTaskResult runAnalyzeTask(QString rekordboxPath, QString enginePath, std::shared_ptr<QtProgressReporter> reporter)
{
    SyncTaskResult result;
    try {
        infrastructure::rekordbox::KaitaiRekordboxReader rbReader(rekordboxPath.toStdString());
        rbReader.setProgressReporter(*reporter);
        auto rekordboxTracks = application::ScanLibrary(rbReader).execute();

        infrastructure::engine::LibdjinteropEngineReader engineReader(enginePath.toStdString());
        engineReader.setProgressReporter(*reporter);
        auto engineTracks = application::ScanLibrary(engineReader).execute();

        std::string rekordboxDbFile = (fs::path(rekordboxPath.toStdString()) / "rekordbox" / "export.pdb").string();
        std::string engineDbFile = (fs::path(enginePath.toStdString()) / "Database2" / "m.db").string();
        auto rekordboxMtime = fileMtime(rekordboxDbFile);
        auto engineMtime = fileMtime(engineDbFile);

        auto allPlans = application::SyncLibraries().execute(rekordboxTracks, engineTracks, rekordboxMtime,
                                                               engineMtime);
        std::vector<SyncPlan> actionable;
        for (auto &plan : allPlans) {
            if (plan.direction != SyncPlan::Direction::None) {
                actionable.push_back(std::move(plan));
            }
        }

        // Waveforms need their own file I/O per track, so only decode them
        // for tracks actually being displayed here -- a second phase after
        // the main scan above already reported 100%, so it gets its own
        // start()/tick() run rather than leave the bar looking stalled.
        reporter->start("Loading waveforms", actionable.size() * 2);
        size_t waveformsProcessed = 0;
        std::unordered_map<std::string, std::vector<double>> waveformsByKey;
        for (const auto &plan : actionable) {
            std::string rbKey = "rb:" + plan.match.rekordboxTrack.sourceId;
            if (!waveformsByKey.contains(rbKey)) {
                waveformsByKey[rbKey] = infrastructure::rekordbox::readWaveformPreview(
                    rekordboxPath.toStdString(), plan.match.rekordboxTrack.sourceId);
            }
            reporter->tick(++waveformsProcessed);

            std::string enKey = "en:" + plan.match.engineTrack.sourceId;
            if (!waveformsByKey.contains(enKey)) {
                waveformsByKey[enKey] = infrastructure::engine::readWaveformPreview(
                    enginePath.toStdString(), plan.match.engineTrack.sourceId);
            }
            reporter->tick(++waveformsProcessed);
        }
        reporter->finish();

        result.plans = std::move(actionable);
        result.waveformsByKey = std::move(waveformsByKey);
        result.rekordboxTrackCount = static_cast<int>(rekordboxTracks.size());
        result.engineTrackCount = static_cast<int>(engineTracks.size());
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

}  // namespace

SyncController::SyncController(QObject *parent) : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<SyncTaskResult>::finished, this, &SyncController::onAnalyzeFinished);
}

void SyncController::analyze(const QString &rekordboxPath, const QString &enginePath)
{
    if (m_busy) {
        return;  // never overlap two analyses
    }
    m_rekordboxPath = rekordboxPath;
    m_enginePath = enginePath;
    setErrorMessage({});
    setStatusMessage({});
    setScanProgress(0, 0);
    setBusy(true);

    // See ScanController::scan() for why the reporter is owned by the task
    // (via shared_ptr) rather than by this controller.
    auto reporter = std::make_shared<QtProgressReporter>();
    connect(reporter.get(), &QtProgressReporter::started, this,
            [this](const QString &, int total) { setScanProgress(0, total); });
    connect(reporter.get(), &QtProgressReporter::progressed, this,
            [this](int current) { setScanProgress(current, m_scanTotal); });

    m_watcher.setFuture(QtConcurrent::run(runAnalyzeTask, rekordboxPath, enginePath, reporter));
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
    m_model.setPlans(std::move(result.plans), std::move(result.waveformsByKey));
    recomputeDirectionCounts();
    setBusy(false);
}

void SyncController::recomputeDirectionCounts()
{
    int toEngine = 0;
    int toRekordbox = 0;
    for (const auto &plan : m_model.plans()) {
        (plan.direction == SyncPlan::Direction::ToEngine ? toEngine : toRekordbox)++;
    }
    m_toEngineCount = toEngine;
    m_toRekordboxCount = toRekordbox;
    emit analysisChanged();
}

// Phase 2 (the confirmation gate) + phase 3 (apply). Ports
// cli/main.cpp's runSyncCommand phase-3 block verbatim: separate
// FilesystemBackupStore/FileOperationLog per side, a single Engine m.db
// backup, per-file-deduped rekordbox .EXT backups.
void SyncController::apply()
{
    if (m_busy) {
        return;
    }
    setErrorMessage({});
    setStatusMessage({});
    setBusy(true);

    const auto &plans = m_model.plans();
    std::vector<const SyncPlan *> toEngine;
    std::vector<const SyncPlan *> toRekordbox;
    for (const auto &plan : plans) {
        if (plan.direction == SyncPlan::Direction::ToEngine) {
            toEngine.push_back(&plan);
        } else if (plan.direction == SyncPlan::Direction::ToRekordbox) {
            toRekordbox.push_back(&plan);
        }
    }

    try {
        std::string rekordboxDbFile =
            (fs::path(m_rekordboxPath.toStdString()) / "rekordbox" / "export.pdb").string();
        std::string engineDbFile = (fs::path(m_enginePath.toStdString()) / "Database2" / "m.db").string();

        infrastructure::backup::FilesystemBackupStore engineBackupStore(
            (fs::path(m_enginePath.toStdString()).parent_path() / ".djconvert-backups").string());
        infrastructure::logging::FileOperationLog engineLog(
            (fs::path(m_enginePath.toStdString()).parent_path() / ".djconvert.log").string());
        infrastructure::backup::FilesystemBackupStore rekordboxBackupStore(
            (fs::path(m_rekordboxPath.toStdString()).parent_path() / ".djconvert-backups").string());
        infrastructure::logging::FileOperationLog rekordboxLog(
            (fs::path(m_rekordboxPath.toStdString()).parent_path() / ".djconvert.log").string());

        size_t engineCuesCopied = 0;
        if (!toEngine.empty()) {
            infrastructure::engine::LibdjinteropEngineCueWriter writer(m_enginePath.toStdString());
            auto record = engineBackupStore.backup({engineDbFile}, "sync");
            engineLog.record("sync: backed up before cross-format sync -> " + record.path);

            for (const auto *plan : toEngine) {
                writer.writeHotCues(plan->match.engineTrack.sourceId, plan->cuesToApply);
                engineCuesCopied += plan->cuesToApply.size();
                engineLog.record("sync: copied cues (" + describeCues(plan->cuesToApply).toStdString() +
                                  ") from rekordbox track \"" + plan->match.rekordboxTrack.title +
                                  "\" (id=" + plan->match.rekordboxTrack.sourceId +
                                  ") to engine track id=" + plan->match.engineTrack.sourceId);
            }
        }

        size_t rekordboxCuesCopied = 0;
        if (!toRekordbox.empty()) {
            infrastructure::rekordbox::RekordboxCueWriter writer(m_rekordboxPath.toStdString());
            std::string pioneerRoot = m_rekordboxPath.toStdString();
            std::set<std::string> backedUpFiles;

            for (const auto *plan : toRekordbox) {
                auto analyzePath = infrastructure::rekordbox::findAnlzPathForTrackId(
                    pioneerRoot, static_cast<uint32_t>(std::stoul(plan->match.rekordboxTrack.sourceId)));
                if (analyzePath) {
                    std::string extPath = infrastructure::rekordbox::extAnlzPath(pioneerRoot, *analyzePath);
                    if (backedUpFiles.insert(extPath).second) {
                        auto record = rekordboxBackupStore.backup({extPath}, "sync");
                        rekordboxLog.record("sync: backed up before cross-format sync -> " + record.path);
                    }
                }

                writer.writeHotCues(plan->match.rekordboxTrack.sourceId, plan->cuesToApply);
                rekordboxCuesCopied += plan->cuesToApply.size();
                rekordboxLog.record("sync: copied cues (" + describeCues(plan->cuesToApply).toStdString() +
                                     ") from engine track \"" + plan->match.engineTrack.title +
                                     "\" (id=" + plan->match.engineTrack.sourceId +
                                     ") to rekordbox track id=" + plan->match.rekordboxTrack.sourceId);
            }
        }

        QStringList parts;
        if (!toEngine.empty()) {
            parts << QString("%1 track(s) to Engine (%2 cue(s))").arg(toEngine.size()).arg(engineCuesCopied);
        }
        if (!toRekordbox.empty()) {
            parts << QString("%1 track(s) to rekordbox (%2 cue(s))").arg(toRekordbox.size()).arg(rekordboxCuesCopied);
        }
        setStatusMessage("Synced " + parts.join(", "));
    } catch (const std::exception &e) {
        setErrorMessage(QString::fromStdString(e.what()));
        setBusy(false);
        return;
    }
    setBusy(false);

    // Re-analyze so the model reflects the now-consistent state (mirrors
    // DuplicatesController::applyOne re-running rescan() after a write).
    analyze(m_rekordboxPath, m_enginePath);
}

void SyncController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
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

}  // namespace djconvert::gui
