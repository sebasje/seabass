#include "domain/local_restore.hpp"

namespace djconvert::domain
{

std::vector<RestoreCandidate> LocalRestorePlanner::plan(
    const std::vector<std::pair<const Track *, const Track *>> &matches)
{
    std::vector<RestoreCandidate> candidates;
    for (const auto &[stickTrack, localTrack] : matches) {
        if (stickTrack->cues.empty() && !localTrack->cues.empty()) {
            candidates.push_back({*stickTrack, *localTrack});
        }
    }
    return candidates;
}

}  // namespace djconvert::domain
