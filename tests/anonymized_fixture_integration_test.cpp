#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <vector>

#include "application/use_cases/scan_library.hpp"
#include "application/use_cases/sync_libraries.hpp"
#include "domain/library_consistency.hpp"
#include "domain/library_statistics.hpp"
#include "infrastructure/engine/libdjinterop_engine_reader.hpp"
#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"
#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"

// Runs the app's real use cases against tests/fixtures/anonymized_library
// -- a committed, de-identified copy of Sebas's real ~1,400-track
// library (see BRAINSTORM.md's "For Developers, Maintainers and
// Testing" and MANIFEST.txt in that fixture directory for exactly what
// it is and isn't) -- at a realistic scale and variety today's other
// ~40 tests, all built on small synthetic fixtures, never exercise.
// Gated with CTest LABEL "integration": slower (real file I/O across
// ~1,400 tracks' worth of ANLZ files) and meant to run before a larger
// change or release, not on every build -- see docs/testing.md.
//
// SEABASS_SOURCE_DIR is injected by CMakeLists.txt (CMAKE_SOURCE_DIR)
// so this resolves the fixture correctly regardless of the build
// directory's own location.

namespace fs = std::filesystem;
using namespace seabass;

int main()
{
    fs::path fixturesRoot = fs::path(SEABASS_SOURCE_DIR) / "tests" / "fixtures" / "anonymized_library";
    fs::path rekordboxRoot = fixturesRoot / "rekordbox";
    fs::path engineRoot = fixturesRoot / "engine";
    assert(fs::exists(rekordboxRoot / "rekordbox" / "export.pdb"));
    assert(fs::exists(engineRoot / "Database2"));

    // Exact counts below were captured against this exact committed
    // fixture (via `seabass-cli scan`) when it was generated -- they'll
    // need updating if the fixture is ever regenerated from a
    // meaningfully different real library.

    infrastructure::rekordbox::KaitaiRekordboxReader rekordboxReader(rekordboxRoot.string());
    auto rekordboxTracks = application::ScanLibrary(rekordboxReader).execute();
    assert(rekordboxTracks.size() == 1370);
    int rekordboxWithCues = 0;
    int rekordboxTotalCues = 0;
    for (const auto &t : rekordboxTracks) {
        if (!t.cues.empty()) {
            ++rekordboxWithCues;
        }
        rekordboxTotalCues += static_cast<int>(t.cues.size());
    }
    assert(rekordboxWithCues == 42);
    assert(rekordboxTotalCues == 188);
    std::cout << "case 1 (rekordbox ScanLibrary at real scale: track/cue counts correct) OK\n";

    infrastructure::engine::LibdjinteropEngineReader engineReader(engineRoot.string());
    auto engineTracks = application::ScanLibrary(engineReader).execute();
    assert(engineTracks.size() == 1376);
    int engineWithCues = 0;
    int engineTotalCues = 0;
    for (const auto &t : engineTracks) {
        if (!t.cues.empty()) {
            ++engineWithCues;
        }
        engineTotalCues += static_cast<int>(t.cues.size());
    }
    assert(engineWithCues == 1376);
    assert(engineTotalCues == 1467);
    std::cout << "case 2 (Engine ScanLibrary at real scale: track/cue counts correct) OK\n";

    auto rekordboxStats = domain::LibraryStatisticsCalculator::calculate(rekordboxTracks);
    assert(rekordboxStats.trackCount == 1370);
    assert(rekordboxStats.playlistCount > 0);
    assert(!rekordboxStats.bpmDistribution.empty());
    assert(!rekordboxStats.tracksPerKey.empty());
    assert(!rekordboxStats.tracksPerFileFormat.empty());
    std::cout << "case 3 (LibraryStatisticsCalculator: sane, non-degenerate aggregates at real scale) OK\n";

    // The property the whole anonymized-fixture design exists to prove
    // (see anonymization_placeholder.hpp): the two catalogs were
    // anonymized in two completely independent runs, but describe the
    // same real stick, so domain::TrackMatcher should still find nearly
    // every track's counterpart -- confirmed manually via `seabass-cli
    // sync --dry-run` against this exact fixture (1370/1370, 100%)
    // before this test was written; asserting a 95% floor here rather
    // than exact equality so the test doesn't become newly-generated-
    // fixture-brittle over an incidental hash collision or two.
    auto now = std::chrono::system_clock::now();
    auto plans = application::SyncLibraries().execute(rekordboxTracks, engineTracks, now, now);
    auto matched = static_cast<int>(plans.size());
    assert(matched >= static_cast<int>(rekordboxTracks.size() * 0.95));
    std::cout << "case 4 (SyncLibraries: " << matched << "/" << rekordboxTracks.size()
               << " matched across independently-anonymized catalogs) OK\n";

    // Every fixture track's file path is a fake placeholder (see the
    // field-policy table in rekordbox_library_anonymizer.hpp) -- treat
    // the whole rekordbox catalog as "broken" (no real file exists) and
    // confirm the checker classifies ~1,370 real-shaped rows without
    // crashing, rather than only ever being exercised against a
    // handful of hand-built rows.
    auto issues = domain::LibraryConsistencyChecker::check({}, rekordboxTracks);
    assert(!issues.empty());
    std::cout << "case 5 (LibraryConsistencyChecker: " << issues.size()
               << " issue(s) classified without crashing at real scale) OK\n";

    // A real cue write, round-tripped through the real write path
    // (RekordboxCueWriter -> ANLZ .EXT PCO2 section -> re-read), with
    // ~1,370 other real-shaped tracks' ANLZ files sitting alongside it
    // -- exactly the scale/variety a tiny synthetic fixture can't
    // exercise. Works on a scratch copy so the committed fixture stays
    // untouched.
    fs::path scratchRoot = fs::temp_directory_path() / "seabass_anonymized_fixture_integration_test";
    fs::remove_all(scratchRoot);
    fs::create_directories(scratchRoot);
    fs::copy(rekordboxRoot, scratchRoot / "rekordbox", fs::copy_options::recursive);

    const domain::Track *target = nullptr;
    for (const auto &t : rekordboxTracks) {
        if (t.cues.empty()) {
            target = &t;
            break;
        }
    }
    assert(target != nullptr);
    std::string targetId = target->sourceId;

    domain::CuePoint newCue;
    newCue.kind = domain::CuePoint::Kind::Hot;
    newCue.hotCueNumber = 1;
    newCue.positionMs = 12345.0;
    newCue.color = "#FF0000";

    infrastructure::rekordbox::RekordboxCueWriter writer((scratchRoot / "rekordbox").string());
    writer.writeHotCues(targetId, {newCue});

    infrastructure::rekordbox::KaitaiRekordboxReader rereader((scratchRoot / "rekordbox").string());
    auto rereadTracks = application::ScanLibrary(rereader).execute();
    bool found = false;
    for (const auto &t : rereadTracks) {
        if (t.sourceId == targetId) {
            assert(t.cues.size() == 1);
            assert(t.cues[0].kind == domain::CuePoint::Kind::Hot);
            assert(t.cues[0].hotCueNumber == 1);
            assert(t.cues[0].positionMs == 12345.0);
            found = true;
        }
    }
    assert(found);
    std::cout << "case 6 (real hot-cue write round-trips correctly at realistic file scale) OK\n";

    fs::remove_all(scratchRoot);
    std::cout << "all cases passed\n";
    return 0;
}
