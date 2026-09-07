// Runs every library in the corpus through the checks that matter:
// integrity, stability, and how much work the code does per item.
//
// The corpus is the committed anonymized fixture plus, when
// SEABASS_CORPUS names a directory, every set inside it. Those extra sets
// are real, un-anonymized libraries extracted by tools/extract_testdata
// and they never leave the machine, which is exactly why they are the
// ones that find things: anonymized titles and artists are placeholders,
// so they cannot exercise the fuzzy matching that Sync and Duplicates are
// built on. See docs/real-data-testing.md.
//
// Everything runs from local disk. Timing is deliberately NOT asserted
// here: it is a property of the medium, not the code (the same Engine
// write is 151 ms on a stick and 0.8 ms on a ramdisk), so this asserts
// operation COUNTS, which are the same number everywhere. Wall clock
// lives in tools/stick_write_bench against real hardware.
//
// Every write happens on a scratch copy. No set is ever modified.
//
// Counts that used to be hardcoded to the committed fixture (1370 tracks,
// 188 cues, and so on) are now recorded per set in SET-EXPECTATIONS.txt
// beside the set, written on first sight and asserted afterwards. That
// keeps the fixture's exact regression guard while giving every other
// library the same one, which is what catches a reader regression against
// data nobody has looked at by hand.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "application/use_cases/scan_library.hpp"
#include "application/use_cases/sync_libraries.hpp"
#include "domain/library_consistency.hpp"
#include "domain/library_statistics.hpp"
#include "infrastructure/backup/filesystem_backup_store.hpp"
#include "infrastructure/cleanup/pending_deletion_applier.hpp"
#include "infrastructure/cleanup/pending_deletion_manifest.hpp"
#include "infrastructure/cleanup/pending_deletion_resolver.hpp"
#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/engine/libdjinterop_engine_reader.hpp"
#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"
#include "infrastructure/rekordbox/rekordbox_cleanup_writer.hpp"
#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"
#include "infrastructure/work_counters.hpp"
#include "infrastructure/zip_archive_reader.hpp"

namespace fs = std::filesystem;
using namespace seabass;
using infrastructure::WorkCounters;

