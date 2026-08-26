#include "playback_controller.hpp"

#include <QFile>
#include <QUrl>

#include "infrastructure/engine/libdjinterop_waveform_reader.hpp"
#include "infrastructure/rekordbox/rekordbox_waveform_reader.hpp"

namespace djconvert::gui
{

PlaybackController::PlaybackController(QObject *parent) : QObject(parent)
{
    m_player.setAudioOutput(&m_audioOutput);

    connect(&m_player, &QMediaPlayer::durationChanged, this, &PlaybackController::durationChanged);
    connect(&m_player, &QMediaPlayer::positionChanged, this, &PlaybackController::positionChanged);
    connect(&m_player, &QMediaPlayer::playbackStateChanged, this, &PlaybackController::playingChanged);
    connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &message) {
        setErrorMessage(message);
    });
}

void PlaybackController::load(const QString &format, const QString &libraryPath, const QString &sourceId,
                               const QString &filePath, const QString &title, const QString &artist)
{
    m_player.stop();
    setErrorMessage({});
    m_waveform.clear();

    m_title = title;
    m_artist = artist;
    m_hasTrack = true;

    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        setErrorMessage("audio file not found" + (filePath.isEmpty() ? QString() : (": " + filePath)));
    } else {
        std::vector<double> points;
        if (format == "rekordbox") {
            points = infrastructure::rekordbox::readWaveformPreview(libraryPath.toStdString(), sourceId.toStdString());
        } else {
            points = infrastructure::engine::readWaveformPreview(libraryPath.toStdString(), sourceId.toStdString());
        }
        for (double v : points) {
            m_waveform.append(v);
        }

        m_player.setSource(QUrl::fromLocalFile(filePath));
        m_player.play();
    }

    emit trackChanged();
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
