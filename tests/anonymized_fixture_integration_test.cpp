#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "application/use_cases/scan_library.hpp"
#include "application/use_cases/sync_libraries.hpp"
#include "domain/library_consistency.hpp"
#include "domain/library_statistics.hpp"
#include "infrastructure/backup/filesystem_backup_store.hpp"
#include "infrastructure/cleanup/pending_deletion_applier.hpp"
#include "infrastructure/cleanup/pending_deletion_manifest.hpp"
#include "infrastructure/cleanup/pending_deletion_resolver.hpp"
#include "infrastructure/engine/libdjinterop_engine_reader.hpp"
#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"
#include "infrastructure/rekordbox/rekordbox_cleanup_writer.hpp"
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

    // Case 7: orphaned-file deletion, the full chain, at realistic scale
    // -- a real duplicate-cleanup writer pass (RekordboxCleanupWriter)
    // against ~1,370 other real-shaped tracks, producing a real
    // pending-deletion manifest entry the same way cleanup_controller.cpp's
    // runApplyTask() does, followed by the stale-manifest safety check
    // (resolvePendingDeletions() correctly protecting a path a fresh scan
    // says is still referenced) and the real, permanent-deletion path
    // (applyPendingDeletions()) -- the one place in the app that destroys
    // real audio content, per BRAINSTORM.md's own flag on this as the
    // highest-risk operation. Complements the small, synthetic-fixture
    // unit coverage in pending_deletion_resolver_test.cpp and
    // pending_deletion_applier_test.cpp, which don't exercise the real
    // cleanup writer or this scale/variety.
    {
        fs::path scratchRoot2 = fs::temp_directory_path() / "seabass_anonymized_fixture_pending_deletion_test";
        fs::remove_all(scratchRoot2);
        fs::create_directories(scratchRoot2);
        fs::path scratchRekordbox = scratchRoot2 / "rekordbox";
        fs::copy(rekordboxRoot, scratchRekordbox, fs::copy_options::recursive);

        infrastructure::rekordbox::KaitaiRekordboxReader scratchReader(scratchRekordbox.string());
        auto scratchTracks = application::ScanLibrary(scratchReader).execute();
        assert(scratchTracks.size() == 1370);

        // Two arbitrary distinct real tracks -- doomed gets removed and
        // "replaced" by survivor, exactly like a real duplicate-cleanup
        // pass would (see cleanup_controller.cpp's runApplyTask()).
        domain::Track doomed = scratchTracks[10];
        domain::Track survivor = scratchTracks[20];
        assert(doomed.sourceId != survivor.sourceId);

        // The fixture's tracks have no real audio file behind them (see
        // case 5's own comment) -- create one now so there's something
        // real for the deletion path to actually delete, same as
        // pending_deletion_applier_test.cpp's own touch() helper.
        fs::create_directories(fs::path(doomed.filePath).parent_path());
        std::ofstream(doomed.filePath) << "fake audio data";
        assert(fs::exists(doomed.filePath));

        fs::path manifestPath = scratchRoot2 / ".seabass-pending-deletions.jsonl";
        infrastructure::cleanup::PendingDeletionManifest manifest(manifestPath.string());

        infrastructure::rekordbox::RekordboxCleanupWriter cleanupWriter(scratchRekordbox.string());
        cleanupWriter.removeTrackReplacingWith(doomed.sourceId, survivor.sourceId);

        infrastructure::cleanup::PendingDeletion pending;
        pending.format = "rekordbox";
        pending.filePath = doomed.filePath;
        pending.title = doomed.title;
        pending.artist = doomed.artist;
        pending.backupId = "test-backup";
        manifest.append(pending);

        // Fresh re-scan after the real write: the doomed track is
        // genuinely gone from the 1,370-track library.
        infrastructure::rekordbox::KaitaiRekordboxReader postRemovalReader(scratchRekordbox.string());
        auto postRemovalTracks = application::ScanLibrary(postRemovalReader).execute();
        assert(postRemovalTracks.size() == 1369);
        for (const auto &t : postRemovalTracks) {
            assert(t.sourceId != doomed.sourceId);
        }
        std::cout << "case 7a (real RekordboxCleanupWriter pass at realistic scale: doomed track genuinely "
                     "removed, pending-deletion entry recorded) OK\n";

        // Stale-manifest safety check: simulate a fresh scan where some
        // other real track now legitimately occupies the doomed track's
        // old file path (e.g. a later re-tag/rename pointed a track at
        // that exact path) -- resolvePendingDeletions() must reclassify
        // the manifest entry as stillReferenced and refuse it, even
        // though the manifest itself still lists it as orphaned.
        auto staleScanTracks = postRemovalTracks;
        staleScanTracks[0].filePath = doomed.filePath;

        auto staleResolution = infrastructure::cleanup::resolvePendingDeletions(manifest.list(), staleScanTracks);
        assert(staleResolution.safeToDelete.empty());
        assert(staleResolution.stillReferenced.size() == 1);
        assert(staleResolution.stillReferenced[0].filePath == doomed.filePath);
        assert(fs::exists(doomed.filePath));  // correctly left untouched
        std::cout << "case 7b (stale manifest: resolvePendingDeletions correctly protects a path a fresh "
                     "scan says is still referenced) OK\n";

        // The real, non-stale case: resolve against the genuinely fresh
        // scan and actually delete -- the file must be genuinely gone
        // from disk afterward, and cleared from the manifest.
        auto realResolution = infrastructure::cleanup::resolvePendingDeletions(manifest.list(), postRemovalTracks);
        assert(realResolution.safeToDelete.size() == 1);
        assert(realResolution.stillReferenced.empty());

        auto outcomes = infrastructure::cleanup::applyPendingDeletions(realResolution.safeToDelete, manifest);
        assert(outcomes.size() == 1);
        assert(outcomes[0].status == infrastructure::cleanup::PendingDeletionOutcome::Status::Deleted);
        assert(!fs::exists(doomed.filePath));
        assert(manifest.list().empty());
        std::cout << "case 7c (genuinely orphaned file: actually deleted from disk at realistic scale, "
                     "manifest cleared) OK\n";

        fs::remove_all(scratchRoot2);
    }

    // Case 8: interrupted-batch rollback for rekordbox, at realistic
    // scale. duplicate_cleanup_interruption_test.cpp already proves this
    // property for Engine against a tiny 4-track synthetic database;
    // rekordbox goes through a genuinely different writer
    // (RekordboxCleanupWriter/PdbRowWriter) and backup shape (a single
    // shared export.pdb among ~1,370 real rows and real playlists,
    // rather than Engine's SQLite file), so it's worth its own proof --
    // and this is the first place this exact property gets exercised
    // against real playlist membership at scale rather than one
    // hand-built playlist. See BRAINSTORM.md's own "Clean Up... allow
    // cancelling (incl rollback)" note and cleanup_controller.cpp's
    // runApplyTask() for the real backup-then-write sequence this
    // mirrors.
    {
        fs::path scratchRoot3 = fs::temp_directory_path() / "seabass_anonymized_fixture_cleanup_interruption_test";
        fs::remove_all(scratchRoot3);
        fs::create_directories(scratchRoot3);
        fs::path scratchRekordbox = scratchRoot3 / "rekordbox";
        fs::copy(rekordboxRoot, scratchRekordbox, fs::copy_options::recursive);

        infrastructure::rekordbox::KaitaiRekordboxReader scratchReader(scratchRekordbox.string());
        auto scratchTracks = application::ScanLibrary(scratchReader).execute();
        assert(scratchTracks.size() == 1370);

        // Group A's doomed track is a real track with real playlist
        // membership, so restoring genuinely has playlist repoint
        // behavior to undo, not just a bare track row. Group B is a
        // different, unrelated pair -- simulates the batch being
        // interrupted after group A completes but before group B starts.
        const domain::Track *doomedA = nullptr;
        for (const auto &t : scratchTracks) {
            if (!t.playlists.empty()) {
                doomedA = &t;
                break;
            }
        }
        assert(doomedA != nullptr);
        std::string doomedAId = doomedA->sourceId;
        std::string doomedATitle = doomedA->title;
        auto doomedAPlaylistsBefore = doomedA->playlists;

        const domain::Track *survivorA = nullptr;
        for (const auto &t : scratchTracks) {
            if (t.sourceId != doomedAId) {
                survivorA = &t;
                break;
            }
        }
        assert(survivorA != nullptr);
        std::string survivorAId = survivorA->sourceId;

        const domain::Track &doomedB = scratchTracks[scratchTracks.size() - 1];
        const domain::Track &survivorB = scratchTracks[scratchTracks.size() - 2];
        assert(doomedB.sourceId != doomedAId && doomedB.sourceId != survivorAId);
        assert(survivorB.sourceId != doomedAId && survivorB.sourceId != survivorAId);

        // Mirrors runApplyTask()'s backupIfNeeded(): export.pdb is the
        // one shared file every group's write touches, backed up exactly
        // once before any group is processed.
        fs::path backupDir = scratchRoot3 / ".seabass-backups";
        infrastructure::backup::FilesystemBackupStore backupStore(backupDir.string());
        std::string pdbPath = (scratchRekordbox / "rekordbox" / "export.pdb").string();
        assert(fs::exists(pdbPath));
        auto backupRecord = backupStore.backup({pdbPath}, "duplicate-file-cleanup");

        // Process group A only -- group B is never touched, simulating
        // the app being killed/closed right after group A completes.
        {
            infrastructure::rekordbox::RekordboxCleanupWriter cleanupWriter(scratchRekordbox.string());
            cleanupWriter.removeTrackReplacingWith(doomedAId, survivorAId);
        }

        // Confirm the "interrupted" state: group A processed, group B
        // completely untouched.
        {
            infrastructure::rekordbox::KaitaiRekordboxReader midReader(scratchRekordbox.string());
            auto midTracks = application::ScanLibrary(midReader).execute();
            assert(midTracks.size() == 1369);
            bool doomedAStillPresent = false;
            bool doomedBPresent = false;
            bool survivorBPresent = false;
            for (const auto &t : midTracks) {
                if (t.sourceId == doomedAId) {
                    doomedAStillPresent = true;
                }
                if (t.sourceId == doomedB.sourceId) {
                    doomedBPresent = true;
                }
                if (t.sourceId == survivorB.sourceId) {
                    survivorBPresent = true;
                }
            }
            assert(!doomedAStillPresent);
            assert(doomedBPresent);
            assert(survivorBPresent);
            std::cout << "case 8a (interrupted after group A: group A's doomed track genuinely removed at "
                         "realistic scale, group B completely untouched) OK\n";
        }

        // The actual rollback: restore the one upfront export.pdb
        // backup.
        bool restored = backupStore.restore(backupRecord.id);
        assert(restored);

        // Everything is back exactly as it was before the batch started
        // -- group A's removal is undone too, including its exact real
        // playlist membership, not just a bare row.
        {
            infrastructure::rekordbox::KaitaiRekordboxReader postReader(scratchRekordbox.string());
            auto postTracks = application::ScanLibrary(postReader).execute();
            assert(postTracks.size() == 1370);
            const domain::Track *restoredDoomedA = nullptr;
            for (const auto &t : postTracks) {
                if (t.sourceId == doomedAId) {
                    restoredDoomedA = &t;
                }
            }
            assert(restoredDoomedA != nullptr);
            assert(restoredDoomedA->title == doomedATitle);
            assert(restoredDoomedA->playlists.size() == doomedAPlaylistsBefore.size());
            for (size_t i = 0; i < doomedAPlaylistsBefore.size(); ++i) {
                assert(restoredDoomedA->playlists[i].name == doomedAPlaylistsBefore[i].name);
                assert(restoredDoomedA->playlists[i].position == doomedAPlaylistsBefore[i].position);
            }
            std::cout << "case 8b (restoring the one upfront export.pdb backup fully reverts group A's "
                         "removal and its real playlist membership, at realistic scale) OK\n";
        }

        fs::remove_all(scratchRoot3);
    }

    std::cout << "all cases passed\n";
    return 0;
}
