#pragma once

#include <string>
#include <vector>

namespace djconvert::infrastructure::rekordbox
{

// Reads the low-resolution monochrome waveform preview (the "PWAV" tag,
// which lives in the track's .DAT file, not the .EXT sibling used for
// cues) for a single track, normalized to 0..1 amplitude values (~400
// points, one per column). Best-effort: returns an empty vector if the
// track, its analysis file, or the tag itself can't be found -- a missing
// waveform should never block playback.
//
// Byte layout (documented at
// https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/anlz.html):
// each byte packs a 5-bit height (0-31) in the low bits and a 3-bit
// "whiteness" in the high bits; only height is used here.
std::vector<double> readWaveformPreview(const std::string &pioneerRoot, const std::string &trackSourceId);

}  // namespace djconvert::infrastructure::rekordbox
