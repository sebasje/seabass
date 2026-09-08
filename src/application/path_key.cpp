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
    std::string slashed = path;
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
