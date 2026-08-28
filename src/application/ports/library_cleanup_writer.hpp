#pragma once

#include <string>

namespace djconvert::application
{

// Port for permanently removing a track -- including its membership in
// every playlist/crate -- from a library. This is the destructive half
// of the "Clean Up" duplicate-removal feature, so it's only implemented
// where a genuinely safe removal path exists: Engine, via libdjinterop's
// own real, tested database::remove_track() (which relies on the
// database's actual ON DELETE CASCADE foreign keys to clean up every
// playlist/crate reference -- not something this project reimplements).
// rekordbox has no implementation yet: PdbRowWriter implements the
// low-level export.pdb row-mutation primitives this would need, but
// nothing wires it to an actual removal decision yet.
class LibraryCleanupWriter
{
public:
    virtual ~LibraryCleanupWriter() = default;

    // Permanently removes the track with this id from the library.
    // Throws std::runtime_error if no such track exists.
    virtual void removeTrack(const std::string &trackSourceId) = 0;
};

}  // namespace djconvert::application
