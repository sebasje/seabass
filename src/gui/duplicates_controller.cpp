#include "duplicates_controller.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>

#include "application/ports/cue_writer.hpp"
#include "application/use_cases/consolidate_duplicate_cues.hpp"
#include "gui/edit/edit_session_registry.hpp"
#include "gui/edit/library_edit_session.hpp"
#include "gui/edit/pending_change.hpp"
#include "gui/edit/save_context.hpp"
#include "gui/library_catalog_cache.hpp"
#include "gui/local_file_url.hpp"
#include "gui/onelibrary_cue_writer_adapter.hpp"
#include "gui/qt_progress_reporter.hpp"
#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/rekordbox/pdb_lookup.hpp"
#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"
#include "gui/edit/changes/copy_cues_change.hpp"

namespace seabass::gui
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
// file instead of every copy, 0 if fewer than two tracks have a known
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
    const auto row = static_cast<size_t>(index.row());
    const auto &plan = m_plans[row];
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
        return QString("%1 copies have different cues, not touching them").arg(plan.group.tracks.size());
    case ActionableRole:
        return plan.kind == ConsolidationPlan::Kind::Unambiguous;
    case TracksRole: {
        QVariantList result;
        for (const auto &t : plan.group.tracks) {
            QVariantMap trackMap;
            trackMap["side"] = QString::fromStdString(t.format);
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

            result << trackMap;
        }
        return result;
    }
    case WastedBytesRole:
        return QString("%1 could be freed if this were on the stick once").arg(humanSize(wastedBytes(plan)));
    case StagedRole:
        return row < m_stagedDescriptions.size() && !m_stagedDescriptions[row].isEmpty();
    case StagedDescriptionRole:
        return row < m_stagedDescriptions.size() ? m_stagedDescriptions[row] : QString();
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
        {StagedRole, "staged"},
        {StagedDescriptionRole, "stagedDescription"},
    };
}

void ConsolidationPlanListModel::setPlans(std::vector<domain::ConsolidationPlan> plans)
{
    beginResetModel();
    m_plans = std::move(plans);
    m_stagedDescriptions.assign(m_plans.size(), QString());
    endResetModel();
}

void ConsolidationPlanListModel::removePlanAt(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= m_plans.size()) {
        return;
    }
    beginRemoveRows(QModelIndex(), index, index);
    m_plans.erase(m_plans.begin() + index);
    m_stagedDescriptions.erase(m_stagedDescriptions.begin() + index);
    endRemoveRows();
}

void ConsolidationPlanListModel::setStaged(int index, bool staged, const QString &description)
{
    if (index < 0 || static_cast<size_t>(index) >= m_plans.size()) {
        return;
    }
    m_stagedDescriptions[static_cast<size_t>(index)] = staged ? description : QString();
    emit dataChanged(this->index(index), this->index(index), {StagedRole, StagedDescriptionRole});
}

void ConsolidationPlanListModel::clearStaged()
{
    if (m_plans.empty()) {
        return;
    }
    std::fill(m_stagedDescriptions.begin(), m_stagedDescriptions.end(), QString());
    emit dataChanged(index(0), index(static_cast<int>(m_plans.size()) - 1), {StagedRole, StagedDescriptionRole});
}

namespace
{

// Mirrors cli/main.cpp's handleDuplicates writer wiring exactly, so a
// GUI-applied consolidation behaves identically to the CLI's. One per
// format per save (SaveContext::shared), except OneLibrary, whose adapter
// needs the tracks it is about to write (see below).

// Runs entirely on a background thread (see DuplicatesController::
// rescan()) - no access to the controller itself.
DuplicatesTaskResult runRescanTask(QString format, QString path, std::shared_ptr<QtProgressReporter> reporter,
                                   application::CancellationToken cancel)
{
    DuplicatesTaskResult result;
    try {
        std::vector<domain::Track> tracks =
            LibraryCatalogCache::instance().tracksFor(format.toStdString(), path.toStdString(), *reporter, cancel);

        // Streaming tracks (Engine/TIDAL) have no real local file.
        // Never propose "syncing" cues onto/from one. See
        // domain::Track::streamingSource's own doc comment for why.
        tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
                                    [](const domain::Track &t) { return !t.streamingSource.empty(); }),
                     tracks.end());

