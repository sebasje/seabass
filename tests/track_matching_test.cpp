#include <cassert>
#include <iostream>

#include "domain/track_matching.hpp"

using namespace djconvert::domain;

int main()
{
    // normalizeFilename: case-insensitive, whitespace-insensitive.
    {
        assert(normalizeFilename("Song.mp3") == normalizeFilename("song.mp3"));
        assert(normalizeFilename("My Song.mp3") == normalizeFilename("MySong.mp3"));
        assert(normalizeFilename("a.mp3") != normalizeFilename("b.mp3"));
        std::cout << "case 1 (normalizeFilename case/whitespace insensitive) OK\n";
    }

    // cueSetsEqual: identical sets, in a different order, are still equal.
    {
        std::vector<CuePoint> a = {
            CuePoint{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"},
            CuePoint{CuePoint::Kind::Hot, 2, 5000.0, "#00FF00", "break"},
        };
        std::vector<CuePoint> b = {
            CuePoint{CuePoint::Kind::Hot, 2, 5000.0, "#00FF00", "break"},
            CuePoint{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"},
        };
        assert(cueSetsEqual(a, b));
        std::cout << "case 2 (cueSetsEqual ignores order) OK\n";
    }

    // cueSetsEqual: different sizes are never equal.
    {
        std::vector<CuePoint> a = {CuePoint{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", ""}};
        std::vector<CuePoint> b = {};
        assert(!cueSetsEqual(a, b));
        std::cout << "case 3 (cueSetsEqual size mismatch -> false) OK\n";
    }

    // cueSetsEqual: position tolerance is inclusive at 1000ms, exclusive past it.
    {
        std::vector<CuePoint> a = {CuePoint{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", ""}};
        std::vector<CuePoint> withinTolerance = {CuePoint{CuePoint::Kind::Hot, 1, 2000.0, "#FF0000", ""}};
        std::vector<CuePoint> pastTolerance = {CuePoint{CuePoint::Kind::Hot, 1, 2000.1, "#FF0000", ""}};
        assert(cueSetsEqual(a, withinTolerance));
        assert(!cueSetsEqual(a, pastTolerance));
        std::cout << "case 4 (cueSetsEqual position tolerance boundary) OK\n";
    }

    // cueSetsEqual: kind/hotCueNumber/color/comment mismatches all count,
    // regardless of position.
    {
        CuePoint base{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"};
        assert(!cueSetsEqual({base}, {CuePoint{CuePoint::Kind::Memory, 1, 1000.0, "#FF0000", "drop"}}));
        assert(!cueSetsEqual({base}, {CuePoint{CuePoint::Kind::Hot, 2, 1000.0, "#FF0000", "drop"}}));
        assert(!cueSetsEqual({base}, {CuePoint{CuePoint::Kind::Hot, 1, 1000.0, "#00FF00", "drop"}}));
        assert(!cueSetsEqual({base}, {CuePoint{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "break"}}));
        std::cout << "case 5 (cueSetsEqual field mismatches -> false) OK\n";
    }

    std::cout << "all cases passed\n";
    return 0;
}
