#pragma once

#include <vector>

#include "application/ports/library_reader.hpp"
#include "domain/track.hpp"

namespace djconvert::application
{

// Read-only use case: return every track (with cues) found in a library.
// Works against any LibraryReader adapter, so it's agnostic to whether the
// source is a rekordbox USB export or an Engine Library.
class ScanLibrary
{
public:
    explicit ScanLibrary(LibraryReader &reader) : m_reader(reader) {}

    std::vector<domain::Track> execute() { return m_reader.readAll(); }

private:
    LibraryReader &m_reader;
};

}  // namespace djconvert::application
