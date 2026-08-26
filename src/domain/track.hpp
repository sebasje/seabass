#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace djconvert::domain
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

// A track as read from either a rekordbox USB export or an Engine Library,
// normalized to a common shape. This is the shared intermediate
// representation the application layer's use cases operate on.
struct Track
{
    std::string sourceId;  // adapter-specific unique id (e.g. rekordbox track id, engine track id)
    std::string title;
    std::string artist;
    std::string filename;
    std::string filePath;     // best-effort resolved absolute path to the audio file on disk, empty if unresolved
    std::string artworkPath;  // best-effort resolved path to a cover art image file, empty if unavailable
    double durationSeconds = 0.0;
    double bpm = 0.0;
    std::string key;  // human-readable, e.g. "Fm" or "F#m" -- empty if unknown
    std::vector<CuePoint> cues;

    // Full paths (e.g. "Techno/Peak Time") of every playlist this track
    // belongs to. Best-effort: populated where the reader supports it,
    // empty otherwise.
    std::vector<std::string> playlists;

    // Engagement signals used to prioritize which tracks are worth setting
    // cue points on. The two formats track different things -- rekordbox
    // keeps a running play count, Engine (via libdjinterop) only exposes
    // the timestamp of the most recent play -- so both are optional and
    // independent; a given Track will typically have at most one set,
    // depending on which format it came from.
    std::optional<int> playCount;
    std::optional<std::chrono::system_clock::time_point> lastPlayedAt;
};

}  // namespace djconvert::domain
