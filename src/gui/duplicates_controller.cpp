#include "duplicates_controller.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>

#include "application/ports/backup_store.hpp"
#include "application/ports/cue_writer.hpp"
#include "application/ports/operation_log.hpp"
#include "application/use_cases/consolidate_duplicate_cues.hpp"
#include "application/use_cases/scan_library.hpp"
#include "gui/local_file_url.hpp"
#include "gui/qt_progress_reporter.hpp"
#include "gui/write_guard.hpp"
#include "infrastructure/backup/filesystem_backup_store.hpp"
#include "infrastructure/backup/stick_write_lock.hpp"
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
using domain::ConsolidationPlan;

namespace
{

// Mirrors cli/main.cpp's humanSize() exactly.
QString humanSize(std::uint64_t bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units)) {
        value /= 1024.0;
        unit++;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f %s", value, units[unit]);
    return QString::fromUtf8(buf);
}

// Bytes that would be freed if this group kept only its single largest
// file instead of every copy -- 0 if fewer than two tracks have a known
// size (nothing meaningful to compare).
std::uint64_t wastedBytes(const ConsolidationPlan &plan)
{
    std::uint64_t total = 0;
    std::uint64_t largest = 0;
    int known = 0;
    for (const auto &t : plan.group.tracks) {
        if (t.fileSizeBytes == 0) {
            continue;
        }
        known++;
        total += t.fileSizeBytes;
        largest = std::max(largest, t.fileSizeBytes);
    }
    return known >= 2 ? total - largest : 0;
}

}  // namespace

ConsolidationPlanListModel::ConsolidationPlanListModel(QObject *parent) : QAbstractListModel(parent) {}

int ConsolidationPlanListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_plans.size());
}

QVariant ConsolidationPlanListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= m_plans.size()) {
        return {};
    }
    const auto &plan = m_plans[static_cast<size_t>(index.row())];
    switch (role) {
    case KindRole:
        return plan.kind == ConsolidationPlan::Kind::Unambiguous ? QStringLiteral("unambiguous")
                                                                   : QStringLiteral("conflict");
    case FilenameRole:
        return QString::fromStdString(plan.group.tracks.empty() ? "" : plan.group.tracks.front().filename);
    case DescriptionRole:
        if (plan.kind == ConsolidationPlan::Kind::Unambiguous) {
            return QString("%1 cue(s) on one copy, missing on %2 other copy/copies")
                .arg(plan.source->cues.size())
                .arg(plan.targets.size());
        }
        return QString("%1 copies have different cues -- not touching them").arg(plan.group.tracks.size());
    case ActionableRole:
        return plan.kind == ConsolidationPlan::Kind::Unambiguous;
    case TracksRole: {
        QVariantList result;
        for (const auto &t : plan.group.tracks) {
            QVariantMap trackMap;
            trackMap["sourceId"] = QString::fromStdString(t.sourceId);
            trackMap["title"] = QString::fromStdString(t.title);
            trackMap["artist"] = QString::fromStdString(t.artist);
            trackMap["filePath"] = QString::fromStdString(t.filePath);
            trackMap["artworkPath"] = toLocalFileUrl(t.artworkPath);
            trackMap["sizeBytes"] = static_cast<qulonglong>(t.fileSizeBytes);

            QStringList playlists;
            for (const auto &p : t.playlists) {
                playlists << QString::fromStdString(p.name);
            }
            trackMap["playlists"] = playlists;

            QVariantList cues;
            for (const auto &c : t.cues) {
                QVariantMap cueMap;
                cueMap["kind"] = c.kind == domain::CuePoint::Kind::Hot ? QStringLiteral("hot") : QStringLiteral("memory");
                cueMap["hotCueNumber"] = c.hotCueNumber;
                cueMap["positionMs"] = c.positionMs;
                cueMap["color"] = QString::fromStdString(c.color);
                cues << cueMap;
            }
            trackMap["cues"] = cues;
            trackMap["durationMs"] = t.durationSeconds * 1000.0;

            QVariantList waveform;
            auto waveformIt = m_waveformsBySourceId.find(t.sourceId);
            if (waveformIt != m_waveformsBySourceId.end()) {
                for (const auto &col : waveformIt->second) {
                    QVariantMap colMap;
                    colMap["low"] = col.low;
                    colMap["mid"] = col.mid;
                    colMap["high"] = col.high;
                    waveform << colMap;
                }
            }
            trackMap["waveform"] = waveform;

            result << trackMap;
        }
        return result;
    }
    case WastedBytesRole:
        return QString("%1 could be freed if this were on the stick once").arg(humanSize(wastedBytes(plan)));
    default:
        return {};
    }
}

