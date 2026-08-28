#include "playback_controller.hpp"

#include <QFile>
#include <QUrl>
#include <QVariantMap>

#include "infrastructure/engine/libdjinterop_waveform_reader.hpp"
#include "infrastructure/rekordbox/rekordbox_waveform_reader.hpp"

namespace djconvert::gui
{

namespace
{

QVariantList readWaveform(const QString &format, const QString &libraryPath, const QString &sourceId)
{
    std::vector<domain::WaveformColumn> points;
    if (format == "rekordbox") {
        points = infrastructure::rekordbox::readWaveformPreview(libraryPath.toStdString(), sourceId.toStdString());
    } else {
        points = infrastructure::engine::readWaveformPreview(libraryPath.toStdString(), sourceId.toStdString());
    }
    QVariantList waveform;
    for (const auto &col : points) {
        QVariantMap m;
        m["low"] = col.low;
        m["mid"] = col.mid;
        m["high"] = col.high;
        waveform.append(m);
    }
    return waveform;
}

}  // namespace

PlaybackController::PlaybackController(QObject *parent) : QObject(parent)
{
    m_player.setAudioOutput(&m_audioOutput);

    connect(&m_player, &QMediaPlayer::durationChanged, this, &PlaybackController::durationChanged);
    connect(&m_player, &QMediaPlayer::positionChanged, this, &PlaybackController::positionChanged);
    connect(&m_player, &QMediaPlayer::playbackStateChanged, this, &PlaybackController::playingChanged);
    connect(&m_audioOutput, &QAudioOutput::volumeChanged, this, &PlaybackController::volumeChanged);
    connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &message) {
        setErrorMessage(message);
    });
}

void PlaybackController::setVolume(qreal volume)
{
    m_audioOutput.setVolume(static_cast<float>(qBound(0.0, volume, 1.0)));
}

void PlaybackController::load(const QString &format, const QString &libraryPath, const QString &sourceId,
                               const QString &filePath, const QString &title, const QString &artist,
                               const QString &artworkPath, const QVariantList &cues)
{
    m_player.stop();
    setErrorMessage({});
    m_waveform.clear();

    m_title = title;
    m_artist = artist;
    m_artworkPath = artworkPath;
    m_cues = cues;
    m_hasTrack = true;

    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        setErrorMessage("audio file not found" + (filePath.isEmpty() ? QString() : (": " + filePath)));
    } else {
        m_waveform = readWaveform(format, libraryPath, sourceId);
        m_player.setSource(QUrl::fromLocalFile(filePath));
        m_player.play();
    }

    emit trackChanged();
}

QVariantList PlaybackController::waveformFor(const QString &format, const QString &libraryPath,
                                              const QString &sourceId) const
{
    return readWaveform(format, libraryPath, sourceId);
}

void PlaybackController::togglePlay()
{
    if (!m_hasTrack) {
        return;
    }
    if (playing()) {
        m_player.pause();
    } else {
        m_player.play();
    }
}

void PlaybackController::seek(qint64 positionMs)
{
    m_player.setPosition(positionMs);
}

void PlaybackController::stop()
{
    m_player.stop();
    m_hasTrack = false;
    m_waveform.clear();
    m_cues.clear();
    emit trackChanged();
}

void PlaybackController::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

}  // namespace djconvert::gui
