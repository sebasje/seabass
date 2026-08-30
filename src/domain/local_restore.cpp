#include "domain/local_restore.hpp"

#include <cmath>

namespace seabass::domain
{

std::vector<CuePoint> LocalRestorePlanner::mergeCues(const std::vector<CuePoint> &existing,
                                                       const std::vector<CuePoint> &incoming)
{
    std::vector<CuePoint> merged = existing;

    for (const auto &candidate : incoming) {
        if (candidate.kind == CuePoint::Kind::Hot) {
            bool slotTaken = false;
            for (const auto &have : existing) {
                if (have.kind == CuePoint::Kind::Hot && have.hotCueNumber == candidate.hotCueNumber) {
                    slotTaken = true;
                    break;
                }
            }
            if (!slotTaken) {
                merged.push_back(candidate);
            }
        } else {
            bool nearExisting = false;
            for (const auto &have : existing) {
                if (have.kind == CuePoint::Kind::Memory &&
                    std::abs(have.positionMs - candidate.positionMs) <= PositionToleranceMs) {
                    nearExisting = true;
                    break;
                }
            }
            if (!nearExisting) {
                merged.push_back(candidate);
            }
        }
    }

    return merged;
}

std::vector<RestoreCandidate> LocalRestorePlanner::plan(
    const std::vector<std::pair<const Track *, const Track *>> &matches)
{
    std::vector<RestoreCandidate> candidates;
    for (const auto &[stickTrack, localTrack] : matches) {
        auto merged = mergeCues(stickTrack->cues, localTrack->cues);
        if (merged.size() > stickTrack->cues.size()) {
            candidates.push_back({*stickTrack, *localTrack, std::move(merged)});
        }
    }
    return candidates;
}

}  // namespace seabass::domain
