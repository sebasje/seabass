#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"

#include <algorithm>
#include <stdexcept>

#include "infrastructure/rekordbox/anlz_cue_codec.hpp"
#include "infrastructure/rekordbox/anlz_file.hpp"
#include "infrastructure/rekordbox/big_endian.hpp"
#include "infrastructure/rekordbox/pdb_lookup.hpp"

namespace seabass::infrastructure::rekordbox
{

namespace
{

constexpr uint32_t Pco2Fourcc = 0x50434f32;  // "PCO2"

bool isCueListSection(const AnlzRawSection &section, uint32_t listType)
{
    return section.fourcc == Pco2Fourcc && section.rawBytes.size() >= 16 &&
           readU32BE(section.rawBytes, 12) == listType;
}

std::optional<std::tuple<uint8_t, uint8_t, uint8_t>> parseColor(const std::string &color)
{
    if (color.size() == 7 && color[0] == '#') {
        auto hexByte = [&](size_t pos) {
            return static_cast<uint8_t>(std::stoi(color.substr(pos, 2), nullptr, 16));
        };
        return std::make_tuple(hexByte(1), hexByte(3), hexByte(5));
    }
    return std::nullopt;
}

// Overwrites the existing PCO2 section of `listType` with `entries`, or
// appends a freshly encoded one if the file doesn't have one yet (a track
// with no memory cues at all may genuinely have no memory-cues PCO2
// section on disk -- see anlz_cue_codec.hpp's confidence notes for the
// real empty-section example this shape is confirmed against). Does
// nothing if there's no existing section AND nothing to write, so a
// track that has and needs neither list is left byte-for-byte untouched.
void writeCueList(AnlzFile &file, uint32_t listType, const std::vector<RawHotCueEntry> &entries)
{
    auto sectionIt = std::find_if(file.sections.begin(), file.sections.end(),
                                   [listType](const AnlzRawSection &s) { return isCueListSection(s, listType); });
    if (sectionIt == file.sections.end() && entries.empty()) {
        return;
    }
    std::string encoded = AnlzCueCodec::encodeHotCues(entries, listType);
    if (sectionIt != file.sections.end()) {
        sectionIt->rawBytes = encoded;
    } else {
        file.sections.push_back({Pco2Fourcc, encoded});
    }
}

}  // namespace

RekordboxCueWriter::RekordboxCueWriter(std::string pioneerRoot) : m_pioneerRoot(std::move(pioneerRoot)) {}

void RekordboxCueWriter::writeHotCues(const std::string &trackSourceId, const std::vector<domain::CuePoint> &cues)
{
    uint32_t trackId = static_cast<uint32_t>(std::stoul(trackSourceId));

    auto analyzePath = findAnlzPathForTrackId(m_pioneerRoot, trackId);
    if (!analyzePath) {
        throw std::runtime_error("no rekordbox track with id=" + trackSourceId + " (or it has no analysis file)");
    }
    std::string extPath = extAnlzPath(m_pioneerRoot, *analyzePath);

    auto file = AnlzFile::readRaw(extPath);

    std::vector<RawHotCueEntry> hotEntries;
    std::vector<RawHotCueEntry> memoryEntries;
    for (const auto &cue : cues) {
        RawHotCueEntry entry;
        entry.timeMs = static_cast<uint32_t>(cue.positionMs);
        entry.color = parseColor(cue.color);
        if (cue.kind == domain::CuePoint::Kind::Hot) {
            entry.hotCueNumber = static_cast<uint32_t>(cue.hotCueNumber);
            hotEntries.push_back(entry);
        } else {
            entry.hotCueNumber = 0;  // memory cues carry no hot-cue slot
            memoryEntries.push_back(entry);
        }
    }

    writeCueList(file, CueListTypeHot, hotEntries);
    writeCueList(file, CueListTypeMemory, memoryEntries);

    file.writeRaw(extPath);
}

}  // namespace seabass::infrastructure::rekordbox
