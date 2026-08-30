#include "sync_controller.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <set>

#include "application/ports/backup_store.hpp"
#include "application/use_cases/scan_library.hpp"
#include "application/use_cases/sync_libraries.hpp"
#include "gui/qt_progress_reporter.hpp"
#include "gui/write_guard.hpp"
#include "infrastructure/backup/filesystem_backup_store.hpp"
#include "infrastructure/backup/stick_write_lock.hpp"
#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/engine/libdjinterop_engine_reader.hpp"
#include "infrastructure/engine/libdjinterop_waveform_reader.hpp"
#include "infrastructure/logging/file_operation_log.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/onelibrary/onelibrary_reader.hpp"
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

QVariantMap trackToMap(const domain::Track &track,
                        const std::unordered_map<std::string, std::vector<domain::WaveformColumn>> &waveformsByKey)
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
        cueMap["color"] = QString::fromStdString(c.color);
        cues << cueMap;
    }
    m["cues"] = cues;

    // OneLibrary has no waveform reader of its own -- simply absent from
    // waveformsByKey, which renders as an empty list here, same as any
    // other best-effort-missing waveform.
    std::string key = track.format + ":" + track.sourceId;
    QVariantList waveform;
    auto it = waveformsByKey.find(key);
    if (it != waveformsByKey.end()) {
        for (const auto &col : it->second) {
            QVariantMap colMap;
            colMap["low"] = col.low;
            colMap["mid"] = col.mid;
            colMap["high"] = col.high;
            waveform << colMap;
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
        return QVariantList{trackToMap(plan.match.trackA, m_waveformsByKey), trackToMap(plan.match.trackB, m_waveformsByKey)};
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

void SyncPlanListModel::setPlans(std::vector<domain::SyncPlan> plans,
                                  std::unordered_map<std::string, std::vector<domain::WaveformColumn>> waveformsByKey)
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

// Runs entirely on a background thread (see SyncController::analyze()) -
// no access to the controller itself. Scans whichever of the three
// catalogs are present, then runs the exact same real diff+direction
// logic (domain::SyncLibraries) once per pair actually available on this
// stick, combining every pair's actionable plans into one list.
SyncTaskResult runAnalyzeTask(QString rekordboxPath, QString enginePath, std::shared_ptr<QtProgressReporter> reporter)
{
    SyncTaskResult result;
    try {
        bool hasRekordbox = !rekordboxPath.isEmpty();
        bool hasEngine = !enginePath.isEmpty();
        bool hasOneLibrary = false;

        std::vector<domain::Track> rekordboxTracks, engineTracks, oneLibraryTracks;
        std::chrono::system_clock::time_point rekordboxMtime, engineMtime, oneLibraryMtime;

        if (hasRekordbox) {
            infrastructure::rekordbox::KaitaiRekordboxReader rbReader(rekordboxPath.toStdString());
            rbReader.setProgressReporter(*reporter);
            rekordboxTracks = application::ScanLibrary(rbReader).execute();
            rekordboxMtime = fileMtime((fs::path(rekordboxPath.toStdString()) / "rekordbox" / "export.pdb").string());
            hasOneLibrary = infrastructure::onelibrary::OneLibraryCueWriter::existsFor(rekordboxPath.toStdString());
        }
        if (hasEngine) {
            infrastructure::engine::LibdjinteropEngineReader engineReader(enginePath.toStdString());
            engineReader.setProgressReporter(*reporter);
            engineTracks = application::ScanLibrary(engineReader).execute();
            // Streaming tracks (TIDAL) have no real local file. Never
            // sync cues onto/from one. See domain::Track::streamingSource's
            // own doc comment.
            engineTracks.erase(std::remove_if(engineTracks.begin(), engineTracks.end(),
                                               [](const domain::Track &t) { return !t.streamingSource.empty(); }),
                                engineTracks.end());
            engineMtime = fileMtime((fs::path(enginePath.toStdString()) / "Database2" / "m.db").string());
        }
        if (hasOneLibrary) {
            infrastructure::onelibrary::OneLibraryReader oneLibReader(rekordboxPath.toStdString());
            oneLibReader.setProgressReporter(*reporter);
            oneLibraryTracks = application::ScanLibrary(oneLibReader).execute();
            oneLibraryMtime =
                fileMtime(infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(rekordboxPath.toStdString()));
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

        // Waveforms need their own file I/O per track, so only decode them
        // for tracks actually being displayed here. A second phase after
        // the main scan above already reported 100%, so it gets its own
        // start()/tick() run rather than leave the bar looking stalled.
        reporter->start("Loading waveforms", actionable.size() * 2);
        size_t waveformsProcessed = 0;
        std::unordered_map<std::string, std::vector<domain::WaveformColumn>> waveformsByKey;
        for (const auto &plan : actionable) {
            for (const domain::Track *side : {&plan.match.trackA, &plan.match.trackB}) {
                std::string key = side->format + ":" + side->sourceId;
                if (!waveformsByKey.contains(key)) {
                    if (side->format == "rekordbox") {
                        waveformsByKey[key] =
                            infrastructure::rekordbox::readWaveformPreview(rekordboxPath.toStdString(), side->sourceId);
                    } else if (side->format == "engine") {
                        waveformsByKey[key] =
                            infrastructure::engine::readWaveformPreview(enginePath.toStdString(), side->sourceId);
                    }
                    // No waveform reader for OneLibrary -- left absent,
                    // see trackToMap()'s own handling of a missing key.
                }
                reporter->tick(++waveformsProcessed);
            }
        }
        reporter->finish();

        result.plans = std::move(actionable);
        result.waveformsByKey = std::move(waveformsByKey);
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

    m_watcher.setFuture(QtConcurrent::run(runAnalyzeTask, rekordboxPath, enginePath, makeReporter()));
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
    m_model.setPlans(std::move(result.plans), std::move(result.waveformsByKey));
    recomputeDirectionCounts();
    setBusy(false);
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
// OneLibrary sharing one stick root's .djconvert-backups, never self-
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
            lockDirs.push_back((fs::path(rekordboxPath.toStdString()).parent_path() / ".djconvert-backups").string());
        }
        if (!enginePath.isEmpty()) {
            lockDirs.push_back((fs::path(enginePath.toStdString()).parent_path() / ".djconvert-backups").string());
        }
        auto locks = acquireStickLocks(lockDirs);

        reporter->start("Writing cues", actionablePlans.size());
        size_t written = 0;
        QStringList summaryParts;

        for (const auto &[targetFormat, items] : byTargetFormat) {
            QString path = pathForFormat(targetFormat);
            std::string stickRoot = fs::path(path.toStdString()).parent_path().string();
            infrastructure::backup::FilesystemBackupStore backupStore((fs::path(stickRoot) / ".djconvert-backups").string());
            infrastructure::logging::FileOperationLog log((fs::path(stickRoot) / ".djconvert.log").string());
            int cuesCopied = 0;

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
                infrastructure::engine::LibdjinteropEngineCueWriter writer(path.toStdString());
                std::string engineDbFile = (fs::path(path.toStdString()) / "Database2" / "m.db").string();
                auto record = backupStore.backup({engineDbFile}, "sync");
                log.record("sync: backed up m.db -> " + record.path);
                result.backups.push_back({QString::fromStdString(fs::path(record.path).parent_path().string()),
                                           QString::fromStdString(record.id)});
                for (const auto &item : items) {
                    writer.writeHotCues(item.targetTrack.sourceId, item.cuesToApply);
                    cuesCopied += static_cast<int>(item.cuesToApply.size());
                    log.record("sync: copied cues (" + describeCues(item.cuesToApply).toStdString() + ") from " +
                               item.sourceFormat + " track \"" + item.sourceTitle + "\" (id=" + item.sourceSourceId +
                               ") to engine track id=" + item.targetTrack.sourceId);
                    reporter->tick(++written);
                }
            } else if (targetFormat == "onelibrary") {
                infrastructure::onelibrary::OneLibraryCueWriter writer(path.toStdString());
                std::string dbFile = infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(path.toStdString());
                auto record = backupStore.backup({dbFile}, "sync");
                log.record("sync: backed up exportLibrary.db -> " + record.path);
                result.backups.push_back({QString::fromStdString(fs::path(record.path).parent_path().string()),
                                           QString::fromStdString(record.id)});
                for (const auto &item : items) {
                    writer.writeCuesForPath(item.targetTrack.filePath, item.cuesToApply);
                    cuesCopied += static_cast<int>(item.cuesToApply.size());
                    log.record("sync: copied cues (" + describeCues(item.cuesToApply).toStdString() + ") from " +
                               item.sourceFormat + " track \"" + item.sourceTitle + "\" to OneLibrary track path=" +
                               item.targetTrack.filePath);
                    reporter->tick(++written);
                }
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

    // Re-analyze so the model reflects the now-consistent state.
    analyze(m_rekordboxPath, m_enginePath);
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

}  // namespace djconvert::gui
