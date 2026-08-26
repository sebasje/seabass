#include "scan_controller.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <set>
#include <unordered_map>

#include "application/use_cases/scan_library.hpp"
#include "domain/track_matching.hpp"
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
    case CuesRole: {
        QVariantList cues;
        for (const auto &c : track.cues) {
            QVariantMap m;
            m["kind"] = c.kind == domain::CuePoint::Kind::Hot ? QStringLiteral("hot") : QStringLiteral("memory");
            m["hotCueNumber"] = c.hotCueNumber;
            m["positionMs"] = c.positionMs;
            m["color"] = QString::fromStdString(c.color);
            cues << m;
        }
        return cues;
    }
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
        {CuesRole, "cues"},
    };
}

void TrackListModel::setTracks(std::vector<domain::Track> tracks)
{
    beginResetModel();
    m_tracks = std::move(tracks);
    endResetModel();
}

ScanController::ScanController(QObject *parent) : QObject(parent) {}

void ScanController::scan(const QString &format, const QString &path, const QString &siblingRekordboxPath)
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

            if (!siblingRekordboxPath.isEmpty()) {
                try {
                    infrastructure::rekordbox::KaitaiRekordboxReader rbReader(siblingRekordboxPath.toStdString());
                    application::ScanLibrary rbUseCase(rbReader);
                    auto rbTracks = rbUseCase.execute();

                    std::unordered_map<std::string, std::string> artworkByTitleArtist;
                    for (const auto &rbTrack : rbTracks) {
                        if (rbTrack.artworkPath.empty() || rbTrack.title.empty() || rbTrack.artist.empty()) {
                            continue;
                        }
                        artworkByTitleArtist[domain::normalizeFilename(rbTrack.title + "|" + rbTrack.artist)] =
                            rbTrack.artworkPath;
                    }

                    for (auto &track : tracks) {
                        if (!track.artworkPath.empty() || track.title.empty() || track.artist.empty()) {
                            continue;
                        }
                        auto it = artworkByTitleArtist.find(domain::normalizeFilename(track.title + "|" + track.artist));
                        if (it != artworkByTitleArtist.end()) {
                            track.artworkPath = it->second;
                        }
                    }
                } catch (const std::exception &) {
                    // Borrowing cover art from the sibling library is a
                    // nice-to-have -- never let it break browsing Engine
                    // tracks on their own.
                }
            }
        }
        std::set<std::string> uniquePlaylistNames;
        for (const auto &track : tracks) {
            for (const auto &playlist : track.playlists) {
                uniquePlaylistNames.insert(playlist.name);
            }
        }
        m_playlistNames.clear();
        for (const auto &name : uniquePlaylistNames) {
            m_playlistNames << QString::fromStdString(name);
        }
        emit playlistNamesChanged();

        m_allTracks = std::move(tracks);
        m_currentPlaylistFilter.clear();
        m_currentSearchQuery.clear();
        applyFilters();
    } catch (const std::exception &e) {
        setErrorMessage(QString::fromStdString(e.what()));
    }

    setBusy(false);
}

void ScanController::filterByPlaylist(const QString &playlistName)
{
    m_currentPlaylistFilter = playlistName;
    applyFilters();
}

void ScanController::search(const QString &query)
{
    m_currentSearchQuery = query;
    applyFilters();
}

void ScanController::setSort(const QString &field, bool ascending)
{
    m_sortField = field;
    m_sortAscending = ascending;
    applyFilters();
}

namespace
{

std::optional<int> playlistPosition(const domain::Track &track, const std::string &playlistName)
{
    for (const auto &p : track.playlists) {
        if (p.name == playlistName) {
            return p.position;
        }
    }
    return std::nullopt;
}

}  // namespace

void ScanController::applyFilters()
{
    std::vector<domain::Track> result = m_allTracks;

    std::string playlistFilter = m_currentPlaylistFilter.toStdString();
    if (!m_currentPlaylistFilter.isEmpty()) {
        std::vector<domain::Track> filtered;
        for (const auto &track : result) {
            if (playlistPosition(track, playlistFilter)) {
                filtered.push_back(track);
            }
        }
        result = std::move(filtered);
    }

    if (!m_currentSearchQuery.isEmpty()) {
        QString query = m_currentSearchQuery.toLower();
        std::vector<domain::Track> filtered;
        for (const auto &track : result) {
            QString title = QString::fromStdString(track.title).toLower();
            QString artist = QString::fromStdString(track.artist).toLower();
            if (title.contains(query) || artist.contains(query)) {
                filtered.push_back(track);
            }
        }
        result = std::move(filtered);
    }

    std::stable_sort(result.begin(), result.end(), [&](const domain::Track &a, const domain::Track &b) {
        bool less;
        if (m_sortField == "title") {
            less = a.title < b.title;
        } else if (m_sortField == "artist") {
            less = a.artist < b.artist;
        } else if (m_sortField == "key") {
            less = a.key < b.key;
        } else if (m_sortField == "bpm") {
            less = a.bpm < b.bpm;
        } else if (m_sortField == "duration") {
            less = a.durationSeconds < b.durationSeconds;
        } else if (m_sortField == "cues") {
            less = a.cues.size() < b.cues.size();
        } else if (m_sortField == "plays") {
            less = a.playCount.value_or(-1) < b.playCount.value_or(-1);
        } else {
            // "playlist" -- the selected playlist's own track order; when
            // no playlist is selected this leaves every position unknown,
            // so stable_sort just preserves scan order.
            auto posA = playlistPosition(a, playlistFilter);
            auto posB = playlistPosition(b, playlistFilter);
            less = posA.value_or(std::numeric_limits<int>::max()) < posB.value_or(std::numeric_limits<int>::max());
        }
        return m_sortAscending ? less : !less;
    });

    m_model.setTracks(std::move(result));
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
