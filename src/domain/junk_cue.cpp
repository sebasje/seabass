#include "domain/junk_cue.hpp"

namespace djconvert::domain
{

std::vector<JunkCueIssue> JunkCueFinder::find(const std::vector<Track> &tracks)
{
    std::vector<JunkCueIssue> issues;
    for (const auto &track : tracks) {
        for (const auto &cue : track.cues) {
            if (cue.kind == CuePoint::Kind::Memory && cue.positionMs == 0.0) {
                issues.push_back(JunkCueIssue{track, cue});
            }
        }
    }
    return issues;
}

}  // namespace djconvert::domain
