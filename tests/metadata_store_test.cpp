// SPDX-FileCopyrightText: 2026 Sebastian Kügler <sebas@kde.org>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <sqlite3.h>

#include "application/ports/progress_reporter.hpp"
#include "domain/track.hpp"
#include "infrastructure/local/metadata_store.hpp"

#include "scratch_path.hpp"

using seabass::application::CancellationToken;
using seabass::application::NullProgressReporter;
using seabass::domain::CuePoint;
using seabass::domain::PlaylistMembership;
using seabass::domain::Track;
using seabass::infrastructure::local::ConflictPolicy;
using seabass::infrastructure::local::MetadataSource;
using seabass::infrastructure::local::MetadataStore;
namespace fs = std::filesystem;

namespace
{

fs::path scratchRoot()
{
    // seabass::testing::scratchRoot() (scratch_path.hpp) already
    // namespaces by pid -- ::getpid() doesn't exist in that unqualified
    // form on Windows, and appending it again here was redundant even on
    // platforms where it does.
    return seabass::testing::scratchRoot() / "seabass-metadata-store-test";
}

void writeFile(const fs::path &path, const std::string &data)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << data;
}

CuePoint memoryCue(double positionMs)
{
    CuePoint cue;
    cue.kind = CuePoint::Kind::Memory;
    cue.positionMs = positionMs;
    return cue;
}

CuePoint hotCue(int number, double positionMs, const std::string &color = "#FF0000")
{
    CuePoint cue;
    cue.kind = CuePoint::Kind::Hot;
    cue.hotCueNumber = number;
    cue.positionMs = positionMs;
    cue.color = color;
    return cue;
}

Track sampleTrack(const fs::path &stickRoot, const std::string &relative, const std::string &title)
{
    Track track;
    track.format = "rekordbox";
    track.sourceId = title;
    track.filePath = (stickRoot / relative).string();
    track.filename = fs::path(relative).filename().string();
    track.title = title;
    track.artist = "Kalte Nacht";
    track.durationSeconds = 361.5;
    track.bpm = 128.0;
    track.key = "Am";
    return track;
}

MetadataSource sourceFor(const fs::path &stickRoot, const std::string &label = "RV2")
{
    MetadataSource source;
    source.stickRoot = stickRoot;
    source.libraryId = "library-abc";
    source.stickLabel = label;
    return source;
}

seabass::infrastructure::local::MetadataBackupSummary store(MetadataStore &store,
                                                             const std::vector<Track> &tracks,
                                                             const MetadataSource &source,
                                                             ConflictPolicy policy)
{
    return store.store(tracks, source, policy, NullProgressReporter::instance(), CancellationToken::none());
}

}  // namespace

