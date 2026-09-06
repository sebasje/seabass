#pragma once

#include <filesystem>

namespace seabass::infrastructure::local
{

// Where this app keeps per-user, per-machine data that is not a setting:
// the local cue store, the benchmark history, the edit-lock cookies.
// Linux: $XDG_DATA_HOME/seabass (else ~/.local/share/seabass); Windows:
// %LOCALAPPDATA%\seabass. Not created here -- callers create what they
// need under it.
std::filesystem::path appDataDirectory();

}  // namespace seabass::infrastructure::local
