#include <cassert>
#include <iostream>

#include "domain/junk_cue.hpp"

using namespace seabass::domain;

namespace
{

CuePoint makeCue(CuePoint::Kind kind, double positionMs)
{
    CuePoint c;
    c.kind = kind;
    c.positionMs = positionMs;
    return c;
}

Track makeTrack(std::string id, std::string title, std::string artist, std::vector<CuePoint> cues)
{
    Track t;
    t.sourceId = std::move(id);
    t.title = std::move(title);
    t.artist = std::move(artist);
    t.cues = std::move(cues);
    return t;
}

}  // namespace

int main()
{
    // Case 1: a memory cue at 0:00 is junk.
    {
        std::vector<Track> tracks = {
            makeTrack("a", "Song", "Artist", {makeCue(CuePoint::Kind::Memory, 0.0)}),
        };
        auto issues = JunkCueFinder::find(tracks);
        assert(issues.size() == 1);
        assert(issues[0].track.sourceId == "a");
        assert(issues[0].cue.kind == CuePoint::Kind::Memory);
        std::cout << "case 1 (memory cue at 0:00 is flagged) OK\n";
    }

    // Case 2: a hot cue at 0:00 is not junk, deliberate track-start pads
    // some DJs place there on purpose.
    {
        std::vector<Track> tracks = {
            makeTrack("a", "Song", "Artist", {makeCue(CuePoint::Kind::Hot, 0.0)}),
        };
        auto issues = JunkCueFinder::find(tracks);
        assert(issues.empty());
        std::cout << "case 2 (hot cue at 0:00 is not flagged) OK\n";
    }

    // Case 3: a memory cue away from 0:00 is not junk.
    {
        std::vector<Track> tracks = {
            makeTrack("a", "Song", "Artist", {makeCue(CuePoint::Kind::Memory, 12345.0)}),
        };
        auto issues = JunkCueFinder::find(tracks);
        assert(issues.empty());
        std::cout << "case 3 (memory cue away from 0:00 is not flagged) OK\n";
    }

    // Case 4: multiple tracks, multiple cues, only the matching ones surface.
    {
        std::vector<Track> tracks = {
            makeTrack("a", "Song A", "Artist", {makeCue(CuePoint::Kind::Memory, 0.0),
                                                 makeCue(CuePoint::Kind::Memory, 5000.0)}),
            makeTrack("b", "Song B", "Artist", {makeCue(CuePoint::Kind::Hot, 0.0)}),
            makeTrack("c", "Song C", "Artist", {makeCue(CuePoint::Kind::Memory, 0.0)}),
        };
        auto issues = JunkCueFinder::find(tracks);
        assert(issues.size() == 2);
        assert(issues[0].track.sourceId == "a");
        assert(issues[1].track.sourceId == "c");
        std::cout << "case 4 (multiple tracks/cues, only true matches surface) OK\n";
    }

    std::cout << "All junk_cue tests passed.\n";
    return 0;
}
