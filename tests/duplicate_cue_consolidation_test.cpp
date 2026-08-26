#include <cassert>
#include <iostream>

#include "domain/duplicate_cue_consolidation.hpp"

using namespace djconvert::domain;

Track makeTrack(std::string id, std::string filename, double duration, std::vector<CuePoint> cues)
{
    Track t;
    t.sourceId = std::move(id);
    t.filename = std::move(filename);
    t.durationSeconds = duration;
    t.cues = std::move(cues);
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

    std::cout << "all cases passed\n";
    return 0;
}
