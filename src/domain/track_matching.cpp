#include "domain/track_matching.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace djconvert::domain
{

namespace
{

constexpr double PositionToleranceMs = 1000.0;
constexpr double DurationToleranceSeconds = 2.0;

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

std::vector<std::pair<const Track *, const Track *>> matchTracks(const std::vector<Track> &a,
                                                                   const std::vector<Track> &b)
{
    std::map<std::string, std::vector<const Track *>> bByTitleArtist;
    std::map<std::string, std::vector<const Track *>> bByFilename;
    for (const auto &track : b) {
        if (auto key = titleArtistKey(track)) {
            bByTitleArtist[*key].push_back(&track);
        }
        bByFilename[normalizeFilename(track.filename)].push_back(&track);
    }

    std::vector<std::pair<const Track *, const Track *>> matches;
    for (const auto &trackA : a) {
        const std::vector<const Track *> *candidates = nullptr;

        if (auto key = titleArtistKey(trackA)) {
            auto it = bByTitleArtist.find(*key);
            if (it != bByTitleArtist.end()) {
                candidates = &it->second;
            }
        }
        if (!candidates) {
            auto it = bByFilename.find(normalizeFilename(trackA.filename));
            if (it != bByFilename.end()) {
                candidates = &it->second;
            }
        }
        if (!candidates) {
            continue;
        }

        for (const auto *trackB : *candidates) {
            if (std::abs(trackA.durationSeconds - trackB->durationSeconds) <= DurationToleranceSeconds) {
                matches.emplace_back(&trackA, trackB);
                break;  // one match per `a` track is enough for propagating cues
            }
        }
    }
    return matches;
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
