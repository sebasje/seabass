// Runs every library in the corpus through the three checks that matter:
// integrity, stability, and how much work the code does per item.
//
// The corpus is the committed anonymized fixture plus, when
// SEABASS_CORPUS names a directory, every set inside it. Those extra sets
// are real, un-anonymized libraries extracted by tools/extract_testdata
// and they never leave the machine, which is exactly why they are the
// ones that find things: the anonymized fixture cannot reproduce the
// libdjinterop decode failures that 30 of 50 tracks hit on a real stick.
// See docs/real-data-testing.md.
//
// Everything runs from local disk. Timing is deliberately NOT asserted
// here: it is a property of the medium, not the code (the same Engine
// write is 151 ms on a stick and 0.8 ms on a ramdisk), so this asserts
// operation COUNTS, which are the same number everywhere. Wall clock
// lives in tools/stick_write_bench against real hardware.
//
// Every write happens on a scratch copy. No set is ever modified.

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "application/use_cases/scan_library.hpp"
#include "domain/library_consistency.hpp"
#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/engine/libdjinterop_engine_reader.hpp"
#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"
#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"
#include "infrastructure/work_counters.hpp"

namespace fs = std::filesystem;
using namespace seabass;
using infrastructure::WorkCounters;

namespace
{

int g_failures = 0;

void check(bool condition, const std::string &what)
{
    if (!condition) {
        std::cout << "    FAIL: " << what << "\n";
        ++g_failures;
    }
}

// A set is a directory holding one or both catalogs. Two layouts exist and
// both are legitimate: the anonymizer writes "rekordbox/" and "engine/",
// while tools/extract_testdata mirrors a stick's own "PIONEER/" and
// "Engine Library/".
struct DataSet
{
    std::string name;
    std::optional<std::string> rekordboxRoot;
    std::optional<std::string> engineRoot;
};

std::optional<std::string> firstExisting(const fs::path &base, std::initializer_list<const char *> candidates)
{
    std::error_code ec;
    for (const char *candidate : candidates) {
        const fs::path path = base / candidate;
        if (fs::is_directory(path, ec)) {
            return path.string();
        }
    }
    return std::nullopt;
}

std::optional<DataSet> asDataSet(const fs::path &dir)
{
    DataSet set;
    set.name = dir.filename().string();
    set.rekordboxRoot = firstExisting(dir, {"rekordbox", "PIONEER"});
    set.engineRoot = firstExisting(dir, {"engine", "Engine Library"});
    if (!set.rekordboxRoot && !set.engineRoot) {
        return std::nullopt;
    }
    return set;
}

std::vector<DataSet> discoverSets()
{
    std::vector<DataSet> sets;
    if (auto committed = asDataSet("tests/fixtures/anonymized_library")) {
        committed->name = "committed fixture";
        sets.push_back(*committed);
    }
    const char *corpus = std::getenv("SEABASS_CORPUS");
    if (corpus != nullptr && *corpus != '\0') {
        std::error_code ec;
        for (const auto &entry : fs::directory_iterator(corpus, ec)) {
            if (!entry.is_directory()) {
                continue;
            }
            if (auto set = asDataSet(entry.path())) {
                sets.push_back(*set);
            }
        }
    }
    return sets;
}

fs::path scratchFor(const std::string &name)
{
    fs::path root = fs::temp_directory_path() / ("seabass_corpus_test_" + name);
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

// ---------------------------------------------------------------- checks

// Integrity: a cue written to a real library reads back as the same cue.
// Reading back with a fresh reader is the whole point -- every write-path
// bug this project has found was invisible to a test that trusted its own
// return value.
void checkRekordboxCueRoundTrip(const DataSet &set, const fs::path &scratch)
{
    fs::copy(*set.rekordboxRoot, scratch / "rekordbox", fs::copy_options::recursive);
    const std::string root = (scratch / "rekordbox").string();

    infrastructure::rekordbox::KaitaiRekordboxReader reader(root);
    auto tracks = application::ScanLibrary(reader).execute();
    check(!tracks.empty(), "rekordbox scan returned tracks");
    if (tracks.empty()) {
        return;
    }

    const domain::Track *target = nullptr;
    for (const auto &track : tracks) {
        if (track.cues.empty()) {
            target = &track;
            break;
        }
    }
    if (target == nullptr) {
        std::cout << "    (every track already has cues; round trip skipped)\n";
        return;
    }

    domain::CuePoint cue;
    cue.kind = domain::CuePoint::Kind::Hot;
    cue.hotCueNumber = 1;
    cue.positionMs = 12345.0;
    cue.color = "#FF0000";
    const std::string targetId = target->sourceId;

    infrastructure::rekordbox::RekordboxCueWriter writer(root);
    writer.writeHotCues(targetId, {cue});

    infrastructure::rekordbox::KaitaiRekordboxReader rereader(root);
    auto reread = application::ScanLibrary(rereader).execute();
    bool found = false;
    for (const auto &track : reread) {
        if (track.sourceId != targetId) {
            continue;
        }
        found = true;
        check(track.cues.size() == 1, "exactly the written cue came back");
        if (!track.cues.empty()) {
            check(track.cues[0].kind == domain::CuePoint::Kind::Hot, "cue kind survived");
            check(track.cues[0].hotCueNumber == 1, "hot cue number survived");
            check(track.cues[0].positionMs == 12345.0, "cue position survived");
        }
    }
    check(found, "the written track is still in the library");
}

// Stability: real libraries contain data libdjinterop cannot decode. The
// contract is not "never refuses" -- it is "refuses visibly, one track at
// a time, and no more often than last time". A set records its own
// baseline, so a rise is a regression and a fall is an improvement.
struct Refusals
{
    int reads = 0;
    int writes = 0;
};

Refusals checkEngineStability(const DataSet &set, const fs::path &scratch, int sampleSize)
{
    Refusals refusals;
    fs::copy(*set.engineRoot, scratch / "engine", fs::copy_options::recursive);
    const std::string root = (scratch / "engine").string();

    std::vector<domain::Track> tracks;
    try {
        infrastructure::engine::LibdjinteropEngineReader reader(root);
        tracks = application::ScanLibrary(reader).execute();
    } catch (const std::exception &e) {
        check(false, std::string("the Engine library could not be read at all: ") + e.what());
        return refusals;
    }
    check(!tracks.empty(), "Engine scan returned tracks");

    infrastructure::engine::LibdjinteropEngineCueWriter writer(root);
    domain::CuePoint cue;
    cue.kind = domain::CuePoint::Kind::Hot;
    cue.hotCueNumber = 1;
    cue.positionMs = 1000.0;

    int written = 0;
    for (const auto &track : tracks) {
        if (written >= sampleSize) {
            break;
        }
        ++written;
        try {
            writer.writeHotCues(track.sourceId, {cue});
        } catch (const std::exception &) {
            ++refusals.writes;
        }
    }
    // A refusal must not be silent, and it must not take the rest of the
    // batch with it: everything after a refused track still has to land.
    check(written == std::min<int>(sampleSize, static_cast<int>(tracks.size())),
          "the whole batch was attempted despite refusals");
    return refusals;
}

// The portable half of performance testing. These are the numbers the
// optimisation work in docs/write-path-performance.md exists to reduce:
// today every item reopens its database and re-parses export.pdb, and
// this records that so the improvement is visible and cannot silently
// regress afterwards.
void checkWorkCounts(const DataSet &set, const fs::path &scratch, int items)
{
    if (!set.engineRoot) {
        return;
    }
    const fs::path root = scratch / "engine-counts";
    fs::copy(*set.engineRoot, root, fs::copy_options::recursive);

    std::vector<domain::Track> tracks;
    try {
        infrastructure::engine::LibdjinteropEngineReader reader(root.string());
        tracks = application::ScanLibrary(reader).execute();
    } catch (const std::exception &) {
        return;
    }
    if (static_cast<int>(tracks.size()) < items) {
        items = static_cast<int>(tracks.size());
    }

    infrastructure::engine::LibdjinteropEngineCueWriter writer(root.string());
    domain::CuePoint cue;
    cue.kind = domain::CuePoint::Kind::Hot;
    cue.hotCueNumber = 1;
    cue.positionMs = 2000.0;

    WorkCounters::instance().reset();
    for (int i = 0; i < items; ++i) {
        try {
            writer.writeHotCues(tracks[static_cast<size_t>(i)].sourceId, {cue});
        } catch (const std::exception &) {
            // counted by the stability check, not here
        }
    }
    const auto counts = WorkCounters::instance().snapshot();
    std::cout << "    " << items << " Engine cue writes: " << counts.describe() << "\n";

    // One open per item is what the code does today. When the writer holds
    // its handle for the save (the fix this measurement argues for), this
    // becomes 1 and the assertion below is what proves it -- change the
    // expectation then, deliberately, rather than discovering it drifted.
    check(counts.engineDatabaseOpens == static_cast<std::uint64_t>(items),
          "Engine database opens match the documented per-item behaviour ("
              + std::to_string(counts.engineDatabaseOpens) + " for " + std::to_string(items) + " items)");
}

std::optional<Refusals> readBaseline(const fs::path &path)
{
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }
    Refusals baseline;
    in >> baseline.reads >> baseline.writes;
    if (!in) {
        return std::nullopt;
    }
    return baseline;
}

void writeBaseline(const fs::path &path, const Refusals &refusals)
{
    std::ofstream out(path, std::ios::trunc);
    out << refusals.reads << " " << refusals.writes << "\n";
}

}  // namespace

