#include "infrastructure/engine/libdjinterop_engine_reader.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include <djinterop/djinterop.hpp>

namespace djconvert::infrastructure::engine
{

namespace application = djconvert::application;
namespace domain = djconvert::domain;

namespace
{

std::string colorHex(const djinterop::pad_color &c)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
    return buf;
}

// Some individual fields on some tracks (observed: sample_rate() on a
// track just re-cued on real Denon hardware) throw a decode error from
// libdjinterop even when the rest of the track -- including hot_cues() --
// reads fine. Isolating each field like this means one flaky field never
// costs us the whole track, which a single try/catch around everything
// used to do.
template<typename T, typename Fn>
T safeGet(application::ProgressReporter &progress, int64_t trackId, const char *fieldName, Fn &&fn, T fallback = T{})
{
    try {
        return fn();
    } catch (const std::exception &e) {
        progress.warn("track id=" + std::to_string(trackId) + ": " + fieldName + " unreadable (" + e.what() + ")");
        return fallback;
    }
}

// Recursively walks a playlist tree, recording each track's full playlist
// path(s) (e.g. "Techno/Peak Time"). Best-effort: any failure here just
// leaves playlists empty rather than failing the whole scan, since
// membership is supplementary information, not core track data.
void collectPlaylistMemberships(const djinterop::playlist &pl, const std::string &pathPrefix,
                                 std::unordered_map<int64_t, std::vector<domain::PlaylistMembership>> &membership)
{
    std::string path = pathPrefix.empty() ? pl.name() : pathPrefix + "/" + pl.name();
    int position = 0;
    for (const auto &tr : pl.tracks()) {
        membership[tr.id()].push_back(domain::PlaylistMembership{path, position++});
    }
    for (const auto &child : pl.children()) {
        collectPlaylistMemberships(child, path, membership);
    }
}

}  // namespace

LibdjinteropEngineReader::LibdjinteropEngineReader(std::string engineLibraryPath)
    : m_engineLibraryPath(std::move(engineLibraryPath))
{
}

std::vector<domain::Track> LibdjinteropEngineReader::readAll()
{
    if (!djinterop::engine::database_exists(m_engineLibraryPath)) {
        throw std::runtime_error("no Engine Library found at " + m_engineLibraryPath);
    }

    auto db = djinterop::engine::load_database(m_engineLibraryPath);
    auto allTracks = db.tracks();

    std::unordered_map<int64_t, std::vector<domain::PlaylistMembership>> playlistsByTrackId;
    try {
        for (const auto &root : db.root_playlists()) {
            collectPlaylistMemberships(root, "", playlistsByTrackId);
        }
    } catch (const std::exception &e) {
        m_progress->warn(std::string("could not read playlists: ") + e.what());
    }

    m_progress->start("Scanning Engine tracks", allTracks.size());
    size_t processed = 0;

    std::vector<domain::Track> tracks;
    for (auto &tr : allTracks) {
        int64_t id = tr.id();
        domain::Track track;
        track.sourceId = std::to_string(id);
        track.title = safeGet<std::string>(*m_progress, id, "title", [&] { return tr.title().value_or(""); });
        track.artist = safeGet<std::string>(*m_progress, id, "artist", [&] { return tr.artist().value_or(""); });
        track.filename = safeGet<std::string>(*m_progress, id, "filename", [&] { return tr.filename(); });
        track.filePath = safeGet<std::string>(*m_progress, id, "relative_path", [&] {
            auto resolved = std::filesystem::path(m_engineLibraryPath) / tr.relative_path();
            return resolved.lexically_normal().string();
        });
        track.bpm = safeGet<double>(*m_progress, id, "bpm", [&] { return tr.bpm().value_or(0.0); });
        track.key = safeGet<std::string>(*m_progress, id, "key", [&] {
            auto k = tr.key();
            if (!k) {
                return std::string();
            }
            std::ostringstream oss;
            oss << *k;
            return oss.str();
        });
        track.durationSeconds = safeGet<double>(*m_progress, id, "duration", [&] {
            auto duration = tr.duration();
            return duration ? duration->count() / 1000.0 : 0.0;
        });
        track.lastPlayedAt = safeGet<std::optional<std::chrono::system_clock::time_point>>(
            *m_progress, id, "last_played_at", [&] { return tr.last_played_at(); });
        auto playlistsIt = playlistsByTrackId.find(id);
        if (playlistsIt != playlistsByTrackId.end()) {
            track.playlists = playlistsIt->second;
        }

        auto sampleRate = safeGet<std::optional<double>>(*m_progress, id, "sample_rate",
                                                          [&] { return tr.sample_rate(); });
        if (!sampleRate) {
            // 44.1kHz is by far the most common sample rate for the
            // compressed audio these libraries hold; falling back to it
            // gives a position that's very likely close to right, instead
            // of treating a raw sample count as if it were milliseconds
            // (which is wrong by roughly a factor of 44).
            sampleRate = 44100.0;
        }
        auto hotCues = safeGet<std::vector<std::optional<djinterop::hot_cue>>>(*m_progress, id, "hot_cues",
                                                                                [&] { return tr.hot_cues(); });
        for (size_t i = 0; i < hotCues.size(); ++i) {
            if (!hotCues[i]) {
                continue;
            }
            const auto &hotCue = *hotCues[i];
            domain::CuePoint cp;
            cp.kind = domain::CuePoint::Kind::Hot;
            cp.hotCueNumber = static_cast<int>(i) + 1;  // Engine slots are 0-based; rekordbox numbers from 1
            cp.positionMs = hotCue.sample_offset / *sampleRate * 1000.0;
            cp.color = colorHex(hotCue.color);
            cp.comment = hotCue.label;
            track.cues.push_back(std::move(cp));
        }

        // Engine's format has exactly one memory-style cue point (called
        // "Cue" in the app), stored as a plain sample offset with no
        // color/comment -- unlike rekordbox's unlimited, independently
        // colored/commented memory cues. Represented here as a single
        // Kind::Memory CuePoint (hotCueNumber 0, matching how rekordbox's
        // own reader marks memory cues) so it can be matched/synced like
        // any other cue; see libdjinterop_engine_cue_writer.cpp for the
        // corresponding (necessarily lossy beyond one cue) write side.
        auto mainCue = safeGet<std::optional<double>>(*m_progress, id, "main_cue", [&] { return tr.main_cue(); });
        if (mainCue) {
            domain::CuePoint cp;
            cp.kind = domain::CuePoint::Kind::Memory;
            cp.positionMs = *mainCue / *sampleRate * 1000.0;
            track.cues.push_back(std::move(cp));
        }

        tracks.push_back(std::move(track));
        m_progress->tick(++processed);
    }

    m_progress->finish();
    return tracks;
}

}  // namespace djconvert::infrastructure::engine
