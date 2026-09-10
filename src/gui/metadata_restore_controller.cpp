#include "metadata_restore_controller.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include <exception>
#include <memory>

#include "application/use_cases/collapse_catalog_rows.hpp"
#include "gui/edit/changes/restore_metadata_change.hpp"
#include "gui/edit/edit_session_registry.hpp"
#include "gui/edit/library_edit_session.hpp"
#include "gui/stick_catalogs.hpp"
#include "infrastructure/engine/engine_library_layout.hpp"
#include "infrastructure/local/metadata_store.hpp"

namespace seabass::gui
{

using domain::MetadataRestorePolicy;
using domain::MetadataRestoreProposal;
using infrastructure::local::MetadataStore;

namespace
{

namespace fs = std::filesystem;

// The catalog directory for one format on the stick that `libraryPath`
// belongs to.
//
// The page hands the controller a single path -- whichever catalog the
// stick list happened to open -- but a proposal carries the rows of
// every catalog that lists the track, and each row's change has to be
// given the path for ITS OWN format. Handing an Engine change the
// PIONEER folder points the Engine writer, and its backup, at a
// database that is not there. Same derivation readAllStickCatalogs()
// uses to find the catalogs in the first place.
QString catalogPathForFormat(const QString &libraryPath, const std::string &format)
{
    const fs::path stickRoot = fs::path(libraryPath.toStdString()).parent_path();
    if (format == "engine") {
        return QString::fromStdString(infrastructure::engine::engineLibraryPath(stickRoot).string());
    }
    // rekordbox and onelibrary are two formats of one library, both
    // under PIONEER.
    return QString::fromStdString((stickRoot / "PIONEER").string());
}

// Runs entirely on a background thread -- no access to the controller.
MetadataRestoreTaskResult runScanTask(QString libraryPath, bool overwriteConflicts,
                                       std::shared_ptr<QtProgressReporter> reporter,
                                       application::CancellationToken cancel)
{
    MetadataRestoreTaskResult result;
    try {
        const auto read = readAllStickCatalogs(libraryPath.toStdString(), *reporter, cancel);
        if (read.catalogs.present().empty()) {
            result.errorMessage = QStringLiteral("No rekordbox or Engine library was found on this stick.");
            return result;
        }

        // Folded into files first, so a track three catalogs list is one
        // proposal carrying all three rows -- not three proposals, each
        // of which would look like a separate track to restore.
        std::vector<domain::Track> rows;
        for (const auto *catalog : {&read.catalogs.rekordbox, &read.catalogs.oneLibrary, &read.catalogs.engine}) {
            if (*catalog) {
                rows.insert(rows.end(), (*catalog)->begin(), (*catalog)->end());
            }
        }
        const auto stickTracks = application::collapseCatalogRows(rows);
        result.stickTrackCount = static_cast<int>(stickTracks.size());

        reporter->start("Reading the metadata store", 0);
        MetadataStore store;
        const auto storedTracks = store.readAll();
        reporter->finish();
        result.storedTrackCount = static_cast<int>(storedTracks.size());

        if (cancel.cancelled()) {
            result.cancelled = true;
            return result;
        }

        result.proposals = domain::planMetadataRestore(
            stickTracks, storedTracks,
            overwriteConflicts ? MetadataRestorePolicy::OverwriteConflicts : MetadataRestorePolicy::SkipConflicts);

        // Counted from a plan that keeps conflicts, so the number is the
        // same under both policies and the page can say "N of these were
        // left alone" without rescanning.
        const auto everything = domain::planMetadataRestore(stickTracks, storedTracks,
                                                             MetadataRestorePolicy::OverwriteConflicts);
        for (const auto &proposal : everything) {
            if (proposal.cuesConflict) {
                result.conflictCount++;
            }
        }
    } catch (const application::OperationCancelled &) {
        result.cancelled = true;
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromUtf8(e.what());
    }
    return result;
}

}  // namespace

// ---- model ----------------------------------------------------------

RestoreProposalListModel::RestoreProposalListModel(QObject *parent) : QAbstractListModel(parent) {}

int RestoreProposalListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_proposals.size());
}

