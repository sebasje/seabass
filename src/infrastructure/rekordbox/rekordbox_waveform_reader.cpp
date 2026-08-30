#include "infrastructure/rekordbox/rekordbox_waveform_reader.hpp"

#include <fstream>

#include "infrastructure/rekordbox/generated/rekordbox_anlz.h"
#include "infrastructure/rekordbox/pdb_lookup.hpp"

namespace seabass::infrastructure::rekordbox
{

using Anlz = rekordbox_anlz_t;

std::vector<domain::WaveformColumn> readWaveformPreview(const std::string &pioneerRoot,
                                                          const std::string &trackSourceId)
{
    // Best-effort per the header contract: findAnlzPathForTrackId() throws
    // if export.pdb itself can't be opened (e.g. the stick was unmounted
    // right as playback was requested), which callers -- notably
    // PlaybackController::load(), invoked directly from every Play click --
    // don't expect and don't guard against.
    try {
        auto analyzePath = findAnlzPathForTrackId(pioneerRoot, static_cast<uint32_t>(std::stoul(trackSourceId)));
        if (!analyzePath) {
            return {};
        }

        std::ifstream ifs(datAnlzPath(pioneerRoot, *analyzePath), std::ifstream::binary);
        if (!ifs.is_open()) {
            return {};
        }

        kaitai::kstream ks(&ifs);
        Anlz anlz(&ks);

        for (const auto &section : *anlz.sections()) {
            if (section->fourcc() != Anlz::SECTION_TAGS_WAVE_PREVIEW) {
                continue;
            }
            auto *tag = dynamic_cast<Anlz::wave_preview_tag_t *>(section->body());
            if (!tag || tag->_is_null_data()) {
                continue;
            }

            std::string data = tag->data();
            std::vector<domain::WaveformColumn> waveform;
            waveform.reserve(data.size());
            for (unsigned char b : data) {
                double height = (b & 0x1F) / 31.0;
                double whiteness = ((b >> 5) & 0x07) / 7.0;
                domain::WaveformColumn col;
                col.low = height;
                col.mid = height * (0.55 + 0.45 * whiteness);
                col.high = height * whiteness;
                waveform.push_back(col);
            }
            return waveform;
        }
    } catch (const std::exception &) {
        return {};
    }
    return {};
}

}  // namespace seabass::infrastructure::rekordbox
