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

    return result;
}

}  // namespace seabass::domain
