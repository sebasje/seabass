#include "domain/track_scope.hpp"

#include <algorithm>
#include <cctype>

namespace seabass::domain
{

namespace
{

std::string toLowerAscii(const std::string &s)
{
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

bool inPlaylist(const Track &track, const std::string &playlistName)
{
    for (const auto &membership : track.playlists) {
        if (membership.name == playlistName) {
            return true;
        }
    }
    return false;
}

}  // namespace

TrackScope TrackScope::all()
{
    TrackScope scope;
    scope.m_kind = Kind::All;
    return scope;
}

TrackScope TrackScope::playlist(std::string name)
{
    TrackScope scope;
    scope.m_kind = Kind::Playlist;
    scope.m_value = std::move(name);
    return scope;
}

TrackScope TrackScope::search(std::string query)
{
    TrackScope scope;
    scope.m_kind = Kind::Search;
    // Lowercased once here rather than per-track in matches() below --
    // the query itself never changes for the lifetime of this scope, only
    // title/artist (which do still need lowering per track) vary.
    scope.m_value = toLowerAscii(query);
    return scope;
}

TrackScope TrackScope::arbitrary(std::set<TrackId> ids)
{
    TrackScope scope;
    scope.m_kind = Kind::Arbitrary;
    scope.m_ids = std::move(ids);
    return scope;
}

bool TrackScope::matches(const Track &track) const
{
    switch (m_kind) {
        case Kind::All:
            return true;
        case Kind::Playlist:
            return inPlaylist(track, m_value);
        case Kind::Search:
            // m_value is already lowercased -- see TrackScope::search().
            return toLowerAscii(track.title).find(m_value) != std::string::npos
                || toLowerAscii(track.artist).find(m_value) != std::string::npos;
        case Kind::Arbitrary:
            return m_ids.count(TrackId{track.format, track.sourceId}) > 0;
    }
    return false;
}

std::vector<Track> filterByScope(const std::vector<Track> &tracks, const TrackScope &scope)
{
    std::vector<Track> result;
    result.reserve(tracks.size());
    for (const auto &track : tracks) {
        if (scope.matches(track)) {
            result.push_back(track);
        }
    }
    return result;
}

}  // namespace seabass::domain