        // No per-item progress for this pass (it's not a simple linear
        // scan), but the label change alone is the actual fix: without
        // it, the progress bar sat frozen at 100% -- the raw file scan's
        // own end state -- for however long grouping took on a real
        // library, with nothing telling the user it was still working.
        reporter->start("Finding duplicates...", 0);
        auto allPlans = application::ConsolidateDuplicateCues().execute(tracks);
        std::vector<ConsolidationPlan> actionable;
        for (auto &plan : allPlans) {
            if (plan.kind == ConsolidationPlan::Kind::Unambiguous || plan.kind == ConsolidationPlan::Kind::Conflict) {
                actionable.push_back(std::move(plan));
            }
        }

        // Waveforms are deliberately NOT loaded here -- QML fetches one
        // on demand via PlaybackController::waveformFor() instead (see
        // ConsolidationPlanListModel::setPlans()'s own comment for why:
        // eagerly decoding one per displayed track here was the same
        // shape of bug confirmed to make Sync's own "scanning" phase
        // take 5+ minutes on a real library).
        result.plans = std::move(actionable);
    } catch (const application::OperationCancelled &) {
        result.cancelled = true;
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

QString DuplicatesController::totalWastedBytesHuman() const
{
    std::uint64_t total = 0;
    for (const auto &plan : m_model.plans()) {
        total += wastedBytes(plan);
    }
    return humanSize(total);
}

bool DuplicatesController::writing() const
{
    return m_session && m_session->writing();
}

bool DuplicatesController::canUndo() const
{
    return m_session && m_session->canUndo();
}

// The group's identity across rescans: its member track ids, sorted.
QString DuplicatesController::groupKeyFor(const domain::ConsolidationPlan &plan)
{
    QStringList ids;
    for (const auto &t : plan.group.tracks) {
        ids << QString::fromStdString(t.sourceId);
    }
    ids.sort();
    return ids.join('+');
}

int DuplicatesController::indexOfGroupKey(const QString &groupKey) const
{
    const auto &plans = m_model.plans();
    for (size_t i = 0; i < plans.size(); ++i) {
        if (groupKeyFor(plans[i]) == groupKey) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void DuplicatesController::attachSession()
{
    auto *registry = EditSessionRegistry::instance();
    LibraryEditSession *session = registry->sessionFor(registry->libraryIdForPath(m_path));
    if (session != m_session) {
        if (m_session) {
            disconnect(m_session, nullptr, this, nullptr);
        }
        m_session = session;
        if (m_session) {
            connect(m_session, &LibraryEditSession::stateChanged, this, &DuplicatesController::writingChanged);
            connect(m_session, &LibraryEditSession::canUndoChanged, this, &DuplicatesController::canUndoChanged);
            connect(m_session, &LibraryEditSession::changeApplied, this, [this](const QString &changeId) {
                if (changeId == QStringLiteral("undo:last-save")) {
                    rescan();  // prior file bytes are back; the plan list is stale
                    return;
                }
                for (auto it = m_stagedByGroup.begin(); it != m_stagedByGroup.end(); ++it) {
                    if (it->second.changeId == changeId) {
                        // Cues just got copied onto every other track in
                        // this group -- it's AlreadyConsistent now, which
                        // this model never shows, so the row disappears.
                        int index = indexOfGroupKey(it->first);
                        m_stagedByGroup.erase(it);
                        if (index >= 0) {
                            m_model.removePlanAt(index);
                        }
                        emit plansChanged();
                        break;
                    }
                }
            });
            connect(m_session, &LibraryEditSession::changesDiscarded, this, [this]() {
                m_stagedByGroup.clear();
                m_model.clearStaged();
                emit plansChanged();
            });
        }
    }
    if (m_session) {
        if (m_format == "engine") {
            m_session->setLibraryPaths(QString(), m_path);
        } else {
            m_session->setLibraryPaths(m_path, QString());
        }
    }
}

void DuplicatesController::scan(const QString &format, const QString &path)
{
    m_format = format;
    m_path = path;
    attachSession();
    rescan();
}

bool DuplicatesController::hasOneLibrary(const QString &pioneerRoot) const
{
    return infrastructure::onelibrary::OneLibraryCueWriter::existsFor(pioneerRoot.toStdString());
}

void DuplicatesController::rescan()
{
    if (m_busy) {
        return;  // never overlap two rescans
    }
    setErrorMessage({});
    setScanProgress(0, 0);
    setBusy(true);

    m_scanCancel = application::CancellationToken();
    m_watcher.setFuture(QtConcurrent::run(runRescanTask, m_format, m_path, makeReporter(), m_scanCancel));
}

void DuplicatesController::cancelScan()
{
    if (scanCancellable()) {
        m_scanCancel.cancel();
    }
}

// See ScanController::scan() for why the reporter is owned by the task
// (via shared_ptr) rather than by this controller.
std::shared_ptr<QtProgressReporter> DuplicatesController::makeReporter()
{
    auto reporter = std::make_shared<QtProgressReporter>();
    connect(reporter.get(), &QtProgressReporter::started, this, [this](const QString &label, int total) {
        setScanLabel(label);
        setScanProgress(0, total);
    });
    connect(reporter.get(), &QtProgressReporter::progressed, this,
            [this](int current) { setScanProgress(current, m_scanTotal); });
    return reporter;
}

void DuplicatesController::onRescanFinished()
{
    DuplicatesTaskResult result = m_watcher.result();

    if (result.cancelled) {
        setBusy(false);
        emit scanCancelled();
        return;
    }
    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
        setBusy(false);
        return;
    }

    m_model.setPlans(std::move(result.plans));
    // Groups staged before this rescan keep their mark if they are still
    // listed (the change itself lives in the session either way).
    for (const auto &[groupKey, info] : m_stagedByGroup) {
        int index = indexOfGroupKey(groupKey);
        if (index >= 0) {
            m_model.setStaged(index, true, info.description);
        }
    }
    setBusy(false);
    emit plansChanged();
}

void DuplicatesController::stageCopy(int index, const DuplicatesCopyOp &op)
{
    if (!m_session) {
        attachSession();
        if (!m_session) {
            setErrorMessage("This stick's library could not be identified; nothing was changed.");
            return;
        }
    }
    if (m_session->writing()) {
        setErrorMessage("A save is running -- stage more once it has finished.");
        return;
    }
    const auto &plan = m_model.plans()[static_cast<size_t>(index)];
    QString groupKey = groupKeyFor(plan);
    auto change = std::make_unique<CopyCuesChange>(m_format, m_path, groupKey, op);
    QString changeId = change->id();
    QString description = change->description();
    if (!m_session->stage(std::move(change))) {
        return;  // the session reported the lock refusal; the page shows it
    }
    m_stagedByGroup[groupKey] = {changeId, description};
    m_model.setStaged(index, true, description);
    emit plansChanged();
}

void DuplicatesController::applyOne(int index)
{
    if (m_busy) {
        return;
    }
    setErrorMessage({});
    setStatusMessage({});
    const auto &plans = m_model.plans();
    if (index < 0 || static_cast<size_t>(index) >= plans.size()) {
        return;
    }
    const auto &plan = plans[static_cast<size_t>(index)];
    if (plan.kind != ConsolidationPlan::Kind::Unambiguous) {
        return;
    }
    stageCopy(index, {*plan.source, plan.targets});
}

void DuplicatesController::copyFromTrack(int index, const QString &sourceTrackId)
{
    if (m_busy) {
        return;
    }
    setErrorMessage({});
    setStatusMessage({});
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
    stageCopy(index, {*source, std::move(targets)});
}

void DuplicatesController::applyAllUnambiguous()
{
    if (m_busy) {
        return;
    }
    setErrorMessage({});
    setStatusMessage({});
    const auto &plans = m_model.plans();
    int staged = 0;
    for (size_t i = 0; i < plans.size(); ++i) {
        if (plans[i].kind == ConsolidationPlan::Kind::Unambiguous) {
            stageCopy(static_cast<int>(i), {*plans[i].source, plans[i].targets});
            staged++;
            if (m_session && !m_session->lockHeld()) {
                return;  // refused at the first one; no point trying the rest
            }
        }
    }
    if (staged > 0) {
        setStatusMessage(QStringLiteral("Staged %1 group(s). Press Save to copy the cues onto the stick.").arg(staged));
    }
}

void DuplicatesController::unstage(int index)
{
    const auto &plans = m_model.plans();
    if (index < 0 || static_cast<size_t>(index) >= plans.size()) {
        return;
    }
    QString groupKey = groupKeyFor(plans[static_cast<size_t>(index)]);
    auto it = m_stagedByGroup.find(groupKey);
    if (it == m_stagedByGroup.end()) {
        return;
    }
    if (m_session) {
        m_session->unstage(it->second.changeId);
    }
    m_stagedByGroup.erase(it);
    m_model.setStaged(index, false, QString());
    emit plansChanged();
}

void DuplicatesController::undoLastOperation()
{
    if (m_busy || !m_session) {
        return;
    }
    setErrorMessage({});
    setStatusMessage({});
    m_session->undoLastSave();
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

void DuplicatesController::setScanLabel(const QString &label)
{
    if (m_scanLabel == label) {
        return;
    }
    m_scanLabel = label;
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

}  // namespace seabass::gui
