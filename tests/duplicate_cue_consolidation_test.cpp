#include <cassert>
#include <iostream>

#include "domain/duplicate_cue_consolidation.hpp"

using namespace djconvert::domain;

Track makeTrack(std::string id, std::string filename, double duration, std::vector<CuePoint> cues,
                std::string title = "", std::string artist = "")
{
    Track t;
    t.sourceId = std::move(id);
    t.filename = std::move(filename);
    t.durationSeconds = duration;
    t.cues = std::move(cues);
    t.title = std::move(title);
    t.artist = std::move(artist);
    return t;
}

int main()
{
    // Case 1: unambiguous -- one copy has cues, other doesn't.
    {
        std::vector<Track> tracks = {
            makeTrack("1", "song.mp3", 200.0, {CuePoint{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"}}),
            makeTrack("2", "song.mp3", 200.0, {}),
        };
        auto groups = DuplicateTrackFinder::find(tracks);
        assert(groups.size() == 1);
        auto plan = DuplicateCueConsolidator::plan(groups[0]);
        assert(plan.kind == ConsolidationPlan::Kind::Unambiguous);
        assert(plan.source->sourceId == "1");
        assert(plan.targets.size() == 1 && plan.targets[0].sourceId == "2");
        std::cout << "case 1 (unambiguous) OK\n";
    }

    // Case 2: conflict -- both have cues, but different.
    {
        std::vector<Track> tracks = {
            makeTrack("1", "song.mp3", 200.0, {CuePoint{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"}}),
            makeTrack("2", "song.mp3", 200.0, {CuePoint{CuePoint::Kind::Hot, 1, 5000.0, "#00FF00", "intro"}}),
        };
        auto groups = DuplicateTrackFinder::find(tracks);
        assert(groups.size() == 1);
        auto plan = DuplicateCueConsolidator::plan(groups[0]);
        assert(plan.kind == ConsolidationPlan::Kind::Conflict);
        std::cout << "case 2 (conflict) OK\n";
    }

    // Case 3: already consistent -- both have the same cues.
    {
        std::vector<Track> tracks = {
            makeTrack("1", "song.mp3", 200.0, {CuePoint{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"}}),
            makeTrack("2", "song.mp3", 200.0, {CuePoint{CuePoint::Kind::Hot, 1, 1000.4, "#FF0000", "drop"}}),
        };
        auto groups = DuplicateTrackFinder::find(tracks);
        assert(groups.size() == 1);
        auto plan = DuplicateCueConsolidator::plan(groups[0]);
        assert(plan.kind == ConsolidationPlan::Kind::AlreadyConsistent);
        std::cout << "case 3 (already consistent) OK\n";
    }

    // Case 4: different filenames -- not treated as duplicates at all.
    {
        std::vector<Track> tracks = {
            makeTrack("1", "14_song.mp3", 200.0, {CuePoint{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"}}),
            makeTrack("2", "83_song.mp3", 200.0, {}),
        };
        auto groups = DuplicateTrackFinder::find(tracks);
        assert(groups.empty());
        std::cout << "case 4 (different filenames -> no group) OK\n";
    }

    // Case 5: same filename, very different duration -- not clustered together.
    {
        std::vector<Track> tracks = {
            makeTrack("1", "song.mp3", 200.0, {}),
            makeTrack("2", "song.mp3", 9000.0, {}),
        };
        auto groups = DuplicateTrackFinder::find(tracks);
        assert(groups.empty());
        std::cout << "case 5 (duration mismatch -> no group) OK\n";
    }

    // Case 6: different filenames but matching title+artist -- grouped via
    // the title+artist path (e.g. re-imported under a new naming scheme).
    {
        std::vector<Track> tracks = {
            makeTrack("1", "099_ella-but_not_for_me.mp3", 200.0,
                      {CuePoint{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"}}, "But Not For Me",
                      "Ella Fitzgerald"),
            makeTrack("2", "03_ella-but_not_for_me.mp3", 200.0, {}, "But Not For Me", "Ella Fitzgerald"),
        };
        auto groups = DuplicateTrackFinder::find(tracks);
        assert(groups.size() == 1);
        assert(groups[0].tracks.size() == 2);
        auto plan = DuplicateCueConsolidator::plan(groups[0]);
        assert(plan.kind == ConsolidationPlan::Kind::Unambiguous);
        std::cout << "case 6 (different filename, same title+artist -> grouped) OK\n";
    }

    // Case 7: different filenames AND different title/artist -- no group.
    {
        std::vector<Track> tracks = {
            makeTrack("1", "a.mp3", 200.0, {}, "Song A", "Artist A"),
            makeTrack("2", "b.mp3", 200.0, {}, "Song B", "Artist B"),
        };
        auto groups = DuplicateTrackFinder::find(tracks);
        assert(groups.empty());
        std::cout << "case 7 (different everything -> no group) OK\n";
    }

    // Case 8: same title+artist, but durations too far apart -- title+artist
    // alone isn't enough; duration tolerance still applies (e.g. a short
    // radio edit misidentified under the same metadata as the full track).
    {
        std::vector<Track> tracks = {
            makeTrack("1", "a.mp3", 200.0, {}, "Song", "Artist"),
            makeTrack("2", "b.mp3", 9000.0, {}, "Song", "Artist"),
        };
        auto groups = DuplicateTrackFinder::find(tracks);
        assert(groups.empty());
        std::cout << "case 8 (same title+artist, duration mismatch -> no group) OK\n";
    }

    // Case 9: tracks with no title/artist metadata must never be merged with
    // each other just because they all share an empty key -- title+artist
    // matching is skipped entirely when either field is blank.
    {
        std::vector<Track> tracks = {
            makeTrack("1", "a.mp3", 200.0, {}),
            makeTrack("2", "b.mp3", 200.0, {}),
            makeTrack("3", "c.mp3", 200.0, {}),
        };
        auto groups = DuplicateTrackFinder::find(tracks);
        assert(groups.empty());
        std::cout << "case 9 (blank title/artist never false-matches) OK\n";
    }

    // Case 10: transitivity -- A and B share a filename (no metadata), B and
    // C share title+artist (different filename from A). All three must end
    // up in one group even though A and C match on neither criterion
    // directly.
    {
        std::vector<Track> tracks = {
            makeTrack("A", "shared.mp3", 200.0, {}),
            makeTrack("B", "shared.mp3", 200.0, {}, "Song", "Artist"),
            makeTrack("C", "other.mp3", 200.0, {CuePoint{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"}}, "Song",
                      "Artist"),
        };
        auto groups = DuplicateTrackFinder::find(tracks);
        assert(groups.size() == 1);
        assert(groups[0].tracks.size() == 3);
        std::cout << "case 10 (transitive filename + title/artist union) OK\n";
    }

    std::cout << "all cases passed\n";
    return 0;
}