QHash<int, QByteArray> ConsolidationPlanListModel::roleNames() const
{
    return {
        {KindRole, "kind"},
        {FilenameRole, "filename"},
        {DescriptionRole, "description"},
        {ActionableRole, "actionable"},
        {TracksRole, "tracks"},
        {WastedBytesRole, "wastedBytesDescription"},
    };
}

void ConsolidationPlanListModel::setPlans(
    std::vector<domain::ConsolidationPlan> plans,
    std::unordered_map<std::string, std::vector<domain::WaveformColumn>> waveformsBySourceId)
{
    beginResetModel();
    m_plans = std::move(plans);
    m_waveformsBySourceId = std::move(waveformsBySourceId);
    endResetModel();
}

namespace
{

// Mirrors cli/main.cpp's handleDuplicates writer/backup/log wiring exactly,
// so a GUI-applied consolidation behaves identically to the CLI's.
struct FormatContext
{
    std::unique_ptr<application::CueWriter> writer;
    std::unique_ptr<application::BackupStore> backupStore;
    std::unique_ptr<application::OperationLog> log;
    std::function<std::vector<std::string>(const std::string &)> filesToBackUpFor;
};

FormatContext makeContext(const QString &format, const QString &path)
{
    fs::path stickRoot = fs::path(path.toStdString()).parent_path();

    FormatContext ctx;
    ctx.backupStore = std::make_unique<infrastructure::backup::FilesystemBackupStore>(
        (stickRoot / ".djconvert-backups").string());
    ctx.log = std::make_unique<infrastructure::logging::FileOperationLog>((stickRoot / ".djconvert.log").string());

    if (format == "rekordbox") {
        std::string pioneerRoot = path.toStdString();
        ctx.writer = std::make_unique<infrastructure::rekordbox::RekordboxCueWriter>(pioneerRoot);
        ctx.filesToBackUpFor = [pioneerRoot](const std::string &trackSourceId) -> std::vector<std::string> {
            auto analyzePath = infrastructure::rekordbox::findAnlzPathForTrackId(
                pioneerRoot, static_cast<uint32_t>(std::stoul(trackSourceId)));
            if (!analyzePath) {
                return {};
            }
            return {infrastructure::rekordbox::extAnlzPath(pioneerRoot, *analyzePath)};
        };
    } else {
        std::string engineLibraryPath = path.toStdString();
        ctx.writer = std::make_unique<infrastructure::engine::LibdjinteropEngineCueWriter>(engineLibraryPath);
        std::string engineDbFile = (fs::path(engineLibraryPath) / "Database2" / "m.db").string();
        ctx.filesToBackUpFor = [engineDbFile](const std::string &) -> std::vector<std::string> { return {engineDbFile}; };
    }
    return ctx;
}

void copyCuesToTargets(const domain::Track &source, const std::vector<domain::Track> &targets, FormatContext &ctx,
                        std::set<std::string> &backedUpFiles, int &cuesCopied, int &targetsWritten,
                        std::vector<UndoableBackup> &outBackups)
{
    for (const auto &target : targets) {
        auto files = ctx.filesToBackUpFor(target.sourceId);
        files.erase(
            std::remove_if(files.begin(), files.end(), [&](const std::string &f) { return backedUpFiles.contains(f); }),
            files.end());
        if (!files.empty()) {
            auto record = ctx.backupStore->backup(files, "duplicate-cue-consolidation");
            ctx.log->record("backed up before duplicate-cue consolidation -> " + record.path);
            outBackups.push_back({QString::fromStdString(fs::path(record.path).parent_path().string()),
                                   QString::fromStdString(record.id)});
            for (const auto &f : files) {
                backedUpFiles.insert(f);
            }
        }

        ctx.writer->writeHotCues(target.sourceId, source.cues);
        ctx.log->record("copied " + std::to_string(source.cues.size()) + " cue(s) from track id=" + source.sourceId +
                         " to track id=" + target.sourceId);
        cuesCopied += static_cast<int>(source.cues.size());
        targetsWritten++;
    }
}

// Runs entirely on a background thread (see DuplicatesController::
// startApply()) -- writes are file I/O just like the rescan, so they get
// the same treatment: never block the UI thread, report progress as they
// go. multiGroup only changes the wording of the final status message.
DuplicatesWriteResult runApplyTask(QString format, QString path, std::vector<DuplicatesCopyOp> ops, bool multiGroup,
                                    std::shared_ptr<QtProgressReporter> reporter)
{
    DuplicatesWriteResult result;
    QString refusal = refuseIfRekordboxRunning();
    if (!refusal.isEmpty()) {
        result.errorMessage = refusal;
        return result;
    }
    try {
        std::string backupDir = (fs::path(path.toStdString()).parent_path() / ".djconvert-backups").string();
        infrastructure::backup::StickWriteLock lock(backupDir + "/.write.lock");

        auto ctx = makeContext(format, path);
        std::set<std::string> backedUpFiles;
        int totalCues = 0;
        int totalTargets = 0;
        int groups = 0;

        size_t totalWork = 0;
        for (const auto &op : ops) {
            totalWork += op.targets.size();
        }
        reporter->start("Writing cues", totalWork);
        size_t done = 0;

        for (const auto &op : ops) {
            int cuesCopied = 0;
            int targetsWritten = 0;
            copyCuesToTargets(op.source, op.targets, ctx, backedUpFiles, cuesCopied, targetsWritten, result.backups);
            totalCues += cuesCopied;
            totalTargets += targetsWritten;
            groups++;
            done += op.targets.size();
            reporter->tick(done);
        }
        reporter->finish();

        result.statusMessage = multiGroup
            ? QString("Consolidated %1 duplicate track(s): copied %2 cue(s) onto %3 track(s)")
                  .arg(groups).arg(totalCues).arg(totalTargets)
            : QString("Copied %1 cue(s) onto %2 track(s)").arg(totalCues).arg(totalTargets);
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

// Runs entirely on a background thread (see DuplicatesController::
// undoLastOperation()). An empty `backups` result always means "nothing
// left to undo" -- the caller's undo trail is stale either way.
DuplicatesWriteResult runUndoTask(std::vector<UndoableBackup> backups, std::shared_ptr<QtProgressReporter> reporter)
{
    DuplicatesWriteResult result;
    QString refusal = refuseIfRekordboxRunning();
    if (!refusal.isEmpty()) {
        result.errorMessage = refusal;
        return result;
    }
    int restored = 0;
    try {
        // All of these backups came from one controller instance (one
        // format+path), so in practice this is always exactly one
        // directory -- dedup anyway rather than assume it.
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
        result.statusMessage =
            QString("Undone -- restored %1 file(s) to their state before the last consolidation").arg(restored);
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

}  // namespace

namespace
{

// Runs entirely on a background thread (see DuplicatesController::
// rescan()) -- no access to the controller itself.
DuplicatesTaskResult runRescanTask(QString format, QString path, std::shared_ptr<QtProgressReporter> reporter)
{
    DuplicatesTaskResult result;
    try {
        std::vector<domain::Track> tracks;
        if (format == "rekordbox") {
            infrastructure::rekordbox::KaitaiRekordboxReader reader(path.toStdString());
            reader.setProgressReporter(*reporter);
            tracks = application::ScanLibrary(reader).execute();
        } else {
            infrastructure::engine::LibdjinteropEngineReader reader(path.toStdString());
            reader.setProgressReporter(*reporter);
            tracks = application::ScanLibrary(reader).execute();
        }

        auto allPlans = application::ConsolidateDuplicateCues().execute(tracks);
        std::vector<ConsolidationPlan> actionable;
        for (auto &plan : allPlans) {
            if (plan.kind == ConsolidationPlan::Kind::Unambiguous || plan.kind == ConsolidationPlan::Kind::Conflict) {
                actionable.push_back(std::move(plan));
            }
        }

        // Waveforms need their own file I/O per track, so only decode them
        // for tracks actually being displayed here, not the whole library.
        // This is a second phase after the scan above already finished (and
        // already reported 100%) -- give it its own start()/tick() run
        // rather than leave the bar looking stalled while it happens.
        size_t totalWaveformTracks = 0;
        for (const auto &plan : actionable) {
            totalWaveformTracks += plan.group.tracks.size();
        }
        reporter->start("Loading waveforms", totalWaveformTracks);
        size_t waveformsProcessed = 0;

        std::unordered_map<std::string, std::vector<domain::WaveformColumn>> waveformsBySourceId;
        for (const auto &plan : actionable) {
            for (const auto &track : plan.group.tracks) {
                if (!waveformsBySourceId.contains(track.sourceId)) {
                    std::vector<domain::WaveformColumn> waveform;
                    if (format == "rekordbox") {
                        waveform = infrastructure::rekordbox::readWaveformPreview(path.toStdString(), track.sourceId);
                    } else {
                        waveform = infrastructure::engine::readWaveformPreview(path.toStdString(), track.sourceId);
                    }
                    waveformsBySourceId[track.sourceId] = std::move(waveform);
                }
                reporter->tick(++waveformsProcessed);
            }
        }
        reporter->finish();

        result.plans = std::move(actionable);
        result.waveformsBySourceId = std::move(waveformsBySourceId);
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

}  // namespace

DuplicatesController::DuplicatesController(QObject *parent) : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<DuplicatesTaskResult>::finished, this,
            &DuplicatesController::onRescanFinished);
    connect(&m_writeWatcher, &QFutureWatcher<DuplicatesWriteResult>::finished, this,
            &DuplicatesController::onWriteFinished);
}

QString DuplicatesController::totalWastedBytesHuman() const
{
    std::uint64_t total = 0;
    for (const auto &plan : m_model.plans()) {
        total += wastedBytes(plan);
    }
    return humanSize(total);
}

void DuplicatesController::scan(const QString &format, const QString &path)
{
    m_format = format;
    m_path = path;
    rescan();
}

void DuplicatesController::rescan()
{
    if (m_busy) {
        return;  // never overlap two rescans
    }
    setErrorMessage({});
    setScanProgress(0, 0);
    setBusy(true);

    m_watcher.setFuture(QtConcurrent::run(runRescanTask, m_format, m_path, makeReporter()));
}

// See ScanController::scan() for why the reporter is owned by the task
// (via shared_ptr) rather than by this controller.
std::shared_ptr<QtProgressReporter> DuplicatesController::makeReporter()
{
    auto reporter = std::make_shared<QtProgressReporter>();
    connect(reporter.get(), &QtProgressReporter::started, this,
            [this](const QString &, int total) { setScanProgress(0, total); });
    connect(reporter.get(), &QtProgressReporter::progressed, this,
            [this](int current) { setScanProgress(current, m_scanTotal); });
    return reporter;
}

void DuplicatesController::onRescanFinished()
{
    DuplicatesTaskResult result = m_watcher.result();

    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
        setBusy(false);
        return;
    }

    m_model.setPlans(std::move(result.plans), std::move(result.waveformsBySourceId));
    setBusy(false);
    emit plansChanged();
}

void DuplicatesController::applyOne(int index)
{
    if (m_busy) {
        return;
    }
    const auto &plans = m_model.plans();
    if (index < 0 || static_cast<size_t>(index) >= plans.size()) {
        return;
    }
    const auto &plan = plans[static_cast<size_t>(index)];
    if (plan.kind != ConsolidationPlan::Kind::Unambiguous) {
        return;
    }
    std::vector<DuplicatesCopyOp> ops{{*plan.source, plan.targets}};
    startApply(std::move(ops), false);
}

void DuplicatesController::copyFromTrack(int index, const QString &sourceTrackId)
{
    if (m_busy) {
        return;
    }
    const auto &plans = m_model.plans();
    if (index < 0 || static_cast<size_t>(index) >= plans.size()) {
        return;
    }
    const auto &group = plans[static_cast<size_t>(index)].group;

    std::string wantedId = sourceTrackId.toStdString();
    const domain::Track *source = nullptr;
    std::vector<domain::Track> targets;
    for (const auto &track : group.tracks) {
        if (track.sourceId == wantedId) {
            source = &track;
        }
    }
    if (!source) {
        return;
    }
    for (const auto &track : group.tracks) {
        if (track.sourceId != source->sourceId) {
            targets.push_back(track);
        }
    }

    std::vector<DuplicatesCopyOp> ops{{*source, std::move(targets)}};
    startApply(std::move(ops), false);
}

void DuplicatesController::applyAllUnambiguous()
{
    if (m_busy) {
        return;
    }
    std::vector<DuplicatesCopyOp> ops;
    for (const auto &plan : m_model.plans()) {
        if (plan.kind == ConsolidationPlan::Kind::Unambiguous) {
            ops.push_back({*plan.source, plan.targets});
        }
    }
    startApply(std::move(ops), true);
}

void DuplicatesController::startApply(std::vector<DuplicatesCopyOp> ops, bool multiGroup)
{
    setErrorMessage({});
    setStatusMessage({});
    setScanProgress(0, 0);
    setBusy(true);
    setWriting(true);
    m_lastBackups.clear();
    emit canUndoChanged();

    m_writeWatcher.setFuture(
        QtConcurrent::run(runApplyTask, m_format, m_path, std::move(ops), multiGroup, makeReporter()));
}

// Common completion path for applyOne()/copyFromTrack()/
// applyAllUnambiguous()/undoLastOperation() -- they only differ in which
// background task fed the watcher.
void DuplicatesController::onWriteFinished()
{
    DuplicatesWriteResult result = m_writeWatcher.result();
    m_lastBackups = std::move(result.backups);
    emit canUndoChanged();

    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
    } else {
        setStatusMessage(result.statusMessage);
    }
    setBusy(false);
    setWriting(false);

    rescan();
}

void DuplicatesController::undoLastOperation()
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

void DuplicatesController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void DuplicatesController::setWriting(bool writing)
{
    if (m_writing == writing) {
        return;
    }
    m_writing = writing;
    emit writingChanged();
}

void DuplicatesController::setScanProgress(int current, int total)
{
    if (m_scanCurrent == current && m_scanTotal == total) {
        return;
    }
    m_scanCurrent = current;
    m_scanTotal = total;
    emit scanProgressChanged();
}

void DuplicatesController::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

void DuplicatesController::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) {
        return;
    }
    m_statusMessage = message;
    emit statusMessageChanged();
}

}  // namespace djconvert::gui
