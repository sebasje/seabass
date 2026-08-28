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

    // cueSetsEqual: kind/hotCueNumber mismatches always count, regardless of
    // position. comment is deliberately NOT compared -- RekordboxCueWriter
    // can't write it at all (anlz_cue_codec.cpp always encodes an empty
    // comment), so treating a comment difference as a real mismatch made
    // Engine cues with a label permanently reappear as "needs sync" with no
    // writer able to ever resolve it. color IS compared for hot cues (both
    // formats support it), but NOT for memory cues -- Engine's single
    // memory-style cue point has no color at all, so the same
    // permanently-stuck problem would hit any colored rekordbox memory cue.
    {
        CuePoint base{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"};
        assert(!cueSetsEqual({base}, {CuePoint{CuePoint::Kind::Memory, 1, 1000.0, "#FF0000", "drop"}}));
        assert(!cueSetsEqual({base}, {CuePoint{CuePoint::Kind::Hot, 2, 1000.0, "#FF0000", "drop"}}));
        assert(!cueSetsEqual({base}, {CuePoint{CuePoint::Kind::Hot, 1, 1000.0, "#00FF00", "drop"}}));
        assert(cueSetsEqual({base}, {CuePoint{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "break"}}));

        CuePoint memoryBase{CuePoint::Kind::Memory, 0, 7000.0, "#FF0000", ""};
        assert(cueSetsEqual({memoryBase}, {CuePoint{CuePoint::Kind::Memory, 0, 7000.0, "", ""}}));
        std::cout << "case 5 (cueSetsEqual field mismatches -> false, comment/memory-color ignored) OK\n";
    }

    // titleArtistKey: case/whitespace-insensitive, symmetric in what it
    // normalizes, and nullopt when either field is missing (too weak a
    // signal to match on).
    {
        Track a;
        a.title = "Song Title";
        a.artist = "The Artist";
        Track b;
        b.title = "song title";
        b.artist = "the artist";
        assert(titleArtistKey(a) == titleArtistKey(b));

        Track noTitle;
        noTitle.artist = "The Artist";
        assert(!titleArtistKey(noTitle).has_value());

        Track noArtist;
        noArtist.title = "Song Title";
        assert(!titleArtistKey(noArtist).has_value());

        Track differentArtist;
        differentArtist.title = "Song Title";
        differentArtist.artist = "Someone Else";
        assert(titleArtistKey(a) != titleArtistKey(differentArtist));
        std::cout << "case 6 (titleArtistKey normalization and missing-field handling) OK\n";
    }

    // matchTracks: title+artist is primary (filenames may legitimately
    // differ), filename is only a fallback when metadata is missing.
    {
        Track a1;
        a1.sourceId = "a1";
        a1.filename = "01 - song.mp3";
        a1.title = "Song";
        a1.artist = "Artist";
        a1.durationSeconds = 200.0;

        Track b1;
        b1.sourceId = "b1";
        b1.filename = "song (export).mp3";
        b1.title = "Song";
        b1.artist = "Artist";
        b1.durationSeconds = 200.5;

        std::vector<Track> as = {a1};
        std::vector<Track> bs = {b1};
        auto matches = matchTracks(as, bs);
        assert(matches.size() == 1);
        assert(matches[0].first->sourceId == "a1");
        assert(matches[0].second->sourceId == "b1");
        std::cout << "case 7 (matchTracks: title+artist match despite differing filenames) OK\n";
    }
    {
        Track a2;
        a2.sourceId = "a2";
        a2.filename = "same.mp3";
        a2.durationSeconds = 100.0;

        Track b2;
        b2.sourceId = "b2";
        b2.filename = "same.mp3";
        b2.durationSeconds = 100.0;

        std::vector<Track> as = {a2};
        std::vector<Track> bs = {b2};
        auto matches = matchTracks(as, bs);
        assert(matches.size() == 1);
        assert(matches[0].first->sourceId == "a2");
        assert(matches[0].second->sourceId == "b2");
        std::cout << "case 8 (matchTracks: filename fallback when metadata missing) OK\n";
    }

    std::cout << "all cases passed\n";
    return 0;
}
