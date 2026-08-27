#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>

#include <vector>

#include "domain/track.hpp"

namespace djconvert::gui
{

// Read-only Qt list model over the tracks ScanController last read.
class TrackListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Populated by ScanController; not constructible from QML")

public:
    enum Roles {
        SourceIdRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        DurationSecondsRole,
        CueCountRole,
        PlayCountRole,
        FilePathRole,
        ArtworkPathRole,
        BpmRole,
        KeyRole,
        CuesRole,
    };

    explicit TrackListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setTracks(std::vector<domain::Track> tracks);

private:
    std::vector<domain::Track> m_tracks;
};

// Result of a background scan task -- see ScanController::scan(). Kept
// separate from ScanController's own state since it's built entirely on a
// worker thread, with no access to the controller itself.
struct ScanTaskResult
{
    std::vector<domain::Track> tracks;
    QString errorMessage;  // empty on success
};

// Wraps ScanLibrary for QML: reads every track (with cues) out of a
// rekordbox or Engine library path, exposed as `tracks`. The read itself
// runs on a background thread (via QtConcurrent) since it can take several
// seconds for a large library -- scan() returns immediately, and `busy`/
// `scanCurrent`/`scanTotal` track progress for a UI-thread progress bar
// that can actually render while the work happens.
class ScanController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(djconvert::gui::TrackListModel *tracks READ tracksModel CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int scanCurrent READ scanCurrent NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanProgressChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QStringList playlistNames READ playlistNames NOTIFY playlistNamesChanged)

public:
    explicit ScanController(QObject *parent = nullptr);

    TrackListModel *tracksModel() { return &m_model; }
    bool busy() const { return m_busy; }
    int scanCurrent() const { return m_scanCurrent; }
    int scanTotal() const { return m_scanTotal; }
    QString errorMessage() const { return m_errorMessage; }
    QStringList playlistNames() const { return m_playlistNames; }

    // format is "rekordbox" or "engine"; path is the corresponding
    // DetectedStick.rekordboxPath / .enginePath. siblingRekordboxPath (only
    // used when format is "engine") lets Engine tracks borrow cover art
    // from the same song's rekordbox copy, matched by title+artist, since
    // libdjinterop has no usable cover art API of its own. A scan already
    // in progress is ignored rather than overlapped.
    Q_INVOKABLE void scan(const QString &format, const QString &path, const QString &siblingRekordboxPath = QString());

    // playlistName empty shows every scanned track again.
    Q_INVOKABLE void filterByPlaylist(const QString &playlistName);

    // Matches against title or artist, case-insensitive substring. Empty
    // clears the search.
    Q_INVOKABLE void search(const QString &query);

    // field is one of "playlist" (the selected playlist's own order, or
    // scan order when no playlist is selected), "title", "artist", "key",
    // "bpm", "duration", "cues", "plays".
    Q_INVOKABLE void setSort(const QString &field, bool ascending);

signals:
    void busyChanged();
    void scanProgressChanged();
    void errorMessageChanged();
    void playlistNamesChanged();

private:
    void setBusy(bool busy);
    void setScanProgress(int current, int total);
    void setErrorMessage(const QString &message);
    void applyFilters();
    void onScanFinished();

    TrackListModel m_model;
    QFutureWatcher<ScanTaskResult> m_watcher;
    std::vector<domain::Track> m_allTracks;
    QStringList m_playlistNames;
    QString m_currentPlaylistFilter;
    QString m_currentSearchQuery;
    QString m_sortField = "playlist";
    bool m_sortAscending = true;
    bool m_busy = false;
    int m_scanCurrent = 0;
    int m_scanTotal = 0;
    QString m_errorMessage;
};

}  // namespace djconvert::gui
