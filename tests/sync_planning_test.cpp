#include <cassert>
#include <iostream>

#include "domain/sync_planning.hpp"

using namespace djconvert::domain;
using namespace std::chrono;

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
    auto now = system_clock::now();

    // Matching: same filename + duration across formats pairs up.
    {
        std::vector<Track> rekordbox = {makeTrack("r1", "song.mp3", 200.0, {})};
        std::vector<Track> engine = {makeTrack("e1", "song.mp3", 200.5, {})};
        auto matches = TrackMatcher::match(rekordbox, engine);
        assert(matches.size() == 1);
        assert(matches[0].rekordboxTrack.sourceId == "r1");
        assert(matches[0].engineTrack.sourceId == "e1");
        std::cout << "case 1 (matching) OK\n";
    }

    // No match when filenames differ.
    {
        std::vector<Track> rekordbox = {makeTrack("r1", "a.mp3", 200.0, {})};
        std::vector<Track> engine = {makeTrack("e1", "b.mp3", 200.0, {})};
        auto matches = TrackMatcher::match(rekordbox, engine);
        assert(matches.empty());
        std::cout << "case 2 (no match, different filenames) OK\n";
    }

    // rekordbox has cues, engine doesn't -> propagate to engine.
    {
        SyncMatch m{makeTrack("r1", "song.mp3", 200.0,
                              {CuePoint{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"}}),
                    makeTrack("e1", "song.mp3", 200.0, {})};
        auto plan = SyncPlanner::plan(m, now, now);
        assert(plan.kind == SyncPlan::Kind::RekordboxOnly);
        assert(plan.direction == SyncPlan::Direction::ToEngine);
        assert(plan.cuesToApply.size() == 1);
        std::cout << "case 3 (rekordbox-only -> to engine) OK\n";
    }

    // Engine has cues, rekordbox doesn't -> would propagate to rekordbox (unwritable, but planned).
    {
        SyncMatch m{makeTrack("r1", "song.mp3", 200.0, {}),
                    makeTrack("e1", "song.mp3", 200.0,
                              {CuePoint{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"}})};
        auto plan = SyncPlanner::plan(m, now, now);
        assert(plan.kind == SyncPlan::Kind::EngineOnly);
        assert(plan.direction == SyncPlan::Direction::ToRekordbox);
        std::cout << "case 4 (engine-only -> to rekordbox) OK\n";
    }

    // Both consistent -> no-op.
    {
        SyncMatch m{makeTrack("r1", "song.mp3", 200.0,
                              {CuePoint{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"}}),
                    makeTrack("e1", "song.mp3", 200.0,
                              {CuePoint{CuePoint::Kind::Hot, 1, 1000.4, "#FF0000", "drop"}})};
        auto plan = SyncPlanner::plan(m, now, now);
        assert(plan.kind == SyncPlan::Kind::AlreadyConsistent);
        std::cout << "case 5 (already consistent) OK\n";
    }

    // Conflict resolved by mtime: rekordbox file newer -> wins, propagates to engine.
    {
        SyncMatch m{makeTrack("r1", "song.mp3", 200.0,
                              {CuePoint{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"}}),
                    makeTrack("e1", "song.mp3", 200.0,
                              {CuePoint{CuePoint::Kind::Hot, 1, 5000.0, "#00FF00", "intro"}})};
        auto plan = SyncPlanner::plan(m, now, now - hours(1));
        assert(plan.kind == SyncPlan::Kind::Conflict);
        assert(plan.direction == SyncPlan::Direction::ToEngine);
        std::cout << "case 6 (conflict, rekordbox newer -> to engine) OK\n";
    }

    // Conflict resolved by mtime: engine file newer -> wins, propagates to rekordbox.
    {
        SyncMatch m{makeTrack("r1", "song.mp3", 200.0,
                              {CuePoint{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"}}),
                    makeTrack("e1", "song.mp3", 200.0,
                              {CuePoint{CuePoint::Kind::Hot, 1, 5000.0, "#00FF00", "intro"}})};
        auto plan = SyncPlanner::plan(m, now - hours(1), now);
        assert(plan.kind == SyncPlan::Kind::Conflict);
        assert(plan.direction == SyncPlan::Direction::ToRekordbox);
        std::cout << "case 7 (conflict, engine newer -> to rekordbox) OK\n";
    }

    std::cout << "all cases passed\n";
    return 0;
}
