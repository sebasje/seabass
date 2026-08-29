#pragma once

#include <string>
#include <vector>

#include "domain/track.hpp"

namespace djconvert::application
{

// Port for writing a track's hot cues back into a library. Implemented for
// both rekordbox (RekordboxCueWriter, via the ANLZ PCO2 sections) and
// Engine (LibdjinteropEngineCueWriter, via libdjinterop).
class CueWriter
{
public:
    virtual ~CueWriter() = default;
    virtual void writeHotCues(const std::string &trackSourceId, const std::vector<domain::CuePoint> &cues) = 0;
};

}  // namespace djconvert::application
