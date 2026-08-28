#include <cassert>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#include <djinterop/djinterop.hpp>

#include "infrastructure/engine/libdjinterop_engine_cleanup_writer.hpp"

using namespace djconvert::infrastructure::engine;
namespace fs = std::filesystem;

int main()
{
    fs::path root = fs::temp_directory_path() / "djconvert_engine_cleanup_writer_test" / "Engine Library";
    fs::remove_all(root.parent_path());
    fs::create_directories(root.parent_path());

    // A real, fresh Engine database (not a synthetic byte fixture --
    // libdjinterop's own creation API, same trust level as its
    // remove_track()/playlist APIs this class relies on).
    {
        auto db = djinterop::engine::create_database(root.string());

        djinterop::track_snapshot snapshot;
        snapshot.title = "Keep Me";
        snapshot.relative_path = "keep.mp3";
        auto survivorTrack = db.create_track(snapshot);

        snapshot.title = "Remove Me";
        snapshot.relative_path = "remove.mp3";
        auto doomedTrack = db.create_track(snapshot);

        auto playlist = db.create_root_playlist("Test Playlist");
        playlist.add_track_back(survivorTrack);
        playlist.add_track_back(doomedTrack);

        assert(playlist.tracks().size() == 2);

        LibdjinteropEngineCleanupWriter writer(root.string());
        writer.removeTrack(std::to_string(doomedTrack.id()));

        // Re-open fresh rather than reusing any in-memory handle -- the
        // point is confirming what's actually on disk now.
        auto dbAfter = djinterop::engine::load_database(root.string());
        assert(!dbAfter.track_by_id(doomedTrack.id()).has_value());
        auto survivorAfter = dbAfter.track_by_id(survivorTrack.id());
        assert(survivorAfter.has_value());

        // The real point: playlist membership was actually cleaned up
        // (this project's own explicit playlist walk -- see the .cpp for
        // why database::remove_track() alone isn't enough), not left
        // dangling.
        auto playlists = dbAfter.root_playlists();
        assert(playlists.size() == 1);
        auto remainingTracks = playlists[0].tracks();
        assert(remainingTracks.size() == 1);
        assert(remainingTracks[0].id() == survivorTrack.id());

        std::cout << "case 1 (removeTrack cleans up root-playlist membership) OK\n";
    }

    // Nested (child) playlists must be cleaned up too, not just root
    // ones -- exercises the recursive walk.
    {
        auto db = djinterop::engine::load_database(root.string());

        djinterop::track_snapshot snapshot;
        snapshot.title = "Nested Track";
        snapshot.relative_path = "nested.mp3";
        auto nestedTrack = db.create_track(snapshot);

        auto parentPlaylist = db.create_root_playlist("Parent");
        auto childPlaylist = parentPlaylist.create_sub_playlist("Child");
        childPlaylist.add_track_back(nestedTrack);
        assert(childPlaylist.tracks().size() == 1);

        LibdjinteropEngineCleanupWriter writer(root.string());
        writer.removeTrack(std::to_string(nestedTrack.id()));

        auto dbAfter = djinterop::engine::load_database(root.string());
        assert(!dbAfter.track_by_id(nestedTrack.id()).has_value());
        for (const auto &r : dbAfter.root_playlists()) {
            if (r.name() != "Parent") {
                continue;
            }
            for (const auto &child : r.children()) {
                assert(child.tracks().empty());
            }
        }
        std::cout << "case 2 (removeTrack cleans up nested/child-playlist membership too) OK\n";
    }

    // Not-found case throws.
    {
        LibdjinteropEngineCleanupWriter writer(root.string());
        bool threw = false;
        try {
            writer.removeTrack("999999999");
        } catch (const std::exception &) {
            threw = true;
        }
        assert(threw);
        std::cout << "case 3 (removeTrack on a nonexistent id throws) OK\n";
    }

    std::cout << "all cases passed\n";
    return 0;
}
