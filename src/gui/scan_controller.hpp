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

public:
    explicit ScanController(QObject *parent = nullptr);

    TrackListModel *tracksModel() { return &m_model; }
    bool busy() const { return m_busy; }
    QString errorMessage() const { return m_errorMessage; }

    // format is "rekordbox" or "engine"; path is the corresponding
    // DetectedStick.rekordboxPath / .enginePath.
    Q_INVOKABLE void scan(const QString &format, const QString &path);

signals:
    void busyChanged();
    void errorMessageChanged();

private:
    void setBusy(bool busy);
    void setErrorMessage(const QString &message);

    TrackListModel m_model;
    bool m_busy = false;
    QString m_errorMessage;
};

}  // namespace djconvert::gui