int main()
{
    const fs::path root = scratchRoot();
    fs::remove_all(root);
    const fs::path stick = root / "stick";
    const fs::path db = root / "local" / "metadata.db";

    // Two tracks that share one cover image, so the content-addressed
    // artwork store has something to collapse.
    const fs::path cover = stick / "PIONEER" / "Artwork" / "cover.jpg";
    writeFile(cover, "JPEGDATA-one-cover-for-two-tracks");
    writeFile(stick / "Contents" / "Kalte Nacht" / "Erste.mp3", "audio");
    writeFile(stick / "Contents" / "Kalte Nacht" / "Zweite.mp3", "audio");

    // ---- case 1: a first backup ------------------------------------
    {
        MetadataStore metadata(db);

        Track first = sampleTrack(stick, "Contents/Kalte Nacht/Erste.mp3", "Erste");
        first.cues = {memoryCue(0.0), hotCue(1, 32000.0)};
        first.rating = 4;
        first.comment = "opener";
        first.artworkPath = cover.string();
        first.playlists = {PlaylistMembership{"Techno/Peak Time", 3}};

        Track second = sampleTrack(stick, "Contents/Kalte Nacht/Zweite.mp3", "Zweite");
        second.artworkPath = cover.string();

        const auto summary = store(metadata, {first, second}, sourceFor(stick), ConflictPolicy::Overwrite);
        assert(summary.tracksSeen == 2);
        assert(summary.tracksAdded == 2);
        assert(summary.tracksUpdated == 0);
        assert(summary.cuesStored == 2);
        // One image on disk for two tracks: the second track's cover
        // hashes to a file that is already there.
        assert(summary.artworkFilesAdded == 1);
        assert(metadata.trackCount() == 2);

        const auto rows = metadata.browse("", 50, 0);
        assert(rows.size() == 2);
        // Ordered by artist then title, so "Erste" leads "Zweite".
        assert(rows[0].title == "Erste");
        assert(rows[0].cueCount == 2);
        assert(rows[0].playlistCount == 1);
        assert(rows[0].rating.has_value() && *rows[0].rating == 4);
        assert(rows[0].stickLabel == "RV2");
        assert(!rows[0].artworkPath.empty() && fs::exists(rows[0].artworkPath));
        assert(rows[1].title == "Zweite");
        assert(rows[1].cueCount == 0);
        assert(!rows[1].rating.has_value());
        // Both tracks point at the same image file.
        assert(rows[0].artworkPath == rows[1].artworkPath);
        std::cout << "case 1 (first backup, cover art shared) OK\n";
    }

    // ---- case 2: the same stick again, nothing new ------------------
    {
        MetadataStore metadata(db);
        Track first = sampleTrack(stick, "Contents/Kalte Nacht/Erste.mp3", "Erste");
        first.cues = {memoryCue(0.0), hotCue(1, 32000.0)};
        first.rating = 4;
        first.comment = "opener";
        first.artworkPath = cover.string();

        const auto summary = store(metadata, {first}, sourceFor(stick), ConflictPolicy::Overwrite);
        assert(summary.tracksAdded == 0);
        assert(summary.tracksUpdated == 1);   // identical values still count as written
        assert(summary.tracksSkipped == 0);
        // The image was already stored, so nothing is hashed or copied
        // again -- a second run over an unchanged stick must not cost
        // what the first did.
        assert(summary.artworkFilesAdded == 0);
        assert(metadata.trackCount() == 2);
        std::cout << "case 2 (re-running over an unchanged stick) OK\n";
    }

    // ---- case 3: conflicts, both policies ---------------------------
    {
        MetadataStore metadata(db);
        Track changed = sampleTrack(stick, "Contents/Kalte Nacht/Erste.mp3", "Erste");
        changed.cues = {memoryCue(0.0), hotCue(1, 48000.0)};  // hot cue moved
        changed.rating = 2;
        changed.comment = "";  // nothing incoming: never a conflict

        const auto skipped = store(metadata, {changed}, sourceFor(stick), ConflictPolicy::Skip);
        assert(skipped.tracksSkipped == 1);
        assert(skipped.tracksUpdated == 0);
        auto rows = metadata.browse("Erste", 10, 0);
        assert(rows.size() == 1);
        assert(*rows[0].rating == 4);
        assert(rows[0].comment == "opener");  // untouched, not blanked
        auto cues = metadata.cuesFor(rows[0].id);
        assert(cues.size() == 2);
        assert(cues[1].positionMs == 32000.0);

        const auto overwritten = store(metadata, {changed}, sourceFor(stick), ConflictPolicy::Overwrite);
        assert(overwritten.tracksUpdated == 1);
        assert(overwritten.tracksSkipped == 0);
        rows = metadata.browse("Erste", 10, 0);
        assert(*rows[0].rating == 2);
        assert(rows[0].comment == "opener");  // an empty incoming value erases nothing
        cues = metadata.cuesFor(rows[0].id);
        assert(cues.size() == 2);
        assert(cues[1].positionMs == 48000.0);
        std::cout << "case 3 (skip keeps, overwrite replaces, blanks erase nothing) OK\n";
    }

    // ---- case 4: filling a blank is not a conflict ------------------
    {
        MetadataStore metadata(db);
        Track second = sampleTrack(stick, "Contents/Kalte Nacht/Zweite.mp3", "Zweite");
        second.cues = {memoryCue(1000.0)};
        second.rating = 5;

        // Skip policy, but the stored row has neither cues nor a rating,
        // so there is nothing to keep and both land.
        const auto summary = store(metadata, {second}, sourceFor(stick), ConflictPolicy::Skip);
        assert(summary.tracksUpdated == 1);
        assert(summary.tracksSkipped == 0);
        const auto rows = metadata.browse("Zweite", 10, 0);
        assert(rows[0].cueCount == 1);
        assert(*rows[0].rating == 5);
        std::cout << "case 4 (a blank is filled even under skip) OK\n";
    }

    // ---- case 5: the same track, a different stick ------------------
    //
    // The whole point of the feature: the same recording on a rebuilt
    // stick, under a different path, mounted somewhere else. It is
    // matched on artist, title and length, so it lands on the row it
    // already had rather than adding a second one. A path-keyed store
    // would have made two.
    {
        const fs::path otherStick = root / "another-mount" / "REBUILT";
        writeFile(otherStick / "Music" / "2026" / "erste-remaster.mp3", "audio");
        MetadataStore metadata(db);
        Track moved = sampleTrack(otherStick, "Music/2026/erste-remaster.mp3", "Erste");
        moved.rating = 3;

        const auto summary = store(metadata, {moved}, sourceFor(otherStick, "REBUILT"), ConflictPolicy::Overwrite);
        assert(summary.tracksAdded == 0);
        assert(summary.tracksUpdated == 1);
        assert(metadata.trackCount() == 2);  // still two rows, not three
        const auto rows = metadata.browse("Erste", 10, 0);
        assert(rows[0].stickLabel == "REBUILT");
        assert(rows[0].relativePath == "Music/2026/erste-remaster.mp3");
        // The cues from the first stick are still there: this stick had
        // none to overwrite them with.
        assert(rows[0].cueCount == 2);
        std::cout << "case 5 (the same recording under another path is one row) OK\n";
    }

    // ---- case 5b: same artist and title, different length ------------
    //
    // A radio edit and an extended mix. Length is the guard that keeps
    // them apart; without it one would silently take the other's cues.
    {
        const fs::path mixDb = root / "mixes" / "metadata.db";
        MetadataStore metadata(mixDb);
        Track radioEdit = sampleTrack(stick, "Contents/A/edit.mp3", "One Track");
        radioEdit.durationSeconds = 210.0;
        radioEdit.cues = {hotCue(1, 1000.0)};
        Track extended = sampleTrack(stick, "Contents/A/extended.mp3", "One Track");
        extended.durationSeconds = 480.0;
        extended.cues = {hotCue(1, 9000.0)};

        store(metadata, {radioEdit, extended}, sourceFor(stick), ConflictPolicy::Overwrite);
        assert(metadata.trackCount() == 2);

        // And a re-read of the shorter one, two seconds out, still finds
        // its own row rather than making a third.
        Track reread = radioEdit;
        reread.durationSeconds = 211.5;
        const auto again = store(metadata, {reread}, sourceFor(stick), ConflictPolicy::Overwrite);
        assert(again.tracksAdded == 0);
        assert(metadata.trackCount() == 2);
        std::cout << "case 5b (same title, different length, two tracks) OK\n";
    }

    // ---- case 6: rows with no file on this stick --------------------
    {
        MetadataStore metadata(db);
        Track streaming = sampleTrack(stick, "Contents/Kalte Nacht/Erste.mp3", "Erste");
        streaming.streamingSource = "TIDAL";
        Track unresolved;
        unresolved.format = "engine";
        unresolved.title = "Nowhere";

        const auto summary = store(metadata, {streaming, unresolved}, sourceFor(stick), ConflictPolicy::Overwrite);
        assert(summary.tracksSeen == 2);
        assert(summary.tracksWithoutIdentity == 2);
        assert(summary.tracksAdded == 0);
        assert(metadata.trackCount() == 2);
        std::cout << "case 6 (streaming links and rows with nothing to match on) OK\n";
    }

    // ---- case 7: unrated is not zero -------------------------------
    {
        const fs::path ratingDb = root / "ratings" / "metadata.db";
        MetadataStore metadata(ratingDb);
        Track zero = sampleTrack(stick, "Contents/A/zero.mp3", "Zero");
        zero.rating = 0;
        Track none = sampleTrack(stick, "Contents/A/none.mp3", "None");

        store(metadata, {zero, none}, sourceFor(stick), ConflictPolicy::Overwrite);
        const auto rows = metadata.browse("", 10, 0);
        assert(rows.size() == 2);
        // Ordered by title within the one artist.
        assert(rows[0].title == "None" && !rows[0].rating.has_value());
        assert(rows[1].title == "Zero" && rows[1].rating.has_value() && *rows[1].rating == 0);
        std::cout << "case 7 (explicitly 0 stars survives as 0, unrated as absent) OK\n";
    }

    // ---- case 8: browse search and paging ---------------------------
    {
        MetadataStore metadata(db);
        assert(metadata.trackCount("erste") == 1);   // case-insensitive
        assert(metadata.trackCount("kalte") == 2);   // the artist matches too, not only the title
        assert(metadata.trackCount("zzz") == 0);
        assert(metadata.browse("", 1, 0).size() == 1);
        assert(metadata.browse("", 1, 1).size() == 1);
        assert(metadata.browse("", 1, 0)[0].id != metadata.browse("", 1, 1)[0].id);
        std::cout << "case 8 (search and paging) OK\n";
    }

    // ---- case 9: readAll carries cues, for matching -----------------
    {
        MetadataStore metadata(db);
        const auto tracks = metadata.readAll();
        assert(tracks.size() == 2);
        bool foundCues = false;
        for (const auto &track : tracks) {
            assert(track.format == "metadata-store");
            // Stick-relative, deliberately: an absolute path here would
            // be a claim about a file on a stick that may not be here.
            assert(!track.filePath.empty());
            assert(track.filePath.find(':') == std::string::npos);
            assert(track.filePath.rfind('/', 0) != 0);
            if (track.title == "Erste") {
                assert(track.cues.size() == 2);
                foundCues = true;
            }
        }
        assert(foundCues);
        std::cout << "case 9 (readAll is a LibraryReader over the store) OK\n";
    }

    // ---- case 10: cancellation keeps what it wrote ------------------
    {
        const fs::path cancelDb = root / "cancel" / "metadata.db";
        MetadataStore metadata(cancelDb);
        CancellationToken token;
        token.cancel();
        Track track = sampleTrack(stick, "Contents/A/one.mp3", "One");
        const auto summary = metadata.store({track}, sourceFor(stick), ConflictPolicy::Overwrite,
                                             NullProgressReporter::instance(), token);
        assert(summary.cancelled);
        assert(summary.tracksSeen == 0);
        assert(metadata.trackCount() == 0);
        std::cout << "case 10 (a cancelled run commits what it had) OK\n";
    }

    // ---- case 11: a database from another schema ---------------------
    //
    // Pre-1.0, the schema changes and there is no migration. What must
    // never happen is losing the file: this store may hold the only copy
    // of cues a reformatted stick no longer has.
    {
        const fs::path oldDb = root / "old-schema" / "metadata.db";
        fs::create_directories(oldDb.parent_path());
        {
            // A plausible earlier schema: a tracks table keyed the way
            // this store used to key it, and a version that is not ours.
            sqlite3 *raw = nullptr;
            assert(sqlite3_open(oldDb.string().c_str(), &raw) == SQLITE_OK);
            sqlite3_exec(raw, "CREATE TABLE tracks (id INTEGER PRIMARY KEY, path_key TEXT)", nullptr, nullptr,
                         nullptr);
            sqlite3_exec(raw, "INSERT INTO tracks (path_key) VALUES ('contents/a.mp3')", nullptr, nullptr, nullptr);
            sqlite3_exec(raw, "CREATE TABLE schema_version (version INTEGER NOT NULL)", nullptr, nullptr, nullptr);
            sqlite3_exec(raw, "INSERT INTO schema_version (version) VALUES (99)", nullptr, nullptr, nullptr);
            sqlite3_close(raw);
        }

        MetadataStore metadata(oldDb);
        // A working store, not a thrown error and not a broken one.
        Track track = sampleTrack(stick, "Contents/A/new.mp3", "New");
        track.cues = {memoryCue(0.0)};
        const auto summary = store(metadata, {track}, sourceFor(stick), ConflictPolicy::Overwrite);
        assert(summary.tracksAdded == 1);
        assert(metadata.trackCount() == 1);

        // And the old file is still on disk, beside it.
        bool foundSuperseded = false;
        for (const auto &entry : fs::directory_iterator(oldDb.parent_path())) {
            if (entry.path().filename().string().find("metadata.db.superseded-99-") == 0) {
                foundSuperseded = true;
            }
        }
        assert(foundSuperseded);
        std::cout << "case 11 (a database from another schema is moved aside, never deleted) OK\n";
    }

    fs::remove_all(root);
    std::cout << "all metadata_store_test cases passed\n";
    return 0;
}
