#pragma once

#include <chrono>
#include <vector>

#include "domain/sync_planning.hpp"
#include "domain/track.hpp"

namespace djconvert::application
{

// Read-only use case: match tracks across a rekordbox scan and an Engine
// scan, and decide what should happen to each pair's cues. Applying a plan
// is a separate step (see cli/), since only some directions are writable.
class SyncLibraries
{
public:
    std::vector<domain::SyncPlan> execute(const std::vector<domain::Track> &rekordboxTracks,
                                           const std::vector<domain::Track> &engineTracks,
                                           std::chrono::system_clock::time_point rekordboxMtime,
                                           std::chrono::system_clock::time_point engineMtime)
    {
        std::vector<domain::SyncPlan> plans;
        for (const auto &match : domain::TrackMatcher::match(rekordboxTracks, engineTracks)) {
            plans.push_back(domain::SyncPlanner::plan(match, rekordboxMtime, engineMtime));
        }
        return plans;
    }
};

}  // namespace djconvert::application
