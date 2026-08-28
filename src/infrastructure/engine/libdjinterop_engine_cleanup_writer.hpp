#pragma once

#include <string>

#include "application/ports/library_cleanup_writer.hpp"

namespace djconvert::infrastructure::engine
{

// Permanently removes a track from an Engine Library, using
// libdjinterop's real, tested database::remove_track() for the track
// row itself, plus an explicit walk of every playlist to move the
// doomed track's membership onto the survivor first --
// database::remove_track()'s own claim that playlist cleanup happens
// automatically via a foreign-key cascade did not hold up against a
// real test (see the .cpp for why). The caller (cli:: or gui::) is
// responsible for backing up the library's files before constructing
// this -- this class only performs the write itself.
class LibdjinteropEngineCleanupWriter : public application::LibraryCleanupWriter
{
public:
    explicit LibdjinteropEngineCleanupWriter(std::string engineLibraryPath);

    void removeTrackReplacingWith(const std::string &doomedTrackId, const std::string &survivorTrackId) override;

private:
    std::string m_engineLibraryPath;
};

}  // namespace djconvert::infrastructure::engine
