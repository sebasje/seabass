#pragma once

#include <string>
#include <vector>

namespace djconvert::infrastructure::engine
{

// Reads an Engine Library track's waveform (via libdjinterop's
// track::waveform(), which is typically full audio resolution -- one
// entry per few dozen samples) and downsamples it to ~400 points,
// normalized to 0..1 amplitude, by averaging the low/mid/high bands.
// Best-effort: returns an empty vector if the track or its waveform data
// can't be found -- a missing waveform should never block playback.
std::vector<double> readWaveformPreview(const std::string &engineLibraryPath, const std::string &trackSourceId);

}  // namespace djconvert::infrastructure::engine
