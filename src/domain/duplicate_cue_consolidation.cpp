#include "domain/duplicate_cue_consolidation.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace djconvert::domain
{

namespace
{

constexpr double DurationToleranceSeconds = 2.0;
constexpr double PositionToleranceMs = 1000.0;

std::string normalizeFilename(const std::string &filename)
{
    std::string result;
    result.reserve(filename.size());
    for (unsigned char c : filename) {
        if (!std::isspace(c)) {
            result.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return result;
}

std::vector<CuePoint> sortedCues(std::vector<CuePoint> cues)
{
    std::sort(cues.begin(), cues.end(), [](const CuePoint &a, const CuePoint &b) {
        if (a.kind != b.kind) {
            return a.kind < b.kind;
        }
        if (a.hotCueNumber != b.hotCueNumber) {
            return a.hotCueNumber < b.hotCueNumber;
        }
        return a.positionMs < b.positionMs;
    });
    return cues;
}

bool cueSetsEqual(const std::vector<CuePoint> &a, const std::vector<CuePoint> &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    auto sortedA = sortedCues(a);
    auto sortedB = sortedCues(b);
    for (size_t i = 0; i < sortedA.size(); ++i) {
        const auto &x = sortedA[i];
        const auto &y = sortedB[i];
        if (x.kind != y.kind || x.hotCueNumber != y.hotCueNumber || x.color != y.color ||
            x.comment != y.comment) {
            return false;
        }
        if (std::abs(x.positionMs - y.positionMs) > PositionToleranceMs) {
            return false;
        }
    }
    return true;
}

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
