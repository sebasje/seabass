#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QVariantList>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "application/ports/cancellation_token.hpp"
#include "domain/metadata_restore.hpp"
#include "gui/qt_progress_reporter.hpp"

namespace seabass::gui
{

class LibraryEditSession;

// Read-only model over the proposals the controller last planned.
class RestoreProposalListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Populated by MetadataRestoreController; not constructible from QML")

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        FilenameRole,
        CueCountRole,      // how many cues the write would leave on the track
        CuesAddedRole,     // how many of those are new
        FillsAGapRole,     // the track has no cues at all today
        ConflictRole,      // the track has cues and they differ
        RatingRole,        // the rating this restore would write, -1 for none
        CommentRole,       // the comment it would write, empty for none
        StagedRole,
    };

    explicit RestoreProposalListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setProposals(std::vector<domain::MetadataRestoreProposal> proposals);
    const std::vector<domain::MetadataRestoreProposal> &proposals() const { return m_proposals; }
    void setStaged(int index, bool staged);
    void removeAt(int index);
    int indexOfStoredId(const std::string &storedId) const;

private:
    std::vector<domain::MetadataRestoreProposal> m_proposals;
    std::vector<bool> m_staged;  // parallel to m_proposals
};

struct MetadataRestoreTaskResult
{
    std::vector<domain::MetadataRestoreProposal> proposals;
    int stickTrackCount = 0;
    int storedTrackCount = 0;
    int conflictCount = 0;  // tracks whose cues differ, whatever the policy did with them
    QString errorMessage;
    bool cancelled = false;
};

// Puts the local metadata store's cues back on a stick that has lost
// them. See docs/metadata-backup-plan.md.
//
// The counterpart to MetadataBackupController, and deliberately not the
// same page: reading a stick into the store risks nothing and needs no
// confirmation, while writing to the stick is a save like every other
// one in Seabass. So nothing here writes directly. Each accepted
// proposal is staged into the library's LibraryEditSession as a
// RestoreMetadataChange, the page's Save writes them through
// runSaveLoop, and that backs up before it touches a file.
class MetadataRestoreController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool writing READ writing NOTIFY writingChanged)
    Q_PROPERTY(int progressCurrent READ progressCurrent NOTIFY progressChanged)
    Q_PROPERTY(int progressTotal READ progressTotal NOTIFY progressChanged)
    Q_PROPERTY(QString currentPhase READ currentPhase NOTIFY currentPhaseChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(RestoreProposalListModel *proposals READ proposals CONSTANT)
    Q_PROPERTY(bool hasScanned READ hasScanned NOTIFY analysisChanged)
    Q_PROPERTY(int stickTrackCount READ stickTrackCount NOTIFY analysisChanged)
    Q_PROPERTY(int storedTrackCount READ storedTrackCount NOTIFY analysisChanged)
    Q_PROPERTY(int conflictCount READ conflictCount NOTIFY analysisChanged)
    Q_PROPERTY(int stagedCount READ stagedCount NOTIFY analysisChanged)
    // Proposals offering a comment that DeviceLibrary alone cannot
    // store. Not a failure and not hidden: the page says so before the
    // save rather than the log saying so after it.
    Q_PROPERTY(int commentsRekordboxCannotTake READ commentsRekordboxCannotTake NOTIFY analysisChanged)

public:
    explicit MetadataRestoreController(QObject *parent = nullptr);
    ~MetadataRestoreController() override;

    bool busy() const { return m_busy; }
    bool writing() const;
    int progressCurrent() const { return m_progressCurrent; }
    int progressTotal() const { return m_progressTotal; }
    QString currentPhase() const { return m_currentPhase; }
    QString errorMessage() const { return m_errorMessage; }
    RestoreProposalListModel *proposals() { return &m_model; }
    bool hasScanned() const { return m_hasScanned; }
    int stickTrackCount() const { return m_stickTrackCount; }
    int storedTrackCount() const { return m_storedTrackCount; }
    int conflictCount() const { return m_conflictCount; }
    int stagedCount() const { return static_cast<int>(m_stagedByStoredId.size()); }
    int commentsRekordboxCannotTake() const { return m_commentsRekordboxCannotTake; }

    // libraryPath is any catalog directory on the stick; every catalog
    // on it is read and folded into files first, so one proposal covers
    // a track however many formats list it.
    Q_INVOKABLE void scan(const QString &libraryPath, bool overwriteConflicts);
    Q_INVOKABLE void cancelScan();
    Q_INVOKABLE void stage(int index);
    // stage(), plus what the save is expected to write in total -- see
    // RestoreMetadataChange's own itemCountHint.
    void stageOne(int index, int itemCountHint);
    Q_INVOKABLE void stageAll();
    Q_INVOKABLE void unstage(int index);

signals:
    void busyChanged();
    void writingChanged();
    void progressChanged();
    void currentPhaseChanged();
    void errorMessageChanged();
    void analysisChanged();
    void canUndoChanged();
    void actionFeedback(const QString &message, bool isError);

private:
    void onScanFinished();
    void attachSession();
    void setBusy(bool busy);
    void setProgress(int current, int total);
    void setCurrentPhase(const QString &phase);
    void setErrorMessage(const QString &message);
    std::shared_ptr<QtProgressReporter> makeReporter();

    QFutureWatcher<MetadataRestoreTaskResult> m_watcher;
    RestoreProposalListModel m_model;
    QPointer<LibraryEditSession> m_session;
    application::CancellationToken m_cancel;
    // One proposal can stage several changes, one per catalog that lists
    // the track, so this maps a proposal to all of them.
    std::map<std::string, QStringList> m_stagedByStoredId;

    QString m_libraryPath;
    bool m_busy = false;
    bool m_hasScanned = false;
    int m_progressCurrent = 0;
    int m_progressTotal = 0;
    int m_phaseBaseline = 0;
    int m_currentPhaseTotal = 0;
    int m_stickTrackCount = 0;
    int m_storedTrackCount = 0;
    int m_conflictCount = 0;
    int m_commentsRekordboxCannotTake = 0;
    QString m_currentPhase;
    QString m_errorMessage;
};

}  // namespace seabass::gui
