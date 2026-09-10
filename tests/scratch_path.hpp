#pragma once

#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace seabass::testing
{

// The temp directory this test process owns, and nobody else does.
//
// Every test in the tree builds its scratch path from
// fs::temp_directory_path() plus a name derived from the test, which is
// fine until two suites run on one machine at once -- which happens
// routinely here, because more than one working copy of Seabass is
// often being built and tested at the same time. Then both processes
// pick the same directory, one calls remove_all() on it while the other
// is walking it, and the second dies with
//
//     cannot increment recursive directory iterator: No such file or
//     directory
//
// which reads exactly like a bug in whatever was being tested. It cost
// two sessions an evening between them before anyone spotted that the
// failing test changed run to run and the common factor was the clock.
//
// The pid is enough: a scratch tree only has to outlive the process that
// made it. Tests keep their own descriptive names underneath, so a
// leftover directory still says which test left it.
//
// This does not cover $HOME. SEABASS_HOME (set per test by the top-level
// CMakeLists) already sandboxes that side.
inline std::filesystem::path scratchRoot()
{
#if defined(_WIN32)
    const auto pid = static_cast<long long>(_getpid());
#else
    const auto pid = static_cast<long long>(::getpid());
#endif
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("seabass-test-" + std::to_string(pid));
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    return root;
}

}  // namespace seabass::testing
