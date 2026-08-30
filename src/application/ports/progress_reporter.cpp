#include "application/ports/progress_reporter.hpp"

#include <iostream>

namespace seabass::application
{

NullProgressReporter &NullProgressReporter::instance()
{
    static NullProgressReporter reporter;
    return reporter;
}

void NullProgressReporter::warn(const std::string &message)
{
    std::cerr << "warning: " << message << "\n";
}

}  // namespace seabass::application
