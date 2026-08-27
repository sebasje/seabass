#include "domain/track_matching.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace djconvert::domain
{

namespace
{

constexpr double PositionToleranceMs = 1000.0;

std::vector<CuePoint> sortedCues(std::vector<CuePoint> cues)
{
    std::sort(cues.begin(), cues.end(), [](const CuePoint &a, const CuePoint &b) {
        if (a.kind != b.kind) {
            return a.kind < b.kind;
        }
        if (a.hotCueNumber != b.hotCueNumber) {
            return a.hotCueNumber < b.hotCueNumber;
        }
        return a.positionMs < b.positionMs;
    });
    return cues;
}

}  // namespace

std::string normalizeFilename(const std::string &filename)
{
    std::string result;
    result.reserve(filename.size());
    for (unsigned char c : filename) {
        if (!std::isspace(c)) {
            result.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return result;
}

std::optional<std::string> titleArtistKey(const Track &track)
{
    if (track.title.empty() || track.artist.empty()) {
        return std::nullopt;
    }
    return normalizeFilename(track.title + "|" + track.artist);
}

bool cueSetsEqual(const std::vector<CuePoint> &a, const std::vector<CuePoint> &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    auto sortedA = sortedCues(a);
    auto sortedB = sortedCues(b);
    for (size_t i = 0; i < sortedA.size(); ++i) {
        const auto &x = sortedA[i];
        const auto &y = sortedB[i];
        if (x.kind != y.kind || x.hotCueNumber != y.hotCueNumber || x.color != y.color || x.comment != y.comment) {
            return false;
        }
        if (std::abs(x.positionMs - y.positionMs) > PositionToleranceMs) {
            return false;
        }
    }
    return true;
}

}  // namespace djconvert::domain
