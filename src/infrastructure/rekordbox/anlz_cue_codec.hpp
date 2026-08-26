#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace djconvert::infrastructure::rekordbox
{

// One hot cue in the shape ANLZ's PCO2 (cue_extended_entry) format
// actually stores it. v1 scope: no comment support (always written with
// an empty comment, matching the exact byte shape validated against real
// rekordbox-written entries) and no loop support.
struct RawHotCueEntry
{
    uint32_t hotCueNumber = 0;  // 1-8
    uint32_t timeMs = 0;
    std::optional<std::tuple<uint8_t, uint8_t, uint8_t>> color;  // (r, g, b), if any
};

// Encodes/decodes the PCO2 (cue_extended_tag, hot cues) section of an
// ANLZ file.
//
// Confidence levels, from real-data validation against seven cues across
// four different real rekordbox-written files this session:
//  - magic/lengths/type/hot_cue/time/color_id/loop_numerator/denominator/
//    len_comment/color fields: HIGH -- decoded and verified byte-for-byte
//    against known cue positions and colors.
//  - the 3-byte field after `type` (called "pad3" here): HIGH that it's a
//    fixed constant -- 0x0003E8 in all seven real examples checked.
//  - loop_time: HIGH that 0xFFFFFFFF means "not a loop" -- true in six of
//    seven examples (one real cue had 0 instead; either value appears to
//    be accepted, we use the more common one).
//  - the 7-byte field after `color_id` (called "pad7" here): LOW
//    confidence. The Kaitai spec this project vendors treats it as opaque
//    padding, but real data shows its first byte is always 0x01, while
//    the next two bytes vary per cue in a way we could not correlate with
//    time, hot cue number, or color. It may be an undocumented per-cue
//    ordering/sequence field. We default new entries to byte 0 = 0x01
//    (well-evidenced) plus an incrementing counter for the next two bytes
//    (matches the *shape* of real data -- distinct, nonzero, varying per
//    cue -- without claiming to know its real meaning). This is the one
//    place in the writer where hardware/rekordbox verification genuinely
//    matters before trusting it for a gig.
class AnlzCueCodec
{
public:
    static std::vector<RawHotCueEntry> decodeHotCues(const std::string &pco2SectionBytes);
    static std::string encodeHotCues(const std::vector<RawHotCueEntry> &cues);
};

}  // namespace djconvert::infrastructure::rekordbox
