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
using domain::ConsolidationPlan;

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
            trackMap["artworkPath"] = QString::fromStdString(t.artworkPath);

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
                for (double v : waveformIt->second) {
                    waveform << v;
                }
            }
            trackMap["waveform"] = waveform;

            result << trackMap;
        }
        return result;
    }
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
    };
}

void ConsolidationPlanListModel::setPlans(std::vector<domain::ConsolidationPlan> plans,
                                           std::unordered_map<std::string, std::vector<double>> waveformsBySourceId)
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
                        std::set<std::string> &backedUpFiles, int &cuesCopied, int &targetsWritten)
{
    for (const auto &target : targets) {
        auto files = ctx.filesToBackUpFor(target.sourceId);
        files.erase(
            std::remove_if(files.begin(), files.end(), [&](const std::string &f) { return backedUpFiles.contains(f); }),
            files.end());
        if (!files.empty()) {
            auto record = ctx.backupStore->backup(files, "duplicate-cue-consolidation");
            ctx.log->record("backed up before duplicate-cue consolidation -> " + record.path);
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
        std::unordered_map<std::string, std::vector<double>> waveformsBySourceId;
        for (const auto &plan : actionable) {
            for (const auto &track : plan.group.tracks) {
                if (waveformsBySourceId.contains(track.sourceId)) {
                    continue;
                }
                std::vector<double> waveform;
                if (format == "rekordbox") {
                    waveform = infrastructure::rekordbox::readWaveformPreview(path.toStdString(), track.sourceId);
                } else {
                    waveform = infrastructure::engine::readWaveformPreview(path.toStdString(), track.sourceId);
                }
                waveformsBySourceId[track.sourceId] = std::move(waveform);
            }
        }

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

    // See ScanController::scan() for why the reporter is owned by the task
    // (via shared_ptr) rather than by this controller.
    auto reporter = std::make_shared<QtProgressReporter>();
    connect(reporter.get(), &QtProgressReporter::started, this,
            [this](const QString &, int total) { setScanProgress(0, total); });
    connect(reporter.get(), &QtProgressReporter::progressed, this,
            [this](int current) { setScanProgress(current, m_scanTotal); });

    m_watcher.setFuture(QtConcurrent::run(runRescanTask, m_format, m_path, reporter));
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
}

void DuplicatesController::applyOne(int index)
{
    const auto &plans = m_model.plans();
    if (index < 0 || static_cast<size_t>(index) >= plans.size()) {
        return;
    }
    if (plans[static_cast<size_t>(index)].kind != ConsolidationPlan::Kind::Unambiguous) {
        return;
    }
    ConsolidationPlan plan = plans[static_cast<size_t>(index)];

    setErrorMessage({});
    setStatusMessage({});
    setBusy(true);
    try {
        auto ctx = makeContext(m_format, m_path);
        std::set<std::string> backedUpFiles;
        int cuesCopied = 0;
        int targetsWritten = 0;
        copyCuesToTargets(*plan.source, plan.targets, ctx, backedUpFiles, cuesCopied, targetsWritten);
        setStatusMessage(QString("Copied %1 cue(s) onto %2 track(s)").arg(cuesCopied).arg(targetsWritten));
    } catch (const std::exception &e) {
        setErrorMessage(QString::fromStdString(e.what()));
    }
    setBusy(false);

    rescan();
}

void DuplicatesController::copyFromTrack(int index, const QString &sourceTrackId)
{
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

    setErrorMessage({});
    setStatusMessage({});
    setBusy(true);
    try {
        auto ctx = makeContext(m_format, m_path);
        std::set<std::string> backedUpFiles;
        int cuesCopied = 0;
        int targetsWritten = 0;
        copyCuesToTargets(*source, targets, ctx, backedUpFiles, cuesCopied, targetsWritten);
        setStatusMessage(QString("Copied %1 cue(s) onto %2 track(s)").arg(cuesCopied).arg(targetsWritten));
    } catch (const std::exception &e) {
        setErrorMessage(QString::fromStdString(e.what()));
    }
    setBusy(false);

    rescan();
}

void DuplicatesController::applyAllUnambiguous()
{
    setErrorMessage({});
    setStatusMessage({});
    setBusy(true);

    int totalCues = 0;
    int totalTargets = 0;
    int groups = 0;
    try {
        auto ctx = makeContext(m_format, m_path);
        std::set<std::string> backedUpFiles;
        for (const auto &plan : m_model.plans()) {
            if (plan.kind != ConsolidationPlan::Kind::Unambiguous) {
                continue;
            }
            int cuesCopied = 0;
            int targetsWritten = 0;
            copyCuesToTargets(*plan.source, plan.targets, ctx, backedUpFiles, cuesCopied, targetsWritten);
            totalCues += cuesCopied;
            totalTargets += targetsWritten;
            groups++;
        }
        setStatusMessage(QString("Consolidated %1 duplicate track(s): copied %2 cue(s) onto %3 track(s)")
                              .arg(groups)
                              .arg(totalCues)
                              .arg(totalTargets));
    } catch (const std::exception &e) {
        setErrorMessage(QString::fromStdString(e.what()));
    }
    setBusy(false);

    rescan();
}

void DuplicatesController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
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
