#include <cassert>
#include <iostream>

#include "domain/local_restore.hpp"

using namespace djconvert::domain;

Track makeTrack(std::string id, double duration, std::vector<CuePoint> cues)
{
    Track t;
    t.sourceId = std::move(id);
    t.durationSeconds = duration;
    t.cues = std::move(cues);
    return t;
}

int main()
{
    CuePoint hotCue{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"};

    // Stick track has no cues, local backup does -> propose a restore.
    {
        Track stickTrack = makeTrack("s1", 200.0, {});
        Track localTrack = makeTrack("l1", 200.0, {hotCue});
        std::vector<std::pair<const Track *, const Track *>> matches = {{&stickTrack, &localTrack}};

        auto candidates = LocalRestorePlanner::plan(matches);
        assert(candidates.size() == 1);
        assert(candidates[0].stickTrack.sourceId == "s1");
        assert(candidates[0].localTrack.sourceId == "l1");
        std::cout << "case 1 (stick empty, local has cues -> candidate) OK\n";
    }

    // Stick track already has cues -> never proposed, even if the local
    // backup's cues differ (restore never overwrites what's on the stick).
    {
        Track stickTrack = makeTrack("s1", 200.0, {hotCue});
        Track localTrack = makeTrack("l1", 200.0,
                                      {CuePoint{CuePoint::Kind::Hot, 2, 5000.0, "#00FF00", "break"}});
        std::vector<std::pair<const Track *, const Track *>> matches = {{&stickTrack, &localTrack}};

        auto candidates = LocalRestorePlanner::plan(matches);
        assert(candidates.empty());
        std::cout << "case 2 (stick already has cues -> not proposed) OK\n";
    }

    // Neither side has cues -> nothing to propose.
    {
        Track stickTrack = makeTrack("s1", 200.0, {});
        Track localTrack = makeTrack("l1", 200.0, {});
        std::vector<std::pair<const Track *, const Track *>> matches = {{&stickTrack, &localTrack}};

        auto candidates = LocalRestorePlanner::plan(matches);
        assert(candidates.empty());
        std::cout << "case 3 (neither side has cues -> not proposed) OK\n";
    }

    std::cout << "all cases passed\n";
    return 0;
}
