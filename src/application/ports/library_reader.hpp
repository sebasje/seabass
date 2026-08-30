#pragma once

#include <vector>

#include "application/ports/progress_reporter.hpp"
#include "domain/track.hpp"

namespace seabass::application
{

// Port implemented by each format-specific infrastructure adapter
// (rekordbox, Engine). The application layer depends only on this
// abstraction, never on Kaitai, libdjinterop, or SQLite directly.
class LibraryReader
{
public:
    virtual ~LibraryReader() = default;
    virtual std::vector<domain::Track> readAll() = 0;

    void setProgressReporter(ProgressReporter &reporter) { m_progress = &reporter; }

protected:
    ProgressReporter *m_progress = &NullProgressReporter::instance();
};

}  // namespace seabass::application