QHash<int, QByteArray> RestoreProposalListModel::roleNames() const
{
    return {
        {TitleRole, "title"},         {ArtistRole, "artist"},   {FilenameRole, "filename"},
        {CueCountRole, "cueCount"},   {CuesAddedRole, "cuesAdded"}, {FillsAGapRole, "fillsAGap"},
        {ConflictRole, "conflict"},   {StagedRole, "staged"},
        {RatingRole, "rating"},       {CommentRole, "comment"},
    };
}

QVariant RestoreProposalListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= static_cast<int>(m_proposals.size())) {
        return {};
    }
    const auto &proposal = m_proposals[static_cast<std::size_t>(index.row())];
    switch (role) {
    case TitleRole:
        return QString::fromStdString(proposal.stickTrack.title);
    case ArtistRole:
        return QString::fromStdString(proposal.stickTrack.artist);
    case FilenameRole:
        return QString::fromStdString(proposal.stickTrack.filename);
    case CueCountRole:
        return static_cast<int>(proposal.cues.size());
    case CuesAddedRole:
        return proposal.cuesAdded();
    case FillsAGapRole:
        return proposal.cuesFillAGap;
    case ConflictRole:
        return proposal.cuesConflict;
    case RatingRole:
        // -1 when this restore offers no rating, so a row can tell "no
        // rating on offer" from "zero stars on offer".
        return proposal.ratingOffered && proposal.rating ? *proposal.rating : -1;
    case CommentRole:
        return proposal.commentOffered ? QString::fromStdString(proposal.comment) : QString();
    case StagedRole:
        return m_staged[static_cast<std::size_t>(index.row())];
    default:
        return {};
    }
}

void RestoreProposalListModel::setProposals(std::vector<MetadataRestoreProposal> proposals)
{
    beginResetModel();
    m_proposals = std::move(proposals);
    m_staged.assign(m_proposals.size(), false);
    endResetModel();
}

void RestoreProposalListModel::setStaged(int index, bool staged)
{
    if (index < 0 || index >= static_cast<int>(m_proposals.size())) {
        return;
    }
    m_staged[static_cast<std::size_t>(index)] = staged;
    emit dataChanged(this->index(index), this->index(index), {StagedRole});
}

void RestoreProposalListModel::removeAt(int index)
{
    if (index < 0 || index >= static_cast<int>(m_proposals.size())) {
        return;
    }
    beginRemoveRows(QModelIndex(), index, index);
    m_proposals.erase(m_proposals.begin() + index);
    m_staged.erase(m_staged.begin() + index);
    endRemoveRows();
}

int RestoreProposalListModel::indexOfStoredId(const std::string &storedId) const
{
    for (std::size_t i = 0; i < m_proposals.size(); ++i) {
        if (m_proposals[i].storedId == storedId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// ---- controller -----------------------------------------------------

MetadataRestoreController::MetadataRestoreController(QObject *parent) : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<MetadataRestoreTaskResult>::finished, this,
            &MetadataRestoreController::onScanFinished);
}

MetadataRestoreController::~MetadataRestoreController()
{
    m_cancel.cancel();
    if (m_watcher.isRunning()) {
        m_watcher.waitForFinished();
    }
}

bool MetadataRestoreController::writing() const
{
    return m_session && m_session->state() == QStringLiteral("writing");
}

void MetadataRestoreController::scan(const QString &libraryPath, bool overwriteConflicts)
{
    if (m_busy) {
        return;
    }
    m_libraryPath = libraryPath;
    setErrorMessage({});
    m_phaseBaseline = 0;
    m_currentPhaseTotal = 0;
    setProgress(0, 0);
    setCurrentPhase(QStringLiteral("Reading this stick"));
    m_cancel = application::CancellationToken();
    setBusy(true);
    m_watcher.setFuture(
        QtConcurrent::run(runScanTask, libraryPath, overwriteConflicts, makeReporter(), m_cancel));
}

void MetadataRestoreController::cancelScan()
{
    m_cancel.cancel();
}

void MetadataRestoreController::onScanFinished()
{
    const MetadataRestoreTaskResult result = m_watcher.result();
    setBusy(false);
    setCurrentPhase({});
    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
        return;
    }
    if (result.cancelled) {
        return;
    }
    m_stagedByStoredId.clear();
    m_model.setProposals(result.proposals);
    m_stickTrackCount = result.stickTrackCount;
    m_storedTrackCount = result.storedTrackCount;
    m_conflictCount = result.conflictCount;
    // A comment can only be written where a format can grow one, and
    // export.pdb cannot (tests/pdb_rating_write_test.cpp). A track that
    // rekordbox alone catalogues therefore gets its rating back and not
    // its comment, and the page has to say so.
    m_commentsRekordboxCannotTake = 0;
    for (const auto &proposal : result.proposals) {
        if (!proposal.commentOffered) {
            continue;
        }
        // "No row anywhere but rekordbox", and a track with no catalog
        // rows at all is not that: collapseCatalogRows() leaves the list
        // empty for a row with no file path and for an Engine streaming
        // track, and counting those would have the banner warn about
        // DeviceLibrary on a stick that may carry no rekordbox catalog.
        if (proposal.stickTrack.catalogRows.empty()) {
            continue;
        }
        bool elsewhere = false;
        for (const auto &row : proposal.stickTrack.catalogRows) {
            if (row.format != "rekordbox") {
                elsewhere = true;
            }
        }
        if (!elsewhere) {
            m_commentsRekordboxCannotTake++;
        }
    }
    m_hasScanned = true;
    emit analysisChanged();
}

