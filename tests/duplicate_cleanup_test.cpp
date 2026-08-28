#include <cassert>
#include <iostream>

#include "domain/duplicate_cleanup.hpp"

using namespace djconvert::domain;

namespace
{

Track makeTrack(std::string id, double duration, int bitrate, std::uint64_t sizeBytes, std::vector<CuePoint> cues = {})
{
    Track t;
    t.sourceId = std::move(id);
    t.filename = "song.mp3";
    t.durationSeconds = duration;
    t.bitrate = bitrate;
    t.fileSizeBytes = sizeBytes;
    t.cues = std::move(cues);
    return t;
}

bool hasTrack(const std::vector<Track> &tracks, const std::string &id)
{
    for (const auto &t : tracks) {
        if (t.sourceId == id) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main()
{
    // Higher bitrate, same duration -> that copy survives, no disagreement.
    {
        DuplicateGroup group{{makeTrack("a", 200.0, 128, 3'200'000), makeTrack("b", 200.0, 320, 8'000'000)}};
        auto plan = DuplicateCleanupPlanner::plan(group);
        assert(plan.survivor.sourceId == "b");
        assert(plan.toRemove.size() == 1 && plan.toRemove[0].sourceId == "a");
        assert(!plan.differs);
        std::cout << "case 1 (higher bitrate survives, agree) OK\n";
    }

    // Higher-bitrate copy is meaningfully shorter -- quality and length
    // disagree on which is "best". Survivor is still the higher-bitrate
    // copy, but flagged for review rather than silently auto-applied.
    {
        DuplicateGroup group{{makeTrack("a", 200.0, 320, 8'000'000), makeTrack("b", 260.0, 128, 4'000'000)}};
        auto plan = DuplicateCleanupPlanner::plan(group);
        assert(plan.survivor.sourceId == "a");
        assert(plan.differs);
        std::cout << "case 2 (quality vs length disagree -> differs=true) OK\n";
    }

    // Bitrate unknown on both -- falls back to duration, never flagged
    // as "differs" since there's no real quality signal to disagree with.
    {
        DuplicateGroup group{{makeTrack("a", 200.0, 0, 3'000'000), makeTrack("b", 260.0, 0, 3'500'000)}};
        auto plan = DuplicateCleanupPlanner::plan(group);
        assert(plan.survivor.sourceId == "b");
        assert(!plan.differs);
        std::cout << "case 3 (bitrate unknown -> falls back to duration, never differs) OK\n";
    }

    // Cue merging: the survivor's own cues are kept as-is; a hot cue in
    // a different slot and a memory cue far from any existing one are
    // added from the removed copy; a colliding hot cue slot and a
    // near-duplicate memory cue are NOT duplicated.
    {
        std::vector<CuePoint> survivorCues = {
            CuePoint{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"},
            CuePoint{CuePoint::Kind::Memory, 0, 5000.0, "", ""},
        };
        std::vector<CuePoint> removedCues = {
            CuePoint{CuePoint::Kind::Hot, 1, 1500.0, "#00FF00", "different position, same slot"},  // slot taken, dropped
            CuePoint{CuePoint::Kind::Hot, 2, 2000.0, "#0000FF", "new slot"},                        // added
            CuePoint{CuePoint::Kind::Memory, 0, 5100.0, "", ""},                                    // near-duplicate, dropped
            CuePoint{CuePoint::Kind::Memory, 0, 40000.0, "", ""},                                   // far away, added
        };
        DuplicateGroup group{{makeTrack("a", 200.0, 320, 8'000'000, survivorCues),
                               makeTrack("b", 200.0, 128, 3'000'000, removedCues)}};
        auto plan = DuplicateCleanupPlanner::plan(group);
        assert(plan.survivor.sourceId == "a");
        assert(plan.mergedCuesForSurvivor.size() == 4);  // 2 original + hot slot 2 + far memory cue
        std::cout << "case 4 (cue merge: adds gaps, never duplicates) OK\n";
    }

    // Degenerate single-track group -- survivor is that track, nothing
    // to remove.
    {
        DuplicateGroup group{{makeTrack("solo", 200.0, 320, 8'000'000)}};
        auto plan = DuplicateCleanupPlanner::plan(group);
        assert(plan.survivor.sourceId == "solo");
        assert(plan.toRemove.empty());
        assert(!plan.differs);
        std::cout << "case 5 (single-track group -- nothing to remove) OK\n";
    }

    // Tie on bitrate -- longer duration wins the tie-break.
    {
        DuplicateGroup group{{makeTrack("a", 200.0, 320, 8'000'000), makeTrack("b", 210.0, 320, 8'100'000)}};
        auto plan = DuplicateCleanupPlanner::plan(group);
        assert(plan.survivor.sourceId == "b");
        assert(hasTrack(plan.toRemove, "a"));
        std::cout << "case 6 (tied bitrate -> longer duration wins tie-break) OK\n";
    }

    std::cout << "all cases passed\n";
    return 0;
}
