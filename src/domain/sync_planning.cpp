#include "domain/sync_planning.hpp"

#include <cmath>
#include <map>

#include "domain/track_matching.hpp"

namespace djconvert::domain
{

namespace
{

constexpr double DurationToleranceSeconds = 2.0;

}  // namespace

std::vector<SyncMatch> TrackMatcher::match(const std::vector<Track> &rekordboxTracks,
                                            const std::vector<Track> &engineTracks)
{
    std::map<std::string, std::vector<const Track *>> engineByFilename;
    for (const auto &track : engineTracks) {
        engineByFilename[normalizeFilename(track.filename)].push_back(&track);
    }

    std::vector<SyncMatch> matches;
    for (const auto &rekordboxTrack : rekordboxTracks) {
        auto it = engineByFilename.find(normalizeFilename(rekordboxTrack.filename));
        if (it == engineByFilename.end()) {
            continue;
        }
        for (const auto *engineTrack : it->second) {
            if (std::abs(rekordboxTrack.durationSeconds - engineTrack->durationSeconds) <= DurationToleranceSeconds) {
                matches.push_back({rekordboxTrack, *engineTrack});
                break;  // one match per rekordbox track is enough for cue syncing
            }
        }
    }
    return matches;
}

SyncPlan SyncPlanner::plan(const SyncMatch &match, std::chrono::system_clock::time_point rekordboxMtime,
                            std::chrono::system_clock::time_point engineMtime)
{
    SyncPlan result;
    result.match = match;

    bool rekordboxHasCues = !match.rekordboxTrack.cues.empty();
    bool engineHasCues = !match.engineTrack.cues.empty();

    if (!rekordboxHasCues && !engineHasCues) {
        result.kind = SyncPlan::Kind::NoCues;
        return result;
    }

    if (rekordboxHasCues && !engineHasCues) {
        result.kind = SyncPlan::Kind::RekordboxOnly;
        result.direction = SyncPlan::Direction::ToEngine;
        result.cuesToApply = match.rekordboxTrack.cues;
        return result;
    }

    if (!rekordboxHasCues && engineHasCues) {
        result.kind = SyncPlan::Kind::EngineOnly;
        result.direction = SyncPlan::Direction::ToRekordbox;
        result.cuesToApply = match.engineTrack.cues;
        return result;
    }

    // Both sides have cues.
    if (cueSetsEqual(match.rekordboxTrack.cues, match.engineTrack.cues)) {
        result.kind = SyncPlan::Kind::AlreadyConsistent;
        return result;
    }

    result.kind = SyncPlan::Kind::Conflict;
    if (rekordboxMtime > engineMtime) {
        result.direction = SyncPlan::Direction::ToEngine;
        result.cuesToApply = match.rekordboxTrack.cues;
    } else {
        result.direction = SyncPlan::Direction::ToRekordbox;
        result.cuesToApply = match.engineTrack.cues;
    }
    return result;
}

}  // namespace djconvert::domain