namespace
{

int g_failures = 0;
std::string g_set;

bool check(bool condition, const std::string &what)
{
    if (!condition) {
        std::cout << "    FAIL [" << g_set << "]: " << what << "\n";
        ++g_failures;
    }
    return condition;
}

void pass(const std::string &what)
{
    std::cout << "    ok: " << what << "\n";
}

// ------------------------------------------------------------- data sets

// A set is a directory holding one or both catalogs. Two layouts exist and
// both are legitimate: the anonymizer writes "rekordbox/" and "engine/",
// while tools/extract_testdata mirrors a stick's own "PIONEER/" and
// "Engine Library/". Which layout it is also says whether the content is
// anonymized, which decides whether the placeholder-shape cases apply.
struct DataSet
{
    std::string name;
    fs::path root;
    // Where this set's recorded numbers live. Normally inside the set, but
    // a zipped set is unpacked into a scratch directory that is deleted at
    // the end of the run, so its file has to live beside the archive
    // instead -- otherwise every run re-records and the guard never fires.
    fs::path expectationsPath;
    std::optional<std::string> rekordboxRoot;
    std::optional<std::string> engineRoot;
    bool anonymized = false;
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
    set.root = dir;
    std::error_code ec;
    set.anonymized = fs::is_directory(dir / "rekordbox", ec) || fs::is_directory(dir / "engine", ec);
    set.expectationsPath = dir / "SET-EXPECTATIONS.txt";
    set.rekordboxRoot = firstExisting(dir, {"rekordbox", "PIONEER"});
    set.engineRoot = firstExisting(dir, {"engine", "Engine Library"});
    if (!set.rekordboxRoot && !set.engineRoot) {
        return std::nullopt;
    }
    return set;
}

// Where a zipped set gets unpacked, once, before anything reads it.
fs::path unpackedSetsRoot()
{
    return fs::temp_directory_path() / "seabass_corpus_unpacked";
}

// A set kept as one file rather than six thousand. Unpacked into a scratch
// directory and then treated exactly like a directory set.
std::optional<DataSet> unpackZippedSet(const fs::path &zipPath)
{
    const std::string stem = zipPath.stem().string();
    const fs::path target = unpackedSetsRoot() / stem;
    std::error_code ec;
    fs::remove_all(target, ec);
    try {
        infrastructure::extractZipArchive(zipPath, target);
    } catch (const std::exception &e) {
        std::cout << "skipping " << zipPath.filename().string() << ": could not unpack it (" << e.what() << ")\n";
        return std::nullopt;
    }
    auto set = asDataSet(target);
    if (!set) {
        std::cout << "skipping " << zipPath.filename().string()
                  << ": unpacked, but holds neither catalog (no rekordbox/PIONEER, no engine/Engine Library)\n";
        fs::remove_all(target, ec);
        return std::nullopt;
    }
    set->name = stem;
    set->expectationsPath = zipPath.parent_path() / (stem + "-EXPECTATIONS.txt");
    std::cout << "unpacked " << zipPath.filename().string() << " into " << target.string() << "\n";
    return set;
}

std::vector<DataSet> discoverSets()
{
    std::vector<DataSet> sets;
    if (auto committed = asDataSet("tests/fixtures/anonymized_library")) {
        committed->name = "committed fixture";
        sets.push_back(*committed);
    } else {
        std::cout << "WARNING: the committed fixture was not found -- run from the repository root\n";
        ++g_failures;
    }
    const char *corpus = std::getenv("SEABASS_CORPUS");
    if (corpus == nullptr || *corpus == '\0') {
        std::cout << "SEABASS_CORPUS is not set: running against the committed fixture alone.\n"
                     "Matching cases need real titles and artists, so they will be skipped.\n";
        return sets;
    }
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(corpus, ec)) {
        if (!entry.is_directory()) {
            if (entry.path().extension() == ".zip") {
                if (auto set = unpackZippedSet(entry.path())) {
                    sets.push_back(*set);
                }
                continue;
            }
            std::cout << "skipping " << entry.path().filename().string()
                      << ": neither a directory nor a .zip\n";
            continue;
        }
        if (auto set = asDataSet(entry.path())) {
            sets.push_back(*set);
        } else {
            std::cout << "skipping " << entry.path().filename().string()
                      << ": holds neither catalog (no rekordbox/PIONEER, no engine/Engine Library)\n";
        }
    }
    return sets;
}

// ---------------------------------------------------------- expectations

// Numbers a set is expected to keep producing. Recorded the first time a
// set is seen, asserted every time after. A deliberate change (a
// regenerated fixture, a set re-extracted from a stick that has moved on)
// means deleting the file and letting the next run record it again.
class Expectations
{
public:
    explicit Expectations(fs::path path) : m_path(std::move(path))
    {
        std::ifstream in(m_path);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            std::istringstream parsed(line);
            std::string key;
            long long value = 0;
            if (parsed >> key >> value) {
                m_values[key] = value;
            }
        }
    }

    // Returns true when the value matched what was recorded, or was
    // recorded now for the first time.
    bool expect(const std::string &key, long long actual, const std::string &what)
    {
        auto it = m_values.find(key);
        if (it == m_values.end()) {
            m_values[key] = actual;
            m_dirty = true;
            std::cout << "    recorded " << key << " = " << actual << "\n";
            return true;
        }
        return check(it->second == actual,
                     what + " (" + key + " was " + std::to_string(it->second) + ", now " + std::to_string(actual) + ")");
    }

    // For a value that may legitimately improve but must never regress.
    bool expectAtLeast(const std::string &key, long long actual, const std::string &what)
    {
        auto it = m_values.find(key);
        if (it == m_values.end()) {
            m_values[key] = actual;
            m_dirty = true;
            std::cout << "    recorded " << key << " = " << actual << "\n";
            return true;
        }
        if (actual > it->second) {
            std::cout << "    " << key << " improved on " << it->second << " (now " << actual
                      << "); update the file when ready\n";
            return true;
        }
        return check(actual >= it->second,
                     what + " (" + key + " was " + std::to_string(it->second) + ", now " + std::to_string(actual) + ")");
    }

    void save()
    {
        if (!m_dirty) {
            return;
        }
        std::ofstream out(m_path, std::ios::trunc);
        out << "# Written by corpus_test. Delete this file to re-record after a\n"
               "# deliberate change to the data set itself.\n";
        for (const auto &[key, value] : m_values) {
            out << key << " " << value << "\n";
        }
    }

