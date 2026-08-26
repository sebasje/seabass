#include "infrastructure/engine/libdjinterop_engine_reader.hpp"

#include <cstdio>
#include <stdexcept>

#include <djinterop/djinterop.hpp>

namespace djconvert::infrastructure::engine
{

namespace domain = djconvert::domain;

namespace
{

std::string colorHex(const djinterop::pad_color &c)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
    return buf;
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

    m_progress->start("Scanning Engine tracks", allTracks.size());
    size_t processed = 0;

    std::vector<domain::Track> tracks;
    for (auto &tr : allTracks) {
        // Some tracks written by real Denon hardware/firmware (observed:
        // cached streaming-service entries, and occasionally a local file)
        // use a track-data blob shape libdjinterop's decoder rejects. Skip
        // those rather than aborting the whole scan.
        try {
            domain::Track track;
            track.sourceId = std::to_string(tr.id());
            if (auto title = tr.title()) {
                track.title = *title;
            }
            if (auto artist = tr.artist()) {
                track.artist = *artist;
            }
            track.filename = tr.filename();
            if (auto duration = tr.duration()) {
                track.durationSeconds = duration->count() / 1000.0;
            }

            auto sampleRate = tr.sample_rate();
            auto hotCues = tr.hot_cues();
            for (size_t i = 0; i < hotCues.size(); ++i) {
                if (!hotCues[i]) {
                    continue;
                }
                const auto &hotCue = *hotCues[i];
                domain::CuePoint cp;
                cp.kind = domain::CuePoint::Kind::Hot;
                cp.hotCueNumber = static_cast<int>(i) + 1;  // Engine slots are 0-based; rekordbox numbers from 1
                cp.positionMs = sampleRate ? (hotCue.sample_offset / *sampleRate * 1000.0) : hotCue.sample_offset;
                cp.color = colorHex(hotCue.color);
                cp.comment = hotCue.label;
                track.cues.push_back(std::move(cp));
            }

            tracks.push_back(std::move(track));
        } catch (const std::exception &e) {
            m_progress->warn("skipping unreadable Engine track id=" + std::to_string(tr.id()) + " (" + e.what() +
                              ")");
        }
        m_progress->tick(++processed);
    }

    m_progress->finish();
    return tracks;
}

}  // namespace djconvert::infrastructure::engine
