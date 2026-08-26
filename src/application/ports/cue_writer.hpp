#pragma once

#include <string>
#include <vector>

#include "domain/track.hpp"

namespace djconvert::application
{

// Port for writing a track's hot cues back into a library. Only
// implemented where the underlying format's write path actually exists
// (Engine, via libdjinterop) -- rekordbox has no implementation yet, since
// its write path is still unbuilt (see the plan's Phasing/Risks).
class CueWriter
{
public:
    virtual ~CueWriter() = default;
    virtual void writeHotCues(const std::string &trackSourceId, const std::vector<domain::CuePoint> &cues) = 0;
};

}  // namespace djconvert::application