void MetadataRestoreController::attachSession()
{
    auto *registry = EditSessionRegistry::instance();
    LibraryEditSession *session = registry->sessionFor(registry->libraryIdForPath(m_libraryPath));
    if (session == m_session) {
        return;
    }
    if (m_session) {
        disconnect(m_session, nullptr, this, nullptr);
    }
    m_session = session;
    if (!m_session) {
        return;
    }
    connect(m_session, &LibraryEditSession::stateChanged, this, &MetadataRestoreController::writingChanged);
    connect(m_session, &LibraryEditSession::canUndoChanged, this, &MetadataRestoreController::canUndoChanged);
    connect(m_session, &LibraryEditSession::changeApplied, this, [this](const QString &changeId) {
        // A proposal disappears once every change it staged has landed:
        // the stick now has what the store had, so there is nothing left
        // to offer it.
        for (auto it = m_stagedByStoredId.begin(); it != m_stagedByStoredId.end(); ++it) {
            if (!it->second.contains(changeId)) {
                continue;
            }
            it->second.removeAll(changeId);
            if (it->second.isEmpty()) {
                const int index = m_model.indexOfStoredId(it->first);
                m_stagedByStoredId.erase(it);
                if (index >= 0) {
                    m_model.removeAt(index);
                }
                emit analysisChanged();
            }
            return;
        }
    });
}

void MetadataRestoreController::stage(int index)
{
    // One track staged on its own is one track's worth of writing, and
    // saying otherwise is not the safe direction.
    //
    // The obvious-looking alternative -- hint the whole list, since the
    // session is built by whichever change applies first and a running
    // total is always 1 there -- makes the wrong trade for SQLite. A
    // hint of 400 for a single staged Engine track has the save copy the
    // entire m.db to scratch and durably copy it back for one row
    // update, and a whole-file replace can lose the database in a way a
    // page-level write cannot. Staging four hundred tracks one at a time
    // therefore does not earn the scratch copy. Stage All, which is how
    // that is actually done, passes the real count and does.
    stageOne(index, 1);
}

