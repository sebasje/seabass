#pragma once

#include <optional>
#include <string>

#include "application/ports/cue_writer.hpp"

namespace seabass::infrastructure::engine
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

    // Fills in a Clean Up survivor's missing bpm/key from another copy
    // in its duplicate group (see domain::DuplicateCleanupPlan). Only
    // bpm and key -- Engine track artwork isn't writable through
    // libdjinterop today (djinterop::track has no set_album_art()/
    // equivalent at all, and djinterop::album_art's own header is
    // explicitly marked "TODO - implement rest of album_art class"), so
    // artwork propagation isn't offered for this format. Either
    // optional being unset just skips that field; throws if the track
    // doesn't exist.
    void propagateMissingFields(const std::string &trackSourceId, std::optional<double> bpm,
                                 std::optional<std::string> key);

private:
    std::string m_engineLibraryPath;
};

}  // namespace seabass::infrastructure::engine
