#include "domain/duplicate_cleanup.hpp"

#include <algorithm>
#include <cmath>

#include "domain/local_restore.hpp"

namespace seabass::domain
{

namespace
{

// Matches sync_planning's/track_matching's own duration-tolerance
// convention: two lengths this close are "the same track", not a
// meaningfully different edit.
constexpr double DurationToleranceSeconds = 2.0;

bool durationsAgree(double a, double b)
{
    return std::abs(a - b) <= DurationToleranceSeconds;
}

// Index of the track scoring highest by `score`, ties broken by longer
// duration, then larger file size, then original order -- deterministic
// regardless of scan order.
template<typename Score>
size_t bestIndex(const std::vector<Track> &tracks, Score score)
{
    size_t best = 0;
    for (size_t i = 1; i < tracks.size(); ++i) {
        auto scoreI = score(tracks[i]);
        auto scoreBest = score(tracks[best]);
        if (scoreI != scoreBest) {
            if (scoreI > scoreBest) {
                best = i;
            }
            continue;
        }
        if (!durationsAgree(tracks[i].durationSeconds, tracks[best].durationSeconds)) {
            if (tracks[i].durationSeconds > tracks[best].durationSeconds) {
                best = i;
            }
            continue;
        }
        if (tracks[i].fileSizeBytes > tracks[best].fileSizeBytes) {
            best = i;
        }
    }
    return best;
}

// True if some track about to be removed carries a value `get` doesn't
// find on the survivor -- checked against the survivor specifically,
// NOT "are there 2+ distinct values anywhere in the group": a group
// where only ONE doomed copy (not the survivor) has a value, and every
// other copy including the survivor has none, has exactly one distinct
// value in the whole group, but removing that doomed copy still
// silently discards the only copy of it. That's a real loss and must be
// flagged, even though nothing in the group technically "disagrees".
template<typename Get>
bool losesDataFromRemoval(const Track &survivor, const std::vector<Track> &toRemove, Get get)
{
    auto survivorValue = get(survivor);
    for (const auto &doomed : toRemove) {
        auto doomedValue = get(doomed);
        if (doomedValue.has_value() && doomedValue != survivorValue) {
            return true;
        }
    }
    return false;
}

}  // namespace

DuplicateCleanupPlan DuplicateCleanupPlanner::plan(const DuplicateGroup &group)
{
    DuplicateCleanupPlan result;
    result.group = group;

    if (group.tracks.empty()) {
        return result;
    }
    if (group.tracks.size() == 1) {
        result.survivor = group.tracks[0];
        result.mergedCuesForSurvivor = group.tracks[0].cues;
        return result;
    }

    bool anyBitrateKnown = std::any_of(group.tracks.begin(), group.tracks.end(),
                                        [](const Track &t) { return t.bitrate > 0; });

    size_t byDuration = bestIndex(group.tracks, [](const Track &t) { return t.durationSeconds; });
    size_t survivorIndex =
        anyBitrateKnown ? bestIndex(group.tracks, [](const Track &t) { return t.bitrate; }) : byDuration;

    // "Differs" means picking by quality and picking by length actually
    // disagree, not merely that bitrates/sizes vary slightly (real
    // duplicate encodes of the same rip commonly do) -- only a
    // meaningfully different *duration* is a signal this might be a
    // deliberately different edit rather than just a different
    // encoding of the same audio.
    result.differs =
        anyBitrateKnown && survivorIndex != byDuration &&
        !durationsAgree(group.tracks[survivorIndex].durationSeconds, group.tracks[byDuration].durationSeconds);

    result.survivor = group.tracks[survivorIndex];
    for (size_t i = 0; i < group.tracks.size(); ++i) {
        if (i != survivorIndex) {
            result.toRemove.push_back(group.tracks[i]);
        }
    }

    std::vector<CuePoint> merged = result.survivor.cues;
    for (size_t i = 0; i < group.tracks.size(); ++i) {
        if (i == survivorIndex) {
            continue;
        }
        merged = LocalRestorePlanner::mergeCues(merged, group.tracks[i].cues);
    }
    result.mergedCuesForSurvivor = std::move(merged);

    // Fill-a-gap propagation: only when the survivor itself lacks the
    // field, and only the first donor found (deterministic group order)
    // -- there's exactly one bpm/key/artwork to end up with, not several
    // to reconcile, unlike cues above.
    if (result.survivor.bpm <= 0.0) {
        for (size_t i = 0; i < group.tracks.size(); ++i) {
            if (i != survivorIndex && group.tracks[i].bpm > 0.0) {
                result.bpmForSurvivor = group.tracks[i].bpm;
                result.bpmDonorSourceId = group.tracks[i].sourceId;
                break;
            }
        }
    }
    if (result.survivor.key.empty()) {
        for (size_t i = 0; i < group.tracks.size(); ++i) {
            if (i != survivorIndex && !group.tracks[i].key.empty()) {
                result.keyForSurvivor = group.tracks[i].key;
                result.keyDonorSourceId = group.tracks[i].sourceId;
                break;
            }
        }
    }
    if (result.survivor.artworkPath.empty()) {
        for (size_t i = 0; i < group.tracks.size(); ++i) {
            if (i != survivorIndex && !group.tracks[i].artworkPath.empty()) {
                result.artworkPathForSurvivor = group.tracks[i].artworkPath;
                result.artworkDonorSourceId = group.tracks[i].sourceId;
                break;
            }
        }
    }

    // Real, currently-unpreservable per-copy data: rating/comment/
    // playCount/lastPlayedAt are never propagated by this planner or any
    // writer, unlike bpm/key/artwork above -- so a genuine disagreement
    // here means removing the other copies really would discard one of
    // these values with no way to keep it.
    bool ratingLoses = losesDataFromRemoval(result.survivor, result.toRemove,
                                             [](const Track &t) -> std::optional<int> { return t.rating; });
    bool commentLoses =
        losesDataFromRemoval(result.survivor, result.toRemove, [](const Track &t) -> std::optional<std::string> {
            return t.comment.empty() ? std::nullopt : std::optional<std::string>(t.comment);
        });
    // playCount and lastPlayedAt are deliberately NOT part of this.
    //
    // They used to be, and it made the flag fire almost everywhere:
    // measured on a real 3-catalog stick, including them held back 57
    // audio files across 36 groups, against 2 files across 2 groups
    // without them. Nearly every one of those was a rekordbox row
    // carrying a play count meeting an Engine row carrying only a
    // last-played timestamp -- the two applications simply count
    // different things, so "they disagree" was being read off a
    // comparison that never had meaning.
    //
    // A play count belongs to the application that kept it. Merging one
    // across library types is not a thing that can be done correctly, and
    // it is not valuable enough to hold a cleanup hostage over. Within a
    // single library type the honest answer would be to add the counts
    // up, which is a real intent -- but no writer in this project can
    // write a play count into any of the three formats today, so that is
    // a follow-up needing a write path, not something to pretend at here.
    // The Clean Up page says so in as many words rather than leaving it
    // to be discovered.
    //
    // rating and comment stay: both are the DJ's own deliberate input,
    // both mean the same thing in every format, and losing one silently
    // is a real loss.
    result.hasUnpreservableDataAtRisk = ratingLoses || commentLoses;

    return result;
}

}  // namespace seabass::domain
