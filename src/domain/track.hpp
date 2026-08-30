#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace seabass::domain
{

// A hot cue or memory cue found on a track, normalized across the rekordbox
// and Engine library formats.
struct CuePoint
{
    enum class Kind { Memory, Hot };

    Kind kind = Kind::Memory;
    int hotCueNumber = 0;  // meaningful only when kind == Hot
    double positionMs = 0.0;
    std::string color;  // adapter-specific color representation (e.g. "#RRGGBB" or a named id)
    std::string comment;
};

// One playlist a track belongs to, with its position within that specific
// playlist (0-based; -1 if the reader couldn't determine it), lets
// callers sort a playlist-filtered view back into its original order.
struct PlaylistMembership
{
    std::string name;  // full path, e.g. "Techno/Peak Time"
    int position = -1;
};

// A track as read from either a rekordbox USB export or an Engine Library,
// normalized to a common shape. This is the shared intermediate
// representation the application layer's use cases operate on.
struct Track
{
    std::string sourceId;  // adapter-specific unique id (e.g. rekordbox track id, engine track id)
    std::string format;    // "rekordbox" or "engine", which catalog this copy was read from
    std::string title;
    std::string artist;
    std::string filename;
    std::string filePath;     // best-effort resolved absolute path to the audio file on disk, empty if unresolved
    std::string artworkPath;  // best-effort resolved path to a cover art image file, empty if unavailable
    // Non-empty (e.g. "TIDAL") if this track is a streaming-service link
    // rather than a local file, Engine only, set via a raw-SQL read of
    // Track.streamingSource (libdjinterop's public API doesn't expose
    // it). filePath is never meaningfully resolvable for these: it
    // points at a streaming-cache path on the computer that manages
    // playback, never at anything present on the stick itself. Callers
    // must never treat a track with this set as a real local file.
    // Never play it, merge it, sync it, or clean it up.
    std::string streamingSource;
    std::uint64_t fileSizeBytes = 0;  // best-effort size of the file at filePath on disk, 0 if unresolved/unreadable
    int bitrate = 0;  // kbps, 0 if unknown, used as the primary "which copy is higher quality" signal
    double durationSeconds = 0.0;
    double bpm = 0.0;
    std::string key;  // human-readable, e.g. "Fm" or "F#m", empty if unknown
    std::vector<CuePoint> cues;

    // Normalized to 0-5 stars (Engine's own 0-100 scale is divided down by
    // readers before this is set), nullopt when unrated rather than 0 --
    // both formats use "no rating stored" and "explicitly rated at 0
    // stars" interchangeably at the storage level, and nullopt is the
    // more useful distinction for statistics ("how many tracks has this
    // DJ actually rated" vs. "how many are literally 0 stars").
    std::optional<int> rating;
    std::string comment;  // the DJ's own free-text comment field, empty if none

    // Every playlist this track belongs to. Best-effort: populated where
    // the reader supports it, empty otherwise.
    std::vector<PlaylistMembership> playlists;

    // Engagement signals used to prioritize which tracks are worth setting
    // cue points on. The two formats track different things, rekordbox
    // keeps a running play count, Engine (via libdjinterop) only exposes
    // the timestamp of the most recent play, so both are optional and
    // independent; a given Track will typically have at most one set,
    // depending on which format it came from.
    std::optional<int> playCount;
    std::optional<std::chrono::system_clock::time_point> lastPlayedAt;
};

}  // namespace seabass::domain
