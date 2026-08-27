#include "domain/sync_planning.hpp"

#include "domain/track_matching.hpp"

namespace djconvert::domain
{

std::vector<SyncMatch> TrackMatcher::match(const std::vector<Track> &rekordboxTracks,
                                            const std::vector<Track> &engineTracks)
{
    std::vector<SyncMatch> matches;
    for (const auto &[rekordboxTrack, engineTrack] : matchTracks(rekordboxTracks, engineTracks)) {
        matches.push_back({*rekordboxTrack, *engineTrack});
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
