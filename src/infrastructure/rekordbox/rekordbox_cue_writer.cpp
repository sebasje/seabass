#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"

#include <algorithm>
#include <stdexcept>

#include "infrastructure/rekordbox/anlz_cue_codec.hpp"
#include "infrastructure/rekordbox/anlz_file.hpp"
#include "infrastructure/rekordbox/big_endian.hpp"
#include "infrastructure/rekordbox/pdb_lookup.hpp"

namespace djconvert::infrastructure::rekordbox
{

namespace
{

constexpr uint32_t Pco2Fourcc = 0x50434f32;  // "PCO2"
constexpr uint32_t HotCuesType = 1;          // cue_list_type::hot_cues

bool isHotCuesSection(const AnlzRawSection &section)
{
    return section.fourcc == Pco2Fourcc && section.rawBytes.size() >= 16 &&
           readU32BE(section.rawBytes, 12) == HotCuesType;
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

    auto sectionIt = std::find_if(file.sections.begin(), file.sections.end(), isHotCuesSection);
    if (sectionIt == file.sections.end()) {
        throw std::runtime_error(extPath + " has no hot-cues (PCO2) section to write into");
    }

    std::vector<RawHotCueEntry> newCues;
    for (const auto &cue : cues) {
        if (cue.kind != domain::CuePoint::Kind::Hot) {
            continue;  // memory cues aren't handled by this writer yet
        }
        RawHotCueEntry entry;
        entry.hotCueNumber = static_cast<uint32_t>(cue.hotCueNumber);
        entry.timeMs = static_cast<uint32_t>(cue.positionMs);
        entry.color = parseColor(cue.color);
        newCues.push_back(entry);
    }

    sectionIt->rawBytes = AnlzCueCodec::encodeHotCues(newCues);
    file.writeRaw(extPath);
}

}  // namespace djconvert::infrastructure::rekordbox
