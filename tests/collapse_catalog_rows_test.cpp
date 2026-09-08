// A file is what duplicates; a catalog row is one catalog's record of
// one. Everything here is a case where confusing the two would have made
// a real stick's cleanup wrong.
#include <cassert>
#include <iostream>
#include <string>

#include "application/use_cases/collapse_catalog_rows.hpp"
#include "domain/duplicate_cleanup.hpp"
#include "domain/duplicate_cue_consolidation.hpp"

using namespace seabass;
using seabass::application::collapseCatalogRows;
using seabass::domain::Track;

namespace
{

Track row(std::string format, std::string sourceId, std::string path, std::string title, double duration,
           int bitrate)
{
    Track t;
    t.format = std::move(format);
    t.sourceId = std::move(sourceId);
    t.filePath = std::move(path);
    t.filename = "song.mp3";
    t.title = std::move(title);
    t.artist = "Kollektiv Turmstrasse";
    t.durationSeconds = duration;
    t.bitrate = bitrate;
    t.fileSizeBytes = 8'000'000;
    return t;
}

bool listsRow(const Track &file, const std::string &format, const std::string &sourceId)
{
    for (const auto &r : file.catalogRows) {
        if (r.format == format && r.sourceId == sourceId) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main()
{
    // The shape of a real stick: three catalogs, one file. It must come
    // out as one copy that knows all three of its rows -- not as three
    // copies, which is what makes a file look like a duplicate of
    // itself.
    {
        std::vector<Track> rows = {
            row("rekordbox", "11", "/stick/Contents/a.mp3", "Sorry", 200.0, 256),
            row("engine", "22", "/stick/Contents/a.mp3", "Sorry", 200.0, 0),
            row("onelibrary", "33", "/stick/Contents/a.mp3", "Sorry", 200.0, 256),
        };
        auto files = collapseCatalogRows(rows);
        assert(files.size() == 1);
        assert(files[0].catalogRows.size() == 3);
        assert(listsRow(files[0], "rekordbox", "11"));
        assert(listsRow(files[0], "engine", "22"));
        assert(listsRow(files[0], "onelibrary", "33"));
        std::cout << "case 1 (three catalogs listing one file collapse to one copy) OK\n";
    }

    // Engine stores no bitrate at all. Uncollapsed, a file catalogued
    // there scores 0 kbps and loses to anything -- which on a real stick
    // meant the survivor rule firing on all 435 groups from the Engine
    // side. Collapsed, the file knows the bitrate rekordbox recorded.
    {
        std::vector<Track> rows = {
            row("engine", "22", "/stick/Contents/a.mp3", "Sorry", 200.0, 0),
            row("rekordbox", "11", "/stick/Contents/a.mp3", "Sorry", 200.0, 256),
        };
        auto files = collapseCatalogRows(rows);
        assert(files.size() == 1);
        assert(files[0].bitrate == 256);
        // The base row stays the first one given: stable output for a
        // caller that passes its catalogs in a fixed order.
        assert(files[0].format == "engine" && files[0].sourceId == "22");
        std::cout << "case 2 (a gap is filled from another catalog's row for the same file) OK\n";
    }

    // Two genuinely different files of the same recording stay two
    // copies -- this is exactly what cleanup is for, and collapsing them
    // would hide the duplicate instead of finding it.
    {
        std::vector<Track> rows = {
            row("rekordbox", "11", "/stick/Contents/a.mp3", "Sorry", 200.0, 256),
            row("rekordbox", "12", "/stick/Contents/a-1.mp3", "Sorry", 200.0, 320),
        };
        auto files = collapseCatalogRows(rows);
        assert(files.size() == 2);
        std::cout << "case 3 (two different files of one recording stay two copies) OK\n";
    }

    // Case and separator spellings of one path name one file: exFAT and
    // NTFS are case-insensitive, and a stick written on Windows is read
    // on Linux.
    {
        std::vector<Track> rows = {
            row("rekordbox", "11", "/stick/Contents/A/Song.mp3", "Sorry", 200.0, 256),
            row("engine", "22", "\\stick\\contents\\a\\song.mp3", "Sorry", 200.0, 0),
        };
        auto files = collapseCatalogRows(rows);
        assert(files.size() == 1 && files[0].catalogRows.size() == 2);
        std::cout << "case 4 (case and backslash spellings are the same file) OK\n";
    }

    // A row with no resolvable path cannot be shown to be the same file
    // as anything, so it stands alone. Folding them together on a blank
    // key would merge unrelated files -- the one error here that deletes
    // music.
    {
        std::vector<Track> rows = {
            row("engine", "22", "", "Sorry", 200.0, 0),
            row("engine", "23", "", "Sorry", 200.0, 0),
        };
        auto files = collapseCatalogRows(rows);
        assert(files.size() == 2);
        assert(files[0].catalogRows.empty() && files[1].catalogRows.empty());
        std::cout << "case 5 (rows with no path are never folded together) OK\n";
    }

    // A streaming row's path names a cache on another machine, so it is
    // never evidence that two rows are one file.
    {
        Track streaming = row("engine", "22", "/home/dj/tidal-cache/x.flac", "Sorry", 200.0, 0);
        streaming.streamingSource = "TIDAL";
        Track other = row("engine", "23", "/home/dj/tidal-cache/x.flac", "Sorry", 200.0, 0);
        other.streamingSource = "TIDAL";
        auto files = collapseCatalogRows({streaming, other});
        assert(files.size() == 2);
        std::cout << "case 6 (streaming rows are never folded together) OK\n";
    }

    // Cues are the union: a cue that only one catalog knows about is
    // still a cue on that file, and cleanup must not decide otherwise.
    {
        Track rb = row("rekordbox", "11", "/stick/Contents/a.mp3", "Sorry", 200.0, 256);
        rb.cues = {domain::CuePoint{domain::CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"}};
        Track en = row("engine", "22", "/stick/Contents/a.mp3", "Sorry", 200.0, 0);
        en.cues = {domain::CuePoint{domain::CuePoint::Kind::Hot, 2, 40000.0, "#00FF00", "outro"}};
        auto files = collapseCatalogRows({rb, en});
        assert(files.size() == 1);
        assert(files[0].cues.size() == 2);
        std::cout << "case 7 (cues are the union across a file's rows) OK\n";
    }

    // The whole point, end to end: three catalogs, two real files of one
    // recording. Uncollapsed this is a group of six "copies" whose
    // removal list holds rows for the survivor's own file. Collapsed it
    // is what it actually is -- two copies, one to keep, one to remove,
    // and the removal carries the rows of all three catalogs.
    {
        std::vector<Track> rows = {
            row("rekordbox", "11", "/stick/Contents/a.mp3", "Sorry", 200.0, 320),
            row("engine", "21", "/stick/Contents/a.mp3", "Sorry", 200.0, 0),
            row("onelibrary", "31", "/stick/Contents/a.mp3", "Sorry", 200.0, 320),
            row("rekordbox", "12", "/stick/Contents/a-1.mp3", "Sorry", 200.0, 128),
            row("engine", "22", "/stick/Contents/a-1.mp3", "Sorry", 200.0, 0),
            row("onelibrary", "32", "/stick/Contents/a-1.mp3", "Sorry", 200.0, 128),
        };
        auto files = collapseCatalogRows(rows);
        assert(files.size() == 2);

        auto groups = domain::DuplicateTrackFinder::find(files);
        assert(groups.size() == 1);
        auto plan = domain::DuplicateCleanupPlanner::plan(groups[0]);
        assert(plan.survivor.filePath == "/stick/Contents/a.mp3");
        assert(plan.toRemove.size() == 1);
        assert(plan.toRemove[0].filePath == "/stick/Contents/a-1.mp3");
        // Not one row for the file being kept is in the removal list.
        for (const auto &r : plan.toRemove[0].catalogRows) {
            assert(!listsRow(plan.survivor, r.format, r.sourceId));
        }
        assert(plan.toRemove[0].catalogRows.size() == 3);
        std::cout << "case 8 (six rows, two files: one group, one removal, three rows to drop) OK\n";
    }

    std::cout << "all collapse_catalog_rows_test cases passed\n";
    return 0;
}
