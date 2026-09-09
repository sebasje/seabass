#include "application/path_key.hpp"

#include <algorithm>
#include <filesystem>

namespace seabass::application
{

std::string normalizedPathKey(const std::string &path)
{
    if (path.empty()) {
        return path;
    }
    // export.pdb keeps its strings in fixed-length fields and space-pads
    // them, so a path read out of a rekordbox catalog arrives with
    // trailing spaces while the same path walked off the filesystem does
    // not. Trimmed here as well as at the reader, because this function
    // is the one place all three destructive comparisons agree on: if a
    // padded path ever reaches it from anywhere, the answer must still be
    // "same file" rather than "delete that". NULs too -- the format pads
    // some fields with those instead.
    std::string slashed = path;
    while (!slashed.empty()
           && (slashed.back() == ' ' || slashed.back() == '\t' || slashed.back() == '\0')) {
        slashed.pop_back();
    }
    if (slashed.empty()) {
        return {};
    }
    std::replace(slashed.begin(), slashed.end(), '\\', '/');
    std::string normalized = std::filesystem::path(slashed).lexically_normal().generic_string();
    for (auto &c : normalized) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return normalized;
}

}  // namespace seabass::application
