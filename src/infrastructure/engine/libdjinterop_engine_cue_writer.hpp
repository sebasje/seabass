#pragma once

#include <string>

#include "application/ports/cue_writer.hpp"

namespace djconvert::infrastructure::engine
{

// Writes hot cues back into an existing Engine Library track using
// libdjinterop's track::set_hot_cues(). The caller (cli/) is responsible
// for backing up the library's files before constructing this -- this
// class only performs the write itself.
class LibdjinteropEngineCueWriter : public application::CueWriter
{
public:
    explicit LibdjinteropEngineCueWriter(std::string engineLibraryPath);

    void writeHotCues(const std::string &trackSourceId, const std::vector<domain::CuePoint> &cues) override;

private:
    std::string m_engineLibraryPath;
};

}  // namespace djconvert::infrastructure::engine
