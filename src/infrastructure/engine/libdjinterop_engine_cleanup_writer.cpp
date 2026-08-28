#include "infrastructure/engine/libdjinterop_engine_cleanup_writer.hpp"

#include <stdexcept>

#include <djinterop/djinterop.hpp>

namespace djconvert::infrastructure::engine
{

namespace
{

void removeFromPlaylistRecursively(djinterop::playlist pl, const djinterop::track &track)
{
    // remove_track() only clears the first matching row -- loop in case
    // the track was somehow added to the same playlist more than once.
    while (true) {
        auto tracks = pl.tracks();
        bool contains = false;
        for (const auto &t : tracks) {
            if (t.id() == track.id()) {
                contains = true;
                break;
            }
        }
        if (!contains) {
            break;
        }
        pl.remove_track(track);
    }
    for (auto &child : pl.children()) {
        removeFromPlaylistRecursively(child, track);
    }
}

}  // namespace

LibdjinteropEngineCleanupWriter::LibdjinteropEngineCleanupWriter(std::string engineLibraryPath)
    : m_engineLibraryPath(std::move(engineLibraryPath))
{
}

void LibdjinteropEngineCleanupWriter::removeTrack(const std::string &trackSourceId)
{
    auto db = djinterop::engine::load_database(m_engineLibraryPath);

    auto track = db.track_by_id(std::stoll(trackSourceId));
    if (!track) {
        throw std::runtime_error("no Engine track with id=" + trackSourceId);
    }

    // database::remove_track()'s own source comment claims playlist
    // membership is cleared automatically via an "ON DELETE CASCADE"
    // foreign key -- but SQLite only enforces a declared FK constraint
    // when `PRAGMA foreign_keys = ON` is set on the connection, which
    // this library never does anywhere. Confirmed by a real test
    // against a freshly-created database: the cascade did NOT fire,
    // leaving a dangling reference in the playlist. So walk every
    // playlist explicitly first, rather than trust that comment.
    for (auto &root : db.root_playlists()) {
        removeFromPlaylistRecursively(root, *track);
    }

    db.remove_track(*track);
}

}  // namespace djconvert::infrastructure::engine