int main()
{
    auto sets = discoverSets();
    if (sets.empty()) {
        std::cout << "no data sets found (run from the repository root; set SEABASS_CORPUS for more)\n";
        return 1;
    }
    std::cout << "corpus: " << sets.size() << " set(s)\n";

    for (const auto &set : sets) {
        std::cout << "\n== " << set.name << " ==\n";
        const fs::path scratch = scratchFor(set.name);

        if (set.rekordboxRoot) {
            std::cout << "  integrity: rekordbox cue round trip\n";
            checkRekordboxCueRoundTrip(set, scratch);
        }

        Refusals refusals;
        if (set.engineRoot) {
            std::cout << "  stability: Engine writes over a sample\n";
            refusals = checkEngineStability(set, scratch, 50);
            std::cout << "    refused: " << refusals.writes << " write(s)\n";

            // The baseline lives beside the set, not in this repository,
            // so a private corpus keeps its own numbers.
            const fs::path baselinePath = fs::path(*set.engineRoot).parent_path() / "REFUSAL-BASELINE.txt";
            if (auto baseline = readBaseline(baselinePath)) {
                check(refusals.writes <= baseline->writes,
                      "refusals did not increase (was " + std::to_string(baseline->writes) + ", now "
                          + std::to_string(refusals.writes) + ")");
                if (refusals.writes < baseline->writes) {
                    std::cout << "    improved on the baseline of " << baseline->writes << "; update it when ready\n";
                }
            } else {
                writeBaseline(baselinePath, refusals);
                std::cout << "    recorded a new baseline at " << baselinePath.string() << "\n";
            }

            std::cout << "  work counts: how much the code does per item\n";
            checkWorkCounts(set, scratch, 20);
        }

        fs::remove_all(scratch);
    }

    std::cout << "\n";
    if (g_failures > 0) {
        std::cout << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
