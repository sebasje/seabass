#include "domain/duplicate_cue_consolidation.hpp"

#include <cmath>
#include <map>

#include "domain/track_matching.hpp"

namespace djconvert::domain
{

namespace
{

constexpr double DurationToleranceSeconds = 2.0;

}  // namespace

std::vector<DuplicateGroup> DuplicateTrackFinder::find(const std::vector<Track> &tracks)
{
    std::map<std::string, std::vector<Track>> byFilename;
    for (const auto &track : tracks) {
        byFilename[normalizeFilename(track.filename)].push_back(track);
    }

    std::vector<DuplicateGroup> groups;
    for (auto &[filename, candidates] : byFilename) {
        if (candidates.size() < 2) {
            continue;
        }

        // Within a shared filename, cluster by duration tolerance -- two
        // unrelated tracks that happen to share a filename (e.g. "01.mp3"
        // from different releases) shouldn't be treated as duplicates.
        std::vector<bool> used(candidates.size(), false);
        for (size_t i = 0; i < candidates.size(); ++i) {
            if (used[i]) {
                continue;
            }
            DuplicateGroup group;
            group.tracks.push_back(candidates[i]);
            used[i] = true;
            for (size_t j = i + 1; j < candidates.size(); ++j) {
                if (used[j]) {
                    continue;
                }
                if (std::abs(candidates[i].durationSeconds - candidates[j].durationSeconds) <=
                    DurationToleranceSeconds) {
                    group.tracks.push_back(candidates[j]);
                    used[j] = true;
                }
            }
            if (group.tracks.size() >= 2) {
                groups.push_back(std::move(group));
            }
        }
    }

    return groups;
}

ConsolidationPlan DuplicateCueConsolidator::plan(const DuplicateGroup &group)
{
    ConsolidationPlan result;
    result.group = group;

    std::vector<const Track *> withCues;
    for (const auto &track : group.tracks) {
        if (!track.cues.empty()) {
            withCues.push_back(&track);
        }
    }

    if (withCues.empty()) {
        result.kind = ConsolidationPlan::Kind::NoCues;
        return result;
    }

    if (withCues.size() == 1) {
        result.kind = ConsolidationPlan::Kind::Unambiguous;
        result.source = *withCues.front();
        for (const auto &track : group.tracks) {
            if (track.sourceId != result.source->sourceId) {
                result.targets.push_back(track);
            }
        }
        return result;
    }

    for (size_t i = 1; i < withCues.size(); ++i) {
        if (!cueSetsEqual(withCues[0]->cues, withCues[i]->cues)) {
            result.kind = ConsolidationPlan::Kind::Conflict;
            return result;
        }
    }
    result.kind = ConsolidationPlan::Kind::AlreadyConsistent;
    return result;
}

}  // namespace djconvert::domain
