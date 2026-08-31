#include <cassert>
#include <chrono>
#include <iostream>

#include "domain/duplicate_cleanup.hpp"

using namespace seabass::domain;

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

    // Propagation is a per-field "fill a gap", not a merge: survivor
    // ("a", higher bitrate) is missing bpm/key/artwork entirely; the
    // removed copy ("b") has all three. Each should carry forward, with
    // "b" recorded as the donor for each.
    {
        Track a = makeTrack("a", 200.0, 320, 8'000'000);
        Track b = makeTrack("b", 200.0, 128, 3'000'000);
        b.bpm = 128.0;
        b.key = "Fm";
        b.artworkPath = "/stick/art/b.jpg";
        DuplicateGroup group{{a, b}};
        auto plan = DuplicateCleanupPlanner::plan(group);
        assert(plan.survivor.sourceId == "a");
        assert(plan.bpmForSurvivor.has_value() && *plan.bpmForSurvivor == 128.0);
        assert(plan.bpmDonorSourceId == "b");
        assert(plan.keyForSurvivor.has_value() && *plan.keyForSurvivor == "Fm");
        assert(plan.keyDonorSourceId == "b");
        assert(plan.artworkPathForSurvivor.has_value() && *plan.artworkPathForSurvivor == "/stick/art/b.jpg");
        assert(plan.artworkDonorSourceId == "b");
        std::cout << "case 7 (bpm/key/artwork propagate from donor when survivor lacks them) OK\n";
    }

    // Survivor already has bpm/key/artwork -- nothing propagates, even
    // though another copy also has values (there's no gap to fill).
    {
        Track a = makeTrack("a", 200.0, 320, 8'000'000);
        a.bpm = 174.0;
        a.key = "Am";
        a.artworkPath = "/stick/art/a.jpg";
        Track b = makeTrack("b", 200.0, 128, 3'000'000);
        b.bpm = 128.0;
        b.key = "Fm";
        b.artworkPath = "/stick/art/b.jpg";
        DuplicateGroup group{{a, b}};
        auto plan = DuplicateCleanupPlanner::plan(group);
        assert(plan.survivor.sourceId == "a");
        assert(!plan.bpmForSurvivor.has_value());
        assert(!plan.keyForSurvivor.has_value());
        assert(!plan.artworkPathForSurvivor.has_value());
        std::cout << "case 8 (survivor already has bpm/key/artwork -- nothing propagates) OK\n";
    }

    // Neither copy has bpm/key/artwork -- nothing to propagate, no crash.
    {
        DuplicateGroup group{{makeTrack("a", 200.0, 320, 8'000'000), makeTrack("b", 200.0, 128, 3'000'000)}};
        auto plan = DuplicateCleanupPlanner::plan(group);
        assert(!plan.bpmForSurvivor.has_value());
        assert(!plan.keyForSurvivor.has_value());
        assert(!plan.artworkPathForSurvivor.has_value());
        std::cout << "case 9 (neither copy has bpm/key/artwork -- nothing propagates) OK\n";
    }

    // hasUnpreservableDataAtRisk: two copies with genuinely different
    // ratings -- real data that would be silently lost, distinct from
    // (and independent of) `differs`.
    {
        Track a = makeTrack("a", 200.0, 320, 8'000'000);
        a.rating = 5;
        Track b = makeTrack("b", 200.0, 128, 3'000'000);
        b.rating = 2;
        DuplicateGroup group{{a, b}};
        auto plan = DuplicateCleanupPlanner::plan(group);
        assert(plan.hasUnpreservableDataAtRisk);
        assert(!plan.differs);
        std::cout << "case 10 (differing ratings -> hasUnpreservableDataAtRisk, not differs) OK\n";
    }

    // Only one copy has a rating (the other has none set) -- nothing to
    // lose, since there's only ever one real value in the group.
    {
        Track a = makeTrack("a", 200.0, 320, 8'000'000);
        a.rating = 5;
        Track b = makeTrack("b", 200.0, 128, 3'000'000);
        DuplicateGroup group{{a, b}};
        auto plan = DuplicateCleanupPlanner::plan(group);
        assert(!plan.hasUnpreservableDataAtRisk);
        std::cout << "case 11 (only one copy has a rating -- not at risk) OK\n";
    }

    // Both copies agree on rating/comment -- not at risk either.
    {
        Track a = makeTrack("a", 200.0, 320, 8'000'000);
        a.rating = 4;
        a.comment = "banger";
        Track b = makeTrack("b", 200.0, 128, 3'000'000);
        b.rating = 4;
        b.comment = "banger";
        DuplicateGroup group{{a, b}};
        auto plan = DuplicateCleanupPlanner::plan(group);
        assert(!plan.hasUnpreservableDataAtRisk);
        std::cout << "case 12 (agreeing rating/comment -- not at risk) OK\n";
    }

    // Differing comment, playCount, and lastPlayedAt each independently
    // trip the flag too, not just rating.
    {
        Track a = makeTrack("a", 200.0, 320, 8'000'000);
        a.comment = "keeper";
        Track b = makeTrack("b", 200.0, 128, 3'000'000);
        b.comment = "meh";
        DuplicateGroup group{{a, b}};
        auto plan = DuplicateCleanupPlanner::plan(group);
        assert(plan.hasUnpreservableDataAtRisk);
        std::cout << "case 13 (differing comment -> hasUnpreservableDataAtRisk) OK\n";
    }
    {
        Track a = makeTrack("a", 200.0, 320, 8'000'000);
        a.playCount = 10;
        Track b = makeTrack("b", 200.0, 128, 3'000'000);
        b.playCount = 3;
        DuplicateGroup group{{a, b}};
        auto plan = DuplicateCleanupPlanner::plan(group);
        assert(plan.hasUnpreservableDataAtRisk);
        std::cout << "case 14 (differing playCount -> hasUnpreservableDataAtRisk) OK\n";
    }
    {
        Track a = makeTrack("a", 200.0, 320, 8'000'000);
        a.lastPlayedAt = std::chrono::system_clock::time_point{std::chrono::seconds{1000}};
        Track b = makeTrack("b", 200.0, 128, 3'000'000);
        b.lastPlayedAt = std::chrono::system_clock::time_point{std::chrono::seconds{2000}};
        DuplicateGroup group{{a, b}};
        auto plan = DuplicateCleanupPlanner::plan(group);
        assert(plan.hasUnpreservableDataAtRisk);
        std::cout << "case 15 (differing lastPlayedAt -> hasUnpreservableDataAtRisk) OK\n";
    }

    // `differs` (quality/length disagreement) and
    // `hasUnpreservableDataAtRisk` (per-copy DJ data disagreement) are
    // genuinely independent flags: this group differs on quality/length
    // but agrees on every DJ-data field, so only `differs` should be set.
    {
        Track a = makeTrack("a", 200.0, 320, 8'000'000);  // higher bitrate, shorter
        a.rating = 4;
        Track b = makeTrack("b", 260.0, 128, 4'000'000);  // lower bitrate, longer
        b.rating = 4;
        DuplicateGroup group{{a, b}};
        auto plan = DuplicateCleanupPlanner::plan(group);
        assert(plan.differs);
        assert(!plan.hasUnpreservableDataAtRisk);
        std::cout << "case 16 (differs without hasUnpreservableDataAtRisk -- flags are independent) OK\n";
    }

    // Regression test for a real bug found in review: case 11 above
    // ("only one copy has a rating -- not at risk") happens to put the
    // rated track on the higher-bitrate copy, which is also the
    // survivor -- so it can't tell "only the survivor has it" (safe)
    // apart from "only a copy about to be REMOVED has it" (a real,
    // silent loss). Here the LOWER-bitrate (doomed) copy is the one
    // with the rating; the higher-bitrate (survivor) copy has none.
    // Must be flagged: cleanup would keep the unrated survivor and
    // discard the only rating that ever existed for this track.
    {
        Track a = makeTrack("a", 200.0, 320, 8'000'000);  // higher bitrate -> survivor, no rating
        Track b = makeTrack("b", 200.0, 128, 3'000'000);  // lower bitrate -> doomed, has a rating
        b.rating = 5;
        DuplicateGroup group{{a, b}};
        auto plan = DuplicateCleanupPlanner::plan(group);
        assert(plan.survivor.sourceId == "a");
        assert(plan.hasUnpreservableDataAtRisk);
        std::cout << "case 17 (only a DOOMED copy has a rating -- flagged as at risk, not silently kept) OK\n";
    }

    std::cout << "all cases passed\n";
    return 0;
}
