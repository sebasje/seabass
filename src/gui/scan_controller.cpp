#include "scan_controller.hpp"

#include <algorithm>
#include <set>

#include "application/use_cases/scan_library.hpp"
#include "infrastructure/engine/libdjinterop_engine_reader.hpp"
#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"

namespace djconvert::gui
{

TrackListModel::TrackListModel(QObject *parent) : QAbstractListModel(parent) {}

int TrackListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_tracks.size());
}

QVariant TrackListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= m_tracks.size()) {
        return {};
    }
    const auto &track = m_tracks[static_cast<size_t>(index.row())];
    switch (role) {
    case SourceIdRole:
        return QString::fromStdString(track.sourceId);
    case TitleRole:
        return QString::fromStdString(track.title);
    case ArtistRole:
        return QString::fromStdString(track.artist);
    case DurationSecondsRole:
        return track.durationSeconds;
    case CueCountRole:
        return static_cast<int>(track.cues.size());
    case PlayCountRole:
        return track.playCount ? *track.playCount : -1;
    case FilePathRole:
        return QString::fromStdString(track.filePath);
    case ArtworkPathRole:
        return QString::fromStdString(track.artworkPath);
    case BpmRole:
        return track.bpm;
    case KeyRole:
        return QString::fromStdString(track.key);
    default:
        return {};
    }
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    return {
        {SourceIdRole, "sourceId"},
        {TitleRole, "title"},
        {ArtistRole, "artist"},
        {DurationSecondsRole, "durationSeconds"},
        {CueCountRole, "cueCount"},
        {PlayCountRole, "playCount"},
        {FilePathRole, "filePath"},
        {ArtworkPathRole, "artworkPath"},
        {BpmRole, "bpm"},
        {KeyRole, "key"},
    };
}

void TrackListModel::setTracks(std::vector<domain::Track> tracks)
{
    beginResetModel();
    m_tracks = std::move(tracks);
    endResetModel();
}

ScanController::ScanController(QObject *parent) : QObject(parent) {}

void ScanController::scan(const QString &format, const QString &path)
{
    setErrorMessage({});
    setBusy(true);

    try {
        std::vector<domain::Track> tracks;
        if (format == "rekordbox") {
            infrastructure::rekordbox::KaitaiRekordboxReader reader(path.toStdString());
            application::ScanLibrary useCase(reader);
            tracks = useCase.execute();
        } else if (format == "engine") {
            infrastructure::engine::LibdjinteropEngineReader reader(path.toStdString());
            application::ScanLibrary useCase(reader);
            tracks = useCase.execute();
        }
        std::set<std::string> uniquePlaylistNames;
        for (const auto &track : tracks) {
            for (const auto &playlist : track.playlists) {
                uniquePlaylistNames.insert(playlist);
            }
        }
        m_playlistNames.clear();
        for (const auto &name : uniquePlaylistNames) {
            m_playlistNames << QString::fromStdString(name);
        }
        emit playlistNamesChanged();

        m_allTracks = tracks;
        m_model.setTracks(std::move(tracks));
    } catch (const std::exception &e) {
        setErrorMessage(QString::fromStdString(e.what()));
    }

    setBusy(false);
}

void ScanController::filterByPlaylist(const QString &playlistName)
{
    if (playlistName.isEmpty()) {
        m_model.setTracks(m_allTracks);
        return;
    }

    std::string target = playlistName.toStdString();
    std::vector<domain::Track> filtered;
    for (const auto &track : m_allTracks) {
        if (std::find(track.playlists.begin(), track.playlists.end(), target) != track.playlists.end()) {
            filtered.push_back(track);
        }
    }
    m_model.setTracks(std::move(filtered));
}

void ScanController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void ScanController::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

}  // namespace djconvert::gui