private:
    fs::path m_path;
    std::map<std::string, long long> m_values;
    bool m_dirty = false;
};

// ---------------------------------------------------------------- shared

struct Catalogs
{
    std::vector<domain::Track> rekordbox;
    std::vector<domain::Track> engine;
};

fs::path scratchFor(const std::string &name)
{
    std::string safe;
    for (char c : name) {
        safe += (c == ' ' || c == '/') ? '_' : c;
    }
    fs::path root = fs::temp_directory_path() / ("seabass_corpus_test_" + safe);
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

// A writable copy of the set's rekordbox tree. Several cases mutate one,
// and each needs to start from the untouched original.
fs::path freshRekordboxCopy(const DataSet &set, const fs::path &scratch, const std::string &name)
{
    fs::path target = scratch / name;
    fs::remove_all(target);
    fs::copy(*set.rekordboxRoot, target, fs::copy_options::recursive);
    return target;
}

int countCues(const std::vector<domain::Track> &tracks)
{
    int total = 0;
    for (const auto &t : tracks) {
        total += static_cast<int>(t.cues.size());
    }
    return total;
}

int countWithCues(const std::vector<domain::Track> &tracks)
{
    int total = 0;
    for (const auto &t : tracks) {
        if (!t.cues.empty()) {
            ++total;
        }
    }
    return total;
}

// ------------------------------------------------------ integrity cases

// Case 1 and 2: both readers return the same library they returned last
// time. Exact numbers rather than "more than zero", because a reader that
// silently drops a row on data nobody inspects by hand is precisely the
// regression this corpus exists to catch.
void caseScanCounts(const DataSet &set, Catalogs &catalogs, Expectations &expected)
{
    if (set.rekordboxRoot) {
        try {
            infrastructure::rekordbox::KaitaiRekordboxReader reader(*set.rekordboxRoot);
            catalogs.rekordbox = application::ScanLibrary(reader).execute();
        } catch (const std::exception &e) {
            check(false, std::string("the rekordbox catalog could not be read at all: ") + e.what());
        }
        if (check(!catalogs.rekordbox.empty(), "rekordbox scan returned tracks")) {
            expected.expect("rekordbox.tracks", static_cast<long long>(catalogs.rekordbox.size()),
                            "rekordbox track count unchanged");
            expected.expect("rekordbox.tracksWithCues", countWithCues(catalogs.rekordbox),
                            "rekordbox tracks-with-cues unchanged");
            expected.expect("rekordbox.cues", countCues(catalogs.rekordbox), "rekordbox cue count unchanged");
            pass("case 1: rekordbox scan at real scale, track and cue counts hold");
        }
    }
    if (set.engineRoot) {
        try {
            infrastructure::engine::LibdjinteropEngineReader reader(*set.engineRoot);
            catalogs.engine = application::ScanLibrary(reader).execute();
        } catch (const std::exception &e) {
            check(false, std::string("the Engine catalog could not be read at all: ") + e.what());
        }
        if (check(!catalogs.engine.empty(), "Engine scan returned tracks")) {
            expected.expect("engine.tracks", static_cast<long long>(catalogs.engine.size()),
                            "Engine track count unchanged");
            expected.expect("engine.tracksWithCues", countWithCues(catalogs.engine),
                            "Engine tracks-with-cues unchanged");
            expected.expect("engine.cues", countCues(catalogs.engine), "Engine cue count unchanged");
            pass("case 2: Engine scan at real scale, track and cue counts hold");
        }
    }
}

// Case 3: the statistics a page draws are non-degenerate on a real
// library. A calculator that returns an empty distribution renders an
// empty page, which no unit test on four synthetic tracks would notice.
void caseStatistics(const Catalogs &catalogs)
{
    if (catalogs.rekordbox.empty()) {
        return;
    }
    auto stats = domain::LibraryStatisticsCalculator::calculate(catalogs.rekordbox);
    check(stats.trackCount == static_cast<int>(catalogs.rekordbox.size()), "statistics counted every track");
    check(stats.playlistCount > 0, "statistics found playlists");
    check(!stats.bpmDistribution.empty(), "statistics produced a BPM distribution");
    check(!stats.tracksPerKey.empty(), "statistics produced a key distribution");
    check(!stats.tracksPerFileFormat.empty(), "statistics produced a file-format distribution");
    pass("case 3: statistics are sane and non-degenerate at real scale");
}

// Case 4: the two catalogs describe the same stick, so matching should
// pair nearly every track. This is the case the raw sets exist for: on an
// anonymized set the matcher runs on placeholder strings, whose fuzziness
// is nothing like real punctuation, accents, "feat." spellings and remix
// suffixes.
void caseSyncMatching(const DataSet &set, const Catalogs &catalogs, Expectations &expected)
{
    if (catalogs.rekordbox.empty() || catalogs.engine.empty()) {
        std::cout << "    skipped case 4 (matching): this set has only one catalog\n";
        return;
    }
    auto now = std::chrono::system_clock::now();
    auto plans = application::SyncLibraries().execute(catalogs.rekordbox, catalogs.engine, now, now);
    const auto matched = static_cast<long long>(plans.size());

    if (set.anonymized) {
        // The property the anonymized fixture exists to prove: the two
        // catalogs were anonymized in two independent runs and must still
        // pair up. A floor rather than equality so an incidental hash
        // collision does not fail a freshly generated fixture.
        check(matched >= static_cast<long long>(catalogs.rekordbox.size() * 0.95),
              "at least 95% of tracks matched across independently anonymized catalogs");
    }
    expected.expectAtLeast("sync.matched", matched, "matching did not get worse");
    std::cout << "    matched " << matched << " of " << catalogs.rekordbox.size() << "\n";
    pass(set.anonymized ? "case 4: matching holds across independently anonymized catalogs"
                        : "case 4: matching holds on real titles and artists");
}

// Case 4b: no two different real tracks collapsed onto the same
// placeholder. Guards a real bug: the placeholder used to put its
// human-readable label before its hash, and the writer preserves the
// original field's byte length, so short titles lost the hash entirely
// and 7.6% of a real library collapsed onto a handful of strings.
// Anonymized sets only -- a raw library legitimately holds duplicates.
void casePlaceholderCollisions(const DataSet &set, const Catalogs &catalogs)
{
    if (!set.anonymized) {
        std::cout << "    skipped case 4b (placeholder collisions): this set is not anonymized\n";
        return;
    }
    auto collisions = [](const std::vector<domain::Track> &tracks) {
        std::map<std::pair<std::string, std::string>, int> byTitleArtist;
        std::map<std::string, int> byFilename;
        for (const auto &t : tracks) {
            ++byTitleArtist[{t.title, t.artist}];
            ++byFilename[t.filename];
        }
        int titleArtist = 0;
        int filename = 0;
        for (const auto &[k, v] : byTitleArtist) {
            if (v > 1) {
                titleArtist += v;
            }
        }
        for (const auto &[k, v] : byFilename) {
            if (v > 1) {
                filename += v;
            }
        }
        return std::make_pair(titleArtist, filename);
    };
    if (!catalogs.rekordbox.empty()) {
        auto [titleArtist, filename] = collisions(catalogs.rekordbox);
        check(titleArtist == 0, "no two rekordbox tracks share an anonymized title and artist");
        check(filename == 0, "no two rekordbox tracks share an anonymized filename");
    }
    if (!catalogs.engine.empty()) {
        auto [titleArtist, filename] = collisions(catalogs.engine);
        check(titleArtist == 0, "no two Engine tracks share an anonymized title and artist");
        check(filename == 0, "no two Engine tracks share an anonymized filename");
    }
    pass("case 4b: every anonymized track is still distinguishable from every other");
}

// Case 5: the consistency checker classifies a real library without
// falling over. Anonymized paths point at files that do not exist, so
// every row is "broken" there; a raw set's files may genuinely be
// present, so the count is reported, not asserted to be non-zero.
void caseConsistencyChecker(const Catalogs &catalogs)
{
    if (catalogs.rekordbox.empty()) {
        return;
    }
    std::vector<domain::LibraryConsistencyIssue> issues;
    try {
        issues = domain::LibraryConsistencyChecker::check({}, catalogs.rekordbox);
    } catch (const std::exception &e) {
        check(false, std::string("the consistency checker threw at real scale: ") + e.what());
        return;
    }
    std::cout << "    classified " << issues.size() << " issue(s)\n";
    pass("case 5: the consistency checker survives a real library");
}

// Case 6: a cue written to a real library reads back as the same cue.
// Reading back with a fresh reader is the whole point -- every write-path
// bug this project has found was invisible to a test that trusted its own
// return value.
void caseCueRoundTrip(const DataSet &set, const fs::path &scratch, const Catalogs &catalogs)
{
    if (catalogs.rekordbox.empty()) {
        return;
    }
    const domain::Track *target = nullptr;
    for (const auto &track : catalogs.rekordbox) {
        if (track.cues.empty()) {
            target = &track;
            break;
        }
    }
    if (target == nullptr) {
        std::cout << "    skipped case 6 (round trip): every track already has cues\n";
        return;
    }
    const std::string targetId = target->sourceId;
    const fs::path root = freshRekordboxCopy(set, scratch, "roundtrip");

    domain::CuePoint cue;
    cue.kind = domain::CuePoint::Kind::Hot;
    cue.hotCueNumber = 1;
    cue.positionMs = 12345.0;
    cue.color = "#FF0000";

    infrastructure::rekordbox::RekordboxCueWriter writer(root.string());
    writer.writeHotCues(targetId, {cue});

    infrastructure::rekordbox::KaitaiRekordboxReader rereader(root.string());
    auto reread = application::ScanLibrary(rereader).execute();
    bool found = false;
    for (const auto &track : reread) {
        if (track.sourceId != targetId) {
            continue;
        }
        found = true;
        if (check(track.cues.size() == 1, "exactly the written cue came back")) {
            check(track.cues[0].kind == domain::CuePoint::Kind::Hot, "cue kind survived");
            check(track.cues[0].hotCueNumber == 1, "hot cue number survived");
            check(track.cues[0].positionMs == 12345.0, "cue position survived");
        }
    }
    check(found, "the written track is still in the library");
    fs::remove_all(root);
    pass("case 6: a real hot-cue write round-trips at realistic file scale");
}

// Case 7: the orphaned-file deletion chain end to end -- a real cleanup
// pass, a real manifest entry, the stale-manifest refusal, then the one
// operation in this app that destroys real audio.
void casePendingDeletion(const DataSet &set, const fs::path &scratch, const Catalogs &catalogs)
{
    if (catalogs.rekordbox.size() < 30) {
        std::cout << "    skipped case 7 (deletion chain): too few rekordbox tracks\n";
        return;
    }
    const fs::path root = freshRekordboxCopy(set, scratch, "deletion");
    infrastructure::rekordbox::KaitaiRekordboxReader reader(root.string());
    auto tracks = application::ScanLibrary(reader).execute();
    const size_t before = tracks.size();

    domain::Track doomed = tracks[10];
    domain::Track survivor = tracks[20];
    if (!check(doomed.sourceId != survivor.sourceId, "picked two distinct tracks")) {
        return;
    }
    // A real set's audio may actually exist and must never be deleted, so
    // this case writes its own stand-in file inside the scratch tree and
    // points the manifest at that instead of the track's real path.
    const fs::path victim = scratch / "deletion-victim.mp3";
    std::ofstream(victim) << "fake audio data";
    doomed.filePath = victim.string();

    fs::path manifestPath = scratch / ".seabass-pending-deletions.jsonl";
    fs::remove(manifestPath);
    infrastructure::cleanup::PendingDeletionManifest manifest(manifestPath.string());

    infrastructure::rekordbox::RekordboxCleanupWriter cleanupWriter(root.string());
    cleanupWriter.removeTrackReplacingWith(doomed.sourceId, survivor.sourceId);

    infrastructure::cleanup::PendingDeletion pending;
    pending.format = "rekordbox";
    pending.filePath = doomed.filePath;
    pending.title = doomed.title;
    pending.artist = doomed.artist;
    pending.backupId = "corpus-test";
    manifest.append(pending);

    infrastructure::rekordbox::KaitaiRekordboxReader postRemovalReader(root.string());
    auto postRemoval = application::ScanLibrary(postRemovalReader).execute();
    check(postRemoval.size() == before - 1, "the removed row is gone from a fresh scan");
    bool stillThere = false;
    for (const auto &t : postRemoval) {
        if (t.sourceId == doomed.sourceId) {
            stillThere = true;
        }
    }
    check(!stillThere, "the removed row is really gone, not just uncounted");
    pass("case 7a: a real cleanup pass removes the row and records the pending deletion");

    // Stale manifest: some other track now legitimately occupies that
    // path. The resolver must refuse the deletion even though the
    // manifest still lists it.
    auto staleScan = postRemoval;
    staleScan[0].filePath = doomed.filePath;
    auto stale = infrastructure::cleanup::resolvePendingDeletions(manifest.list(), staleScan);
    check(stale.safeToDelete.empty(), "a still-referenced path is not offered for deletion");
    if (check(stale.stillReferenced.size() == 1, "the still-referenced path is reported as such")) {
        check(stale.stillReferenced[0].filePath == doomed.filePath, "the right path was protected");
    }
    check(fs::exists(victim), "the protected file is still on disk");
    pass("case 7b: a stale manifest entry is refused, not acted on");

    auto real = infrastructure::cleanup::resolvePendingDeletions(manifest.list(), postRemoval);
    if (check(real.safeToDelete.size() == 1, "the genuinely orphaned file is offered for deletion")) {
        check(real.stillReferenced.empty(), "nothing else was flagged");
        auto outcomes = infrastructure::cleanup::applyPendingDeletions(real.safeToDelete, manifest);
        if (check(outcomes.size() == 1, "one deletion was attempted")) {
            check(outcomes[0].status == infrastructure::cleanup::PendingDeletionOutcome::Status::Deleted,
                  "the deletion reported success");
        }
        check(!fs::exists(victim), "the orphaned file is genuinely gone from disk");
        check(manifest.list().empty(), "the manifest was cleared");
    }
    fs::remove_all(root);
    pass("case 7c: a genuinely orphaned file is deleted and the manifest cleared");
}

// Case 8: a batch interrupted partway is fully revertible from the one
// backup taken before it started, including real playlist membership --
// not just the bare row.
void caseInterruptedBatch(const DataSet &set, const fs::path &scratch, const Catalogs &catalogs)
{
    if (catalogs.rekordbox.size() < 30) {
        std::cout << "    skipped case 8 (rollback): too few rekordbox tracks\n";
        return;
    }
    const fs::path root = freshRekordboxCopy(set, scratch, "rollback");
    infrastructure::rekordbox::KaitaiRekordboxReader reader(root.string());
    auto tracks = application::ScanLibrary(reader).execute();
    const size_t before = tracks.size();

    // A track with real playlist membership, so the restore genuinely has
    // playlist repointing to undo.
    const domain::Track *doomed = nullptr;
    for (const auto &t : tracks) {
        if (!t.playlists.empty()) {
            doomed = &t;
            break;
        }
    }
    if (doomed == nullptr) {
        std::cout << "    skipped case 8 (rollback): no track has playlist membership\n";
        fs::remove_all(root);
        return;
    }
    const std::string doomedId = doomed->sourceId;
    const std::string doomedTitle = doomed->title;
    const auto playlistsBefore = doomed->playlists;

    const domain::Track *survivor = nullptr;
    for (const auto &t : tracks) {
        if (t.sourceId != doomedId) {
            survivor = &t;
            break;
        }
    }
    if (!check(survivor != nullptr, "found a survivor for the rollback case")) {
        return;
    }

    // Mirrors the save loop's own rule: export.pdb is the one shared file
    // every group's write touches, backed up exactly once before any of
    // them run.
    infrastructure::backup::FilesystemBackupStore backupStore((scratch / "rollback-backups").string());
    const std::string pdbPath = (root / "rekordbox" / "export.pdb").string();
    if (!check(fs::exists(pdbPath), "the catalog database is where the backup expects it")) {
        return;
    }
    auto record = backupStore.backup({pdbPath}, "duplicate-file-cleanup");

    {
        infrastructure::rekordbox::RekordboxCleanupWriter cleanupWriter(root.string());
        cleanupWriter.removeTrackReplacingWith(doomedId, survivor->sourceId);
    }

    {
        infrastructure::rekordbox::KaitaiRekordboxReader midReader(root.string());
        auto mid = application::ScanLibrary(midReader).execute();
        check(mid.size() == before - 1, "the first group's removal landed");
        bool present = false;
        for (const auto &t : mid) {
            if (t.sourceId == doomedId) {
                present = true;
            }
        }
        check(!present, "the first group's doomed row is gone");
        pass("case 8a: an interrupted batch leaves exactly the groups it finished");
    }

    check(backupStore.restore(record.id), "the backup restored");

    {
        infrastructure::rekordbox::KaitaiRekordboxReader postReader(root.string());
        auto post = application::ScanLibrary(postReader).execute();
        check(post.size() == before, "every row is back");
        const domain::Track *restored = nullptr;
        for (const auto &t : post) {
            if (t.sourceId == doomedId) {
                restored = &t;
            }
        }
        if (check(restored != nullptr, "the removed row came back")) {
            check(restored->title == doomedTitle, "it came back with its title");
            if (check(restored->playlists.size() == playlistsBefore.size(),
                      "it came back with all its playlist entries")) {
                for (size_t i = 0; i < playlistsBefore.size(); ++i) {
                    check(restored->playlists[i].name == playlistsBefore[i].name, "playlist name survived the restore");
                    check(restored->playlists[i].position == playlistsBefore[i].position,
                          "playlist position survived the restore");
                }
            }
        }
        pass("case 8b: restoring the one upfront backup reverts the batch completely");
    }
    fs::remove_all(root);
}

// ------------------------------------------------------- stability cases

// Real libraries contain data libdjinterop cannot decode. The contract is
// not "never refuses" -- it is "refuses visibly, one track at a time, and
// no more often than last time".
void caseEngineStability(const DataSet &set, const fs::path &scratch, const Catalogs &catalogs, int sampleSize,
                         Expectations &expected)
{
    if (catalogs.engine.empty()) {
        return;
    }
    const fs::path root = scratch / "engine-stability";
    fs::remove_all(root);
    fs::copy(*set.engineRoot, root, fs::copy_options::recursive);

    infrastructure::engine::LibdjinteropEngineCueWriter writer(root.string());
    domain::CuePoint cue;
    cue.kind = domain::CuePoint::Kind::Hot;
    cue.hotCueNumber = 1;
    cue.positionMs = 1000.0;

    int attempted = 0;
    int refused = 0;
    for (const auto &track : catalogs.engine) {
        if (attempted >= sampleSize) {
            break;
        }
        ++attempted;
        try {
            writer.writeHotCues(track.sourceId, {cue});
        } catch (const std::exception &) {
            ++refused;
        }
    }
    // A refusal must not be silent, and it must not take the rest of the
    // batch with it: everything after a refused track still has to land.
    check(attempted == std::min<int>(sampleSize, static_cast<int>(catalogs.engine.size())),
          "the whole batch was attempted despite refusals");
    std::cout << "    " << refused << " of " << attempted << " write(s) refused\n";
    // Recorded rather than asserted to be zero: 30 of 50 tracks refused
    // the rejected snapshot-based write path on a real stick, so a
    // library where this is nonzero is a property to track, not a bug.
    expected.expect("engine.writeRefusals", refused, "Engine write refusals did not increase");
    fs::remove_all(root);
    pass("stability: Engine writes degrade visibly, one track at a time");
}

// ------------------------------------------------------- work-count case

// The portable half of performance testing. These are the numbers the
// optimisation work in docs/write-path-performance.md exists to reduce:
// today every item reopens its database, and this records that so the
// improvement is visible and cannot silently regress afterwards.
void caseWorkCounts(const DataSet &set, const fs::path &scratch, const Catalogs &catalogs, int items,
                    Expectations &expected)
{
    if (catalogs.engine.empty()) {
        return;
    }
    const fs::path root = scratch / "engine-counts";
    fs::remove_all(root);
    fs::copy(*set.engineRoot, root, fs::copy_options::recursive);

    if (static_cast<int>(catalogs.engine.size()) < items) {
        items = static_cast<int>(catalogs.engine.size());
    }

    infrastructure::engine::LibdjinteropEngineCueWriter writer(root.string());
    domain::CuePoint cue;
    cue.kind = domain::CuePoint::Kind::Hot;
    cue.hotCueNumber = 1;
    cue.positionMs = 2000.0;

    WorkCounters::instance().reset();
    for (int i = 0; i < items; ++i) {
        try {
            writer.writeHotCues(catalogs.engine[static_cast<size_t>(i)].sourceId, {cue});
        } catch (const std::exception &) {
            // counted by the stability case, not here
        }
    }
    const auto counts = WorkCounters::instance().snapshot();
    std::cout << "    " << items << " Engine cue writes: " << counts.describe() << "\n";

    // One open per item is what the code does today. When the writer holds
    // its handle for the save (the fix this measurement argues for), this
    // becomes 1 -- change the expectation then, deliberately, in the same
    // commit, rather than discovering it drifted.
    expected.expect("engine.opensPerItem", counts.engineDatabaseOpens / std::max(1, items),
                    "Engine database opens per item unchanged");
    fs::remove_all(root);
    pass("work counts: the per-item database opens match what is documented");
}

}  // namespace

