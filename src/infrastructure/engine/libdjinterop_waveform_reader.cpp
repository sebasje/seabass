#include "infrastructure/engine/libdjinterop_waveform_reader.hpp"

#include <algorithm>
#include <cstddef>

#include <djinterop/djinterop.hpp>

namespace djconvert::infrastructure::engine
{

namespace
{

constexpr size_t TargetPoints = 400;

}  // namespace

std::vector<double> readWaveformPreview(const std::string &engineLibraryPath, const std::string &trackSourceId)
{
    if (!djinterop::engine::database_exists(engineLibraryPath)) {
        return {};
    }

    auto db = djinterop::engine::load_database(engineLibraryPath);
    auto track = db.track_by_id(std::stoll(trackSourceId));
    if (!track) {
        return {};
    }

    std::vector<djinterop::waveform_entry> entries;
    try {
        entries = track->waveform();
    } catch (const std::exception &) {
        return {};
    }
    if (entries.empty()) {
        return {};
    }

    // Downsample to ~TargetPoints columns by averaging each bucket's
    // low/mid/high band values into a single amplitude.
    std::vector<double> waveform;
    waveform.reserve(TargetPoints);
    size_t bucketSize = std::max<size_t>(1, entries.size() / TargetPoints);
    for (size_t start = 0; start < entries.size(); start += bucketSize) {
        size_t end = std::min(entries.size(), start + bucketSize);
        double sum = 0.0;
        size_t count = 0;
        for (size_t i = start; i < end; ++i) {
            sum += entries[i].low.value + entries[i].mid.value + entries[i].high.value;
            count += 3;
        }
        waveform.push_back(count > 0 ? (sum / count) / 255.0 : 0.0);
    }
    return waveform;
}

}  // namespace djconvert::infrastructure::engine