// itemCountHint is what the save is expected to write in total, which
// decides whether the catalog is worth copying to local scratch first.
// Stage All knows that number; a single Stage does not, and does not
// need to.
void MetadataRestoreController::stageOne(int index, int itemCountHint)
{
    const auto &proposals = m_model.proposals();
    if (index < 0 || static_cast<std::size_t>(index) >= proposals.size()) {
        return;
    }
    const MetadataRestoreProposal &proposal = proposals[static_cast<std::size_t>(index)];
    if (m_stagedByStoredId.count(proposal.storedId)) {
        return;
    }
    if (!proposal.offersAnything()) {
        return;
    }
    if (!m_session) {
        attachSession();
        if (!m_session) {
            setErrorMessage(QStringLiteral("This stick's library could not be identified; nothing was changed."));
            return;
        }
    }

    // One change per catalog that lists this file. OneLibrary is skipped
    // when rekordbox is present, because the rekordbox change already
    // mirrors into it -- staging both would write the same cues twice
    // and report the track as two restores.
    bool hasRekordbox = false;
    for (const auto &row : proposal.stickTrack.catalogRows) {
        if (row.format == "rekordbox") {
            hasRekordbox = true;
        }
    }

    QStringList staged;
    for (const auto &row : proposal.stickTrack.catalogRows) {
        if (row.format == "onelibrary" && hasRekordbox) {
            continue;
        }
        auto change = std::make_unique<RestoreMetadataChange>(
            QString::fromStdString(row.format), catalogPathForFormat(m_libraryPath, row.format),
            QString::fromStdString(row.sourceId), proposal, itemCountHint);
        const QString changeId = change->id();
        if (!m_session->stage(std::move(change))) {
            // Refused: the session reports why and the page shows it.
            // Anything already staged for this track stays staged --
            // undoing a partial batch here would be a second, silent
            // decision on top of the refusal.
            break;
        }
        staged << changeId;
    }
    if (staged.isEmpty()) {
        return;
    }
    m_stagedByStoredId[proposal.storedId] = staged;
    m_model.setStaged(index, true);
    emit analysisChanged();
}

void MetadataRestoreController::stageAll()
{
    if (writing()) {
        emit actionFeedback(QStringLiteral("A save is running. Stage more once it has finished."), true);
        return;
    }
    const int count = static_cast<int>(m_model.proposals().size());
    int staged = 0;
    for (int i = 0; i < count; ++i) {
        if (m_stagedByStoredId.count(m_model.proposals()[static_cast<std::size_t>(i)].storedId)) {
            continue;
        }
        // Every remaining proposal is about to be staged, so this is
        // the same upper bound arrived at exactly.
        stageOne(i, count);
        if (m_session && !m_session->lockHeld()) {
            return;  // refused at the first one; no point trying the rest
        }
        staged++;
    }
    if (staged > 0) {
        emit actionFeedback(QStringLiteral("Staged %1 track(s). Press Save to write them to the stick.").arg(staged),
                            false);
    }
}

void MetadataRestoreController::unstage(int index)
{
    const auto &proposals = m_model.proposals();
    if (index < 0 || static_cast<std::size_t>(index) >= proposals.size()) {
        return;
    }
    auto it = m_stagedByStoredId.find(proposals[static_cast<std::size_t>(index)].storedId);
    if (it == m_stagedByStoredId.end()) {
        return;
    }
    if (m_session) {
        for (const auto &changeId : it->second) {
            m_session->unstage(changeId);
        }
    }
    m_stagedByStoredId.erase(it);
    m_model.setStaged(index, false);
    emit analysisChanged();
}

std::shared_ptr<QtProgressReporter> MetadataRestoreController::makeReporter()
{
    auto reporter = std::make_shared<QtProgressReporter>();
    connect(reporter.get(), &QtProgressReporter::started, this, [this](const QString &label, int total) {
        m_phaseBaseline += m_currentPhaseTotal;
        m_currentPhaseTotal = total;
        setCurrentPhase(label);
        setProgress(m_phaseBaseline, m_phaseBaseline + total);
    });
    connect(reporter.get(), &QtProgressReporter::progressed, this,
            [this](int current) { setProgress(m_phaseBaseline + current, m_phaseBaseline + m_currentPhaseTotal); });
    return reporter;
}

void MetadataRestoreController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void MetadataRestoreController::setProgress(int current, int total)
{
    if (m_progressCurrent == current && m_progressTotal == total) {
        return;
    }
    m_progressCurrent = current;
    m_progressTotal = total;
    emit progressChanged();
}

void MetadataRestoreController::setCurrentPhase(const QString &phase)
{
    if (m_currentPhase == phase) {
        return;
    }
    m_currentPhase = phase;
    emit currentPhaseChanged();
}

void MetadataRestoreController::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

}  // namespace seabass::gui
