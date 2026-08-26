#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace djconvert::infrastructure::rekordbox
{

// One tagged section of an ANLZ file, kept as opaque raw bytes (fourcc +
// len_header + len_tag + body, i.e. exactly len_tag bytes). We only need
// to semantically understand the one section we're editing (PCO2, hot
// cues); every other section (waveforms, beatgrid, path, ...) is copied
// through untouched as bytes we never interpret -- the safest possible
// approach for a format we can't afford to silently corrupt.
struct AnlzRawSection
{
    uint32_t fourcc = 0;
    std::string rawBytes;
};

// A parsed-at-the-section-level ANLZ file: the fixed header (magic,
// len_header, len_file, and whatever padding fills out len_header) kept
// verbatim, plus the sequence of raw sections that follow it.
class AnlzFile
{
public:
    static AnlzFile readRaw(const std::string &path);

    // Recomputes len_file from the current header+sections size, then
    // writes the file out.
    void writeRaw(const std::string &path) const;

    std::string headerBytes;
    std::vector<AnlzRawSection> sections;
};

}  // namespace djconvert::infrastructure::rekordbox
