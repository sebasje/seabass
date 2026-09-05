#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace seabass::infrastructure::engine
{

// The domain-level "did the restore work": opens the restored Engine
// database with the same reader the rest of the app uses and lists every
// local (non-streaming) track whose file is not there. nullopt when the
// target has no Engine database. Plugs into
// application::RestoreOptions::libraryCheck.
std::optional<std::vector<std::string>> checkRestoredEngineLibrary(const std::filesystem::path &targetRoot);

}  // namespace seabass::infrastructure::engine