int main()
{
    auto sets = discoverSets();
    if (sets.empty()) {
        std::cout << "no data sets found\n";
        return 1;
    }
    std::cout << "corpus: " << sets.size() << " set(s)\n";

    for (const auto &set : sets) {
        g_set = set.name;
        std::cout << "\n== " << set.name << (set.anonymized ? " (anonymized)" : " (real)") << " ==\n";
        const fs::path scratch = scratchFor(set.name);
        Expectations expected(set.expectationsPath);
        Catalogs catalogs;

        std::cout << "  integrity\n";
        caseScanCounts(set, catalogs, expected);
        caseStatistics(catalogs);
        caseSyncMatching(set, catalogs, expected);
        casePlaceholderCollisions(set, catalogs);
        caseConsistencyChecker(catalogs);
        if (set.rekordboxRoot) {
            caseCueRoundTrip(set, scratch, catalogs);
            casePendingDeletion(set, scratch, catalogs);
            caseInterruptedBatch(set, scratch, catalogs);
        }

        std::cout << "  stability\n";
        caseEngineStability(set, scratch, catalogs, 50, expected);

        std::cout << "  work counts\n";
        caseWorkCounts(set, scratch, catalogs, 20, expected);

        expected.save();
        fs::remove_all(scratch);
    }

    std::error_code ec;
    fs::remove_all(unpackedSetsRoot(), ec);

    std::cout << "\n";
    if (g_failures > 0) {
        std::cout << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
