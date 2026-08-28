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
    Q_PROPERTY(QString artworkPath READ artworkPath NOTIFY trackChanged)
    Q_PROPERTY(QVariantList waveform READ waveform NOTIFY trackChanged)
    Q_PROPERTY(QVariantList cues READ cues NOTIFY trackChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    explicit PlaybackController(QObject *parent = nullptr);

    bool hasTrack() const { return m_hasTrack; }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QString artworkPath() const { return m_artworkPath; }
    QVariantList waveform() const { return m_waveform; }
    QVariantList cues() const { return m_cues; }
    qint64 duration() const { return m_player.duration(); }
    qint64 position() const { return m_player.position(); }
    bool playing() const { return m_player.playbackState() == QMediaPlayer::PlayingState; }
    // Linear gain, 0.0 (silent) to 1.0 (unattenuated) -- matches
    // QAudioOutput::volume()'s own scale directly, no remapping.
    qreal volume() const { return m_audioOutput.volume(); }
    void setVolume(qreal volume);
    QString errorMessage() const { return m_errorMessage; }

    // format is "rekordbox" or "engine"; libraryPath is the corresponding
    // DetectedStick.rekordboxPath / .enginePath; sourceId/filePath/title/
    // artist come straight from the track row being played.
    Q_INVOKABLE void load(const QString &format, const QString &libraryPath, const QString &sourceId,
                           const QString &filePath, const QString &title, const QString &artist,
                           const QString &artworkPath, const QVariantList &cues);

    // Reads a track's waveform preview without touching playback state --
    // for a read-only preview (e.g. the Library page's per-track info
    // popup) that shouldn't interrupt whatever's currently loaded/playing.
    // Same on-demand, synchronous-on-the-UI-thread read as load() itself
    // uses; a single track's preview blob is small enough that this
    // hasn't needed backgrounding so far (see load()'s own doc comment).
    Q_INVOKABLE QVariantList waveformFor(const QString &format, const QString &libraryPath,
                                          const QString &sourceId) const;

    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE void stop();

signals:
    void trackChanged();
    void durationChanged();
    void positionChanged();
    void playingChanged();
    void volumeChanged();
    void errorMessageChanged();

private:
    void setErrorMessage(const QString &message);

    QMediaPlayer m_player;
    QAudioOutput m_audioOutput;
    bool m_hasTrack = false;
    QString m_title;
    QString m_artist;
    QString m_artworkPath;
    QVariantList m_waveform;
    QVariantList m_cues;
    QString m_errorMessage;
};

}  // namespace djconvert::gui
