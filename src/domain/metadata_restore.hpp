#pragma once

#include <optional>
#include <string>
#include <vector>

#include "domain/track.hpp"

namespace seabass::domain
{

// What to do where the stick and the local metadata store disagree about
// something the DJ authored.
//
// Default skip, and the asymmetry with the backup direction is
// deliberate: a stick may have been re-cued since the backup was taken,
// and a restore that silently replaced last night's work with last
// month's would be the worst thing this feature could do.
//
// A field the stick does not have is never a conflict. Filling a blank
// is the whole point, and it happens under both policies.
enum class MetadataRestorePolicy {
    SkipConflicts,
    OverwriteConflicts,
};

// One stick track and what the store has to offer it.
//
// Each field group is decided separately, so a track whose cues conflict
// can still be given the comment it never had.
struct MetadataRestoreProposal
{
    Track stickTrack;
    std::string storedId;   // the store row this came from
    std::string storedFrom; // the stick label the store last saw it on, for display

    // The complete cue list to write, never a diff: every cue writer in
    // Seabass replaces a track's whole set, so anything less silently
    // drops what is already there.
    std::vector<CuePoint> cues;
    bool cuesOffered = false;
    // The stick already has cues and they are not the stored ones. Only
    // offered under OverwriteConflicts.
    bool cuesConflict = false;
    // The headline case: the stick has no cues at all for this track and
    // the store has some. Unambiguous, and what a re-export leaves
    // behind.
    bool cuesFillAGap = false;

    std::optional<int> rating;
    bool ratingOffered = false;
    bool ratingConflict = false;

    std::string comment;
    bool commentOffered = false;
    bool commentConflict = false;

    bool offersAnything() const { return cuesOffered || ratingOffered || commentOffered; }
    // How many cues the write would add, for a sentence a person reads.
    int cuesAdded() const;
};

// Pairs each stick track with its stored copy and works out what a
// restore would put back.
//
// Matching is domain::matchTracks() unchanged: artist and title, falling
// back to filename, guarded by length. The stored side carries
// stick-relative paths and the stick side absolute ones, so the path
// branch never fires and never needs to -- two spellings of one path
// were never going to agree across a rebuilt stick anyway.
//
// Returns only proposals that offer something. A track already carrying
// everything the store has is not a decision anyone needs to make.
std::vector<MetadataRestoreProposal> planMetadataRestore(const std::vector<Track> &stickTracks,
                                                          const std::vector<Track> &storedTracks,
                                                          MetadataRestorePolicy policy);

}  // namespace seabass::domain
