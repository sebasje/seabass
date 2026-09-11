// SPDX-FileCopyrightText: 2026 Sebastian Kügler <sebas@kde.org>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "domain/metadata_restore.hpp"

using seabass::domain::CuePoint;
using seabass::domain::MetadataRestorePolicy;
using seabass::domain::MetadataRestoreProposal;
using seabass::domain::planMetadataRestore;
using seabass::domain::Track;

namespace
{

CuePoint hotCue(int number, double positionMs)
{
    CuePoint cue;
    cue.kind = CuePoint::Kind::Hot;
    cue.hotCueNumber = number;
    cue.positionMs = positionMs;
    cue.color = "#FF0000";
    return cue;
}

Track stickTrack(const std::string &title, const std::string &artist = "Kalte Nacht")
{
    Track track;
    track.format = "rekordbox";
    track.sourceId = "42";
    track.title = title;
    track.artist = artist;
    track.filename = title + ".mp3";
    track.filePath = "/media/RV2/Contents/" + artist + "/" + title + ".mp3";
    track.durationSeconds = 361.5;
    return track;
}

Track storedTrack(const std::string &title, const std::string &artist = "Kalte Nacht")
{
    Track track = stickTrack(title, artist);
    track.format = "metadata-store";
    track.sourceId = "7";
    // The store keeps stick-relative paths, so the two sides never agree
    // on a path and matchTracks falls through to artist and title. That
    // is the arrangement this planner is built on.
    track.filePath = "Contents/" + artist + "/" + title + ".mp3";
    return track;
}

const MetadataRestoreProposal *find(const std::vector<MetadataRestoreProposal> &proposals, const std::string &title)
{
    for (const auto &proposal : proposals) {
        if (proposal.stickTrack.title == title) {
            return &proposal;
        }
    }
    return nullptr;
}

}  // namespace

