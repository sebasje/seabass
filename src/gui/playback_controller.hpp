#pragma once

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QObject>
#include <QQmlEngine>
#include <QVariantList>

namespace djconvert::gui
{

// Plays a single track's audio file (via QMediaPlayer) and exposes its
// best-effort waveform preview for display, read on demand from whichever
// format the track came from -- never during a bulk library scan, since
// waveform decoding needs its own file I/O per track.
class PlaybackController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY trackChanged)
    Q_PROPERTY(QString title READ title NOTIFY trackChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY trackChanged)
    Q_PROPERTY(QVariantList waveform READ waveform NOTIFY trackChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    explicit PlaybackController(QObject *parent = nullptr);

    bool hasTrack() const { return m_hasTrack; }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QVariantList waveform() const { return m_waveform; }
    qint64 duration() const { return m_player.duration(); }
    qint64 position() const { return m_player.position(); }
    bool playing() const { return m_player.playbackState() == QMediaPlayer::PlayingState; }
    QString errorMessage() const { return m_errorMessage; }

    // format is "rekordbox" or "engine"; libraryPath is the corresponding
    // DetectedStick.rekordboxPath / .enginePath; sourceId/filePath/title/
    // artist come straight from the track row being played.
    Q_INVOKABLE void load(const QString &format, const QString &libraryPath, const QString &sourceId,
                           const QString &filePath, const QString &title, const QString &artist);
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE void stop();

signals:
    void trackChanged();
    void durationChanged();
    void positionChanged();
    void playingChanged();
    void errorMessageChanged();

private:
    void setErrorMessage(const QString &message);

    QMediaPlayer m_player;
    QAudioOutput m_audioOutput;
    bool m_hasTrack = false;
    QString m_title;
    QString m_artist;
    QVariantList m_waveform;
    QString m_errorMessage;
};

}  // namespace djconvert::gui
