#pragma once

#include <string>
#include <vector>

#include "application/ports/library_reader.hpp"
#include "domain/track.hpp"

namespace seabass::infrastructure::engine
{

// Reads an Engine Library (Database2/m.db etc.) using the vendored
// libdjinterop. This is the only place that knows about libdjinterop or the
// on-disk Engine schema.
class LibdjinteropEngineReader : public application::LibraryReader
{
public:
    // engineLibraryPath is the directory containing "Database2/"
    // (i.e. the "Engine Library" folder itself).
    explicit LibdjinteropEngineReader(std::string engineLibraryPath);

    std::vector<domain::Track> readAll() override;

private:
    std::string m_engineLibraryPath;
};

}  // namespace seabass::infrastructure::engine
