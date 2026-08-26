#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"

#include <optional>
#include <stdexcept>

#include <djinterop/djinterop.hpp>

namespace djconvert::infrastructure::engine
{

namespace
{

constexpr int HotCueSlotCount = 8;

djinterop::pad_color parseColor(const std::string &color)
{
    if (color.size() == 7 && color[0] == '#') {
        auto hexByte = [&](size_t pos) {
            return static_cast<std::uint8_t>(std::stoi(color.substr(pos, 2), nullptr, 16));
        };
        return djinterop::pad_color{hexByte(1), hexByte(3), hexByte(5), 0xFF};
    }
    return djinterop::pad_color{};
}

}  // namespace

LibdjinteropEngineCueWriter::LibdjinteropEngineCueWriter(std::string engineLibraryPath)
    : m_engineLibraryPath(std::move(engineLibraryPath))
{
}

void LibdjinteropEngineCueWriter::writeHotCues(const std::string &trackSourceId,
                                                const std::vector<domain::CuePoint> &cues)
{
    auto db = djinterop::engine::load_database(m_engineLibraryPath);

    auto track = db.track_by_id(std::stoll(trackSourceId));
    if (!track) {
        throw std::runtime_error("no Engine track with id=" + trackSourceId);
    }

    // Some tracks' sample_rate() throws even though set_hot_cues() below
    // works fine (same libdjinterop decoder quirk documented in the Engine
    // reader: it lives in a different blob than the one hot cues use).
    // Fall back to 44.1kHz -- see libdjinterop_engine_reader.cpp for the
    // same reasoning.
    double sampleRate = 44100.0;
    try {
        if (auto rate = track->sample_rate()) {
            sampleRate = *rate;
        }
    } catch (const std::exception &) {
        // fall back to the default above
    }

    std::vector<std::optional<djinterop::hot_cue>> slots(HotCueSlotCount);
    for (const auto &cue : cues) {
        if (cue.kind != domain::CuePoint::Kind::Hot) {
            continue;  // memory cues aren't handled by this writer yet
        }
        int slot = cue.hotCueNumber - 1;
        if (slot < 0 || slot >= HotCueSlotCount) {
            continue;
        }
        double sampleOffset = cue.positionMs / 1000.0 * sampleRate;
        slots[slot] = djinterop::hot_cue{cue.comment, sampleOffset, parseColor(cue.color)};
    }

    track->set_hot_cues(slots);
}

}  // namespace djconvert::infrastructure::engine