int main()
{
    // ---- the headline case: a stick track with no cues at all -------
    {
        Track stick = stickTrack("Erste");
        Track stored = storedTrack("Erste");
        stored.cues = {hotCue(1, 32000.0), hotCue(2, 64000.0)};

        const auto proposals = planMetadataRestore({stick}, {stored}, MetadataRestorePolicy::SkipConflicts);
        assert(proposals.size() == 1);
        assert(proposals[0].cuesOffered);
        assert(proposals[0].cuesFillAGap);
        assert(!proposals[0].cuesConflict);
        assert(proposals[0].cues.size() == 2);
        assert(proposals[0].cuesAdded() == 2);
        // Matched across two different spellings of the path.
        assert(proposals[0].storedId == "7");
        std::cout << "case 1 (no cues on the stick, cues in the store) OK\n";
    }

    // ---- a track that already has everything --------------------------
    {
        Track stick = stickTrack("Erste");
        stick.cues = {hotCue(1, 32000.0)};
        Track stored = storedTrack("Erste");
        stored.cues = {hotCue(1, 32000.0)};

        const auto proposals = planMetadataRestore({stick}, {stored}, MetadataRestorePolicy::SkipConflicts);
        // Not a decision anyone needs to make, so not on the list.
        assert(proposals.empty());
        std::cout << "case 2 (nothing to offer, nothing proposed) OK\n";
    }

    // ---- cues that differ, under both policies ------------------------
    {
        Track stick = stickTrack("Erste");
        stick.cues = {hotCue(1, 32000.0)};
        Track stored = storedTrack("Erste");
        stored.cues = {hotCue(1, 48000.0), hotCue(2, 64000.0)};

        const auto skipped = planMetadataRestore({stick}, {stored}, MetadataRestorePolicy::SkipConflicts);
        // Skip is the default because the stick may have been re-cued
        // since the backup. Nothing else was on offer, so there is no
        // proposal at all.
        assert(skipped.empty());

        const auto overwritten = planMetadataRestore({stick}, {stored}, MetadataRestorePolicy::OverwriteConflicts);
        assert(overwritten.size() == 1);
        assert(overwritten[0].cuesOffered);
        assert(overwritten[0].cuesConflict);
        assert(!overwritten[0].cuesFillAGap);
        assert(overwritten[0].cues.size() == 2);
        std::cout << "case 3 (differing cues: kept by default, replaced on request) OK\n";
    }

    // ---- a conflict does not block the other fields -------------------
    {
        Track stick = stickTrack("Erste");
        stick.cues = {hotCue(1, 32000.0)};   // conflicts
        stick.comment = "";                   // blank
        Track stored = storedTrack("Erste");
        stored.cues = {hotCue(1, 48000.0)};
        stored.comment = "peak time";
        stored.rating = 4;

        const auto proposals = planMetadataRestore({stick}, {stored}, MetadataRestorePolicy::SkipConflicts);
        assert(proposals.size() == 1);
        // Cues kept, because they conflict and the policy is skip.
        assert(!proposals[0].cuesOffered);
        assert(proposals[0].cuesConflict);
        // The comment and rating land anyway: the stick has neither, and
        // filling a blank is not overwriting.
        assert(proposals[0].commentOffered);
        assert(proposals[0].comment == "peak time");
        assert(proposals[0].ratingOffered);
        assert(*proposals[0].rating == 4);
        std::cout << "case 4 (each field group decided on its own) OK\n";
    }

    // ---- a rating of 0 is a rating ------------------------------------
    {
        Track stick = stickTrack("Erste");
        stick.rating = 0;               // explicitly zero stars
        Track stored = storedTrack("Erste");
        stored.rating = 5;

        const auto skipped = planMetadataRestore({stick}, {stored}, MetadataRestorePolicy::SkipConflicts);
        // Zero stars is a decision the DJ made, not an empty field, so
        // this is a conflict and skip keeps it.
        assert(skipped.empty());

        const auto overwritten = planMetadataRestore({stick}, {stored}, MetadataRestorePolicy::OverwriteConflicts);
        assert(overwritten.size() == 1);
        assert(overwritten[0].ratingConflict);
        assert(*overwritten[0].rating == 5);
        std::cout << "case 5 (zero stars is a rating, not a blank) OK\n";
    }

    // ---- a stick track the store has never seen -----------------------
    {
        Track stick = stickTrack("Unknown", "Nobody");
        Track stored = storedTrack("Erste");
        stored.cues = {hotCue(1, 1000.0)};

        const auto proposals = planMetadataRestore({stick}, {stored}, MetadataRestorePolicy::SkipConflicts);
        assert(proposals.empty());
        std::cout << "case 6 (an unmatched stick track is left alone) OK\n";
    }

    // ---- length keeps two mixes apart ---------------------------------
    {
        Track radioEdit = stickTrack("One Track");
        radioEdit.durationSeconds = 210.0;
        Track storedExtended = storedTrack("One Track");
        storedExtended.durationSeconds = 480.0;
        storedExtended.cues = {hotCue(1, 400000.0)};

        const auto proposals = planMetadataRestore({radioEdit}, {storedExtended},
                                                    MetadataRestorePolicy::SkipConflicts);
        // A cue 6:40 into a 3:30 track would be past the end of it.
        assert(proposals.empty());
        std::cout << "case 7 (a different length is a different track) OK\n";
    }

    // ---- several tracks at once ---------------------------------------
    {
        Track first = stickTrack("Erste");
        Track second = stickTrack("Zweite");
        second.sourceId = "43";
        Track thirdWithCues = stickTrack("Dritte");
        thirdWithCues.sourceId = "44";
        thirdWithCues.cues = {hotCue(1, 1000.0)};

        Track storedFirst = storedTrack("Erste");
        storedFirst.cues = {hotCue(1, 1000.0)};
        Track storedThird = storedTrack("Dritte");
        storedThird.sourceId = "9";
        storedThird.cues = {hotCue(1, 1000.0)};

        const auto proposals = planMetadataRestore({first, second, thirdWithCues}, {storedFirst, storedThird},
                                                    MetadataRestorePolicy::SkipConflicts);
        assert(proposals.size() == 1);
        assert(find(proposals, "Erste") != nullptr);
        assert(find(proposals, "Zweite") == nullptr);  // the store has never seen it
        assert(find(proposals, "Dritte") == nullptr);  // already has the same cue
        std::cout << "case 8 (only the tracks with something to gain) OK\n";
    }

    // ---- case 9: what each format can actually take -------------------
    //
    // The planner offers a comment whenever the store has one and the
    // stick does not; which formats can store it is the writer's
    // business. This pins the fact the page depends on: a proposal
    // carries the catalog rows that decide it, so a track catalogued
    // only in DeviceLibrary is distinguishable from one Engine also
    // holds. See tests/pdb_rating_write_test.cpp for why that matters.
    {
        Track rekordboxOnly = stickTrack("Erste");
        rekordboxOnly.catalogRows = {{"rekordbox", "42"}};
        Track alsoEngine = stickTrack("Zweite");
        alsoEngine.sourceId = "43";
        alsoEngine.catalogRows = {{"rekordbox", "43"}, {"engine", "900"}};

        Track storedFirst = storedTrack("Erste");
        storedFirst.comment = "peak time";
        storedFirst.rating = 4;
        Track storedSecond = storedTrack("Zweite");
        storedSecond.sourceId = "8";
        storedSecond.comment = "closer";

        const auto proposals = planMetadataRestore({rekordboxOnly, alsoEngine}, {storedFirst, storedSecond},
                                                    MetadataRestorePolicy::SkipConflicts);
        assert(proposals.size() == 2);

        const auto *first = find(proposals, "Erste");
        assert(first && first->commentOffered);
        assert(first->stickTrack.catalogRows.size() == 1);
        assert(first->stickTrack.catalogRows[0].format == "rekordbox");
        // A rating goes back even on this one: export.pdb stores it in a
        // byte that is already there.
        assert(first->ratingOffered && *first->rating == 4);

        const auto *second = find(proposals, "Zweite");
        assert(second && second->commentOffered);
        bool hasEngine = false;
        for (const auto &row : second->stickTrack.catalogRows) {
            if (row.format == "engine") {
                hasEngine = true;
            }
        }
        assert(hasEngine);
        std::cout << "case 9 (a proposal carries the catalogs that decide what can be written) OK\n";
    }

    std::cout << "all metadata_restore_test cases passed\n";
    return 0;
}
