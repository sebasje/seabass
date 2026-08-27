#include <cassert>
#include <cstdio>
#include <filesystem>
#include <iostream>

#include "infrastructure/local/local_cue_store.hpp"

using namespace djconvert::domain;
using namespace djconvert::infrastructure::local;
namespace fs = std::filesystem;

namespace
{

Track makeTrack(std::string id, std::string filename, std::string title, std::string artist, double duration,
                 std::vector<CuePoint> cues)
{
    Track t;
    t.sourceId = std::move(id);
    t.filename = std::move(filename);
    t.title = std::move(title);
    t.artist = std::move(artist);
    t.durationSeconds = duration;
    t.cues = std::move(cues);
    return t;
}

}  // namespace

int main()
{
    fs::path dbPath = fs::temp_directory_path() / "djconvert_local_cue_store_test.db";
    fs::remove(dbPath);

    CuePoint hotCue{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"};

    // Upsert a track with cues, read it back.
    {
        LocalCueStore store(dbPath.string());
        std::vector<Track> tracks = {
            makeTrack("e1", "song.mp3", "Song", "Artist", 200.0, {hotCue}),
            makeTrack("e2", "no-cues.mp3", "Silent", "Nobody", 100.0, {}),  // no cues -> skipped
        };
        store.upsert(tracks, "engine", "WHALESHARK2");

        auto readBack = store.readAll();
        assert(readBack.size() == 1);  // the no-cues track was never stored
        assert(readBack[0].filename == "song.mp3");
        assert(readBack[0].title == "Song");
        assert(readBack[0].artist == "Artist");
        assert(readBack[0].cues.size() == 1);
        assert(readBack[0].cues[0].kind == CuePoint::Kind::Hot);
        assert(readBack[0].cues[0].positionMs == 1000.0);
        std::cout << "case 1 (upsert stores only tracks with cues, readAll round-trips) OK\n";
    }

    // Re-opening the same database file persists what was written.
    {
        LocalCueStore store(dbPath.string());
        auto readBack = store.readAll();
        assert(readBack.size() == 1);
        std::cout << "case 2 (data persists across store instances) OK\n";
    }

    // Upserting a track that matches an existing one (by title+artist)
    // replaces its cues rather than adding a second row.
    {
        LocalCueStore store(dbPath.string());
        CuePoint newCue{CuePoint::Kind::Hot, 2, 5000.0, "#00FF00", "break"};
        std::vector<Track> tracks = {
            makeTrack("e1-rescanned", "song (renamed).mp3", "Song", "Artist", 200.2, {newCue}),
        };
        store.upsert(tracks, "engine", "WHALESHARK2");

        auto readBack = store.readAll();
        assert(readBack.size() == 1);  // still one row, not two
        assert(readBack[0].filename == "song (renamed).mp3");  // metadata updated
        assert(readBack[0].cues.size() == 1);
        assert(readBack[0].cues[0].positionMs == 5000.0);  // cues replaced, not appended
        std::cout << "case 3 (upsert matches by title+artist and replaces cues) OK\n";
    }

    fs::remove(dbPath);
    std::cout << "all cases passed\n";
    return 0;
}
