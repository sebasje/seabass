#pragma once

#include <QAbstractListModel>
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

// Wraps ScanLibrary for QML: reads every track (with cues) out of a
// rekordbox or Engine library path, exposed as `tracks`.
class ScanController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(djconvert::gui::TrackListModel *tracks READ tracksModel CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QStringList playlistNames READ playlistNames NOTIFY playlistNamesChanged)

public:
    explicit ScanController(QObject *parent = nullptr);

    TrackListModel *tracksModel() { return &m_model; }
    bool busy() const { return m_busy; }
    QString errorMessage() const { return m_errorMessage; }
    QStringList playlistNames() const { return m_playlistNames; }

    // format is "rekordbox" or "engine"; path is the corresponding
    // DetectedStick.rekordboxPath / .enginePath. siblingRekordboxPath (only
    // used when format is "engine") lets Engine tracks borrow cover art
    // from the same song's rekordbox copy, matched by title+artist, since
    // libdjinterop has no usable cover art API of its own.
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
    void errorMessageChanged();
    void playlistNamesChanged();

private:
    void setBusy(bool busy);
    void setErrorMessage(const QString &message);
    void applyFilters();

    TrackListModel m_model;
    std::vector<domain::Track> m_allTracks;
    QStringList m_playlistNames;
    QString m_currentPlaylistFilter;
    QString m_currentSearchQuery;
    QString m_sortField = "playlist";
    bool m_sortAscending = true;
    bool m_busy = false;
    QString m_errorMessage;
};

}  // namespace djconvert::gui
