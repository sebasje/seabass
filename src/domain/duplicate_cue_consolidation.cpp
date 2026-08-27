#include "domain/duplicate_cue_consolidation.hpp"

#include <cmath>
#include <map>
#include <numeric>
#include <optional>

#include "domain/track_matching.hpp"

namespace djconvert::domain
{

namespace
{

constexpr double DurationToleranceSeconds = 2.0;

size_t findRoot(std::vector<size_t> &parent, size_t x)
{
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

void unite(std::vector<size_t> &parent, size_t a, size_t b)
{
    parent[findRoot(parent, a)] = findRoot(parent, b);
}

}  // namespace

std::vector<DuplicateGroup> DuplicateTrackFinder::find(const std::vector<Track> &tracks)
{
    // Two tracks are candidates if they share a filename OR share a
    // title+artist -- union-find merges both criteria transitively into
    // one candidate set per underlying song.
    std::vector<size_t> parent(tracks.size());
    std::iota(parent.begin(), parent.end(), size_t{0});

    std::map<std::string, std::vector<size_t>> byFilename;
    std::map<std::string, std::vector<size_t>> byTitleArtist;
    for (size_t i = 0; i < tracks.size(); ++i) {
        byFilename[normalizeFilename(tracks[i].filename)].push_back(i);
        if (auto key = titleArtistKey(tracks[i])) {
            byTitleArtist[*key].push_back(i);
        }
    }
    for (const auto &[key, indices] : byFilename) {
        for (size_t k = 1; k < indices.size(); ++k) {
            unite(parent, indices[0], indices[k]);
        }
    }
    for (const auto &[key, indices] : byTitleArtist) {
        for (size_t k = 1; k < indices.size(); ++k) {
            unite(parent, indices[0], indices[k]);
        }
    }

    std::map<size_t, std::vector<size_t>> byRoot;
    for (size_t i = 0; i < tracks.size(); ++i) {
        byRoot[findRoot(parent, i)].push_back(i);
    }

    std::vector<DuplicateGroup> groups;
    for (auto &[root, indices] : byRoot) {
        if (indices.size() < 2) {
            continue;
        }

        // Within a candidate set, cluster by duration tolerance -- two
        // unrelated tracks that happen to share a filename or title+artist
        // (e.g. a cover version) shouldn't be treated as duplicates.
        std::vector<bool> used(indices.size(), false);
        for (size_t i = 0; i < indices.size(); ++i) {
            if (used[i]) {
                continue;
            }
            DuplicateGroup group;
            group.tracks.push_back(tracks[indices[i]]);
            used[i] = true;
            for (size_t j = i + 1; j < indices.size(); ++j) {
                if (used[j]) {
                    continue;
                }
                if (std::abs(tracks[indices[i]].durationSeconds - tracks[indices[j]].durationSeconds) <=
                    DurationToleranceSeconds) {
                    group.tracks.push_back(tracks[indices[j]]);
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
