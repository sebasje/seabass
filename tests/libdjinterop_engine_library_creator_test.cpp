#include <cassert>
#include <filesystem>
#include <iostream>

#include "application/use_cases/scan_library.hpp"
#include "infrastructure/engine/libdjinterop_engine_library_creator.hpp"
#include "infrastructure/engine/libdjinterop_engine_reader.hpp"

using namespace seabass::infrastructure::engine;
using seabass::domain::CuePoint;
using seabass::domain::Track;
namespace fs = std::filesystem;

namespace
{

Track makeTrack(std::string id, std::string title, std::string artist, std::string filePath, double bpm = 128.0,
                 std::string key = "F#m", double duration = 300.0)
{
    Track t;
    t.sourceId = std::move(id);
    t.title = std::move(title);
    t.artist = std::move(artist);
    t.filePath = std::move(filePath);
    t.bpm = bpm;
    t.key = std::move(key);
    t.durationSeconds = duration;
    t.bitrate = 320;
    t.rating = 4;
    t.comment = "banger";
    t.fileSizeBytes = 12345;
    t.cues = {
        {CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"},
        {CuePoint::Kind::Memory, 0, 500.0, "", ""},
    };
    return t;
}

}  // namespace

int main()
{
    fs::path root = fs::temp_directory_path() / "seabass_engine_library_creator_test";
    fs::remove_all(root);
    fs::create_directories(root);
    fs::path enginePath = root / "Engine Library";

    // Case 1: creates tracks + cues, verified by reading them back via
    // the same, already-proven LibdjinteropEngineReader.
    {
        std::vector<Track> tracks = {
            makeTrack("r1", "Song One", "Artist One", (root / "song1.mp3").string()),
            makeTrack("r2", "Song Two", "Artist Two", (root / "song2.mp3").string(), 140.0, "Gbm", 200.0),
        };
        auto result = EngineLibraryCreator::create(enginePath.string(), tracks, EngineSchemaGeneration::V2);
        assert(result.errorMessage.empty());
        assert(result.tracksCreated == 2);
        assert(result.tracksSkipped == 0);
        assert(result.cuesCopied == 4);

        LibdjinteropEngineReader reader(enginePath.string());
        auto readBack = seabass::application::ScanLibrary(reader).execute();
        assert(readBack.size() == 2);

        bool foundOne = false, foundTwo = false;
        for (const auto &t : readBack) {
            if (t.title == "Song One") {
                foundOne = true;
                assert(t.artist == "Artist One");
                assert(t.bitrate == 320);
                assert(t.rating.has_value() && *t.rating == 4);
                assert(t.comment == "banger");
                assert(t.cues.size() == 2);
            }
            if (t.title == "Song Two") {
                foundTwo = true;
                // "Gbm" and "F#m" are enharmonically the same key --
                // both tracks should read back with a non-empty key
                // string (exact spelling is libdjinterop's own choice).
                assert(!t.key.empty());
            }
        }
        assert(foundOne && foundTwo);
        std::cout << "case 1 (create + read back tracks/cues/metadata) OK\n";
    }

    // Case 2: refuses to overwrite an existing Engine Library.
    {
        std::vector<Track> tracks = {makeTrack("r3", "Song Three", "Artist Three", (root / "song3.mp3").string())};
        auto result = EngineLibraryCreator::create(enginePath.string(), tracks, EngineSchemaGeneration::V2);
        assert(!result.errorMessage.empty());
        assert(result.tracksCreated == 0);
        std::cout << "case 2 (refuses to overwrite an existing library) OK\n";
    }

    // Case 3: a track with no resolved local file is skipped, not
    // fabricated into the new library.
    {
        fs::path secondEnginePath = root / "Engine Library 2";
        Track noFile = makeTrack("r4", "No File", "Nobody", "");
        std::vector<Track> tracks = {noFile};
        auto result = EngineLibraryCreator::create(secondEnginePath.string(), tracks, EngineSchemaGeneration::V2);
        assert(result.errorMessage.empty());
        assert(result.tracksCreated == 0);
        assert(result.tracksSkipped == 1);
        std::cout << "case 3 (track with no local file is skipped) OK\n";
    }

    fs::remove_all(root);
    std::cout << "All libdjinterop_engine_library_creator tests passed.\n";
    return 0;
}
