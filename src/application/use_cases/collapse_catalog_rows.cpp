#include "application/use_cases/collapse_catalog_rows.hpp"

#include <map>

#include "application/path_key.hpp"
#include "domain/local_restore.hpp"

namespace seabass::application
{

namespace
{

void fillGapsFrom(domain::Track &into, const domain::Track &from)
{
    if (into.bitrate <= 0) {
        into.bitrate = from.bitrate;
    }
    if (into.durationSeconds <= 0.0) {
        into.durationSeconds = from.durationSeconds;
    }
    if (into.fileSizeBytes == 0) {
        into.fileSizeBytes = from.fileSizeBytes;
    }
    if (into.bpm <= 0.0) {
        into.bpm = from.bpm;
    }
    if (into.key.empty()) {
        into.key = from.key;
    }
    if (into.artworkPath.empty()) {
        into.artworkPath = from.artworkPath;
    }
    if (into.title.empty()) {
        into.title = from.title;
    }
    if (into.artist.empty()) {
        into.artist = from.artist;
    }
    if (!into.rating.has_value()) {
        into.rating = from.rating;
    }
    if (into.comment.empty()) {
        into.comment = from.comment;
    }
    if (!into.playCount.has_value()) {
        into.playCount = from.playCount;
    }
    if (!into.lastPlayedAt.has_value()) {
        into.lastPlayedAt = from.lastPlayedAt;
    }
    into.cues = domain::LocalRestorePlanner::mergeCues(into.cues, from.cues);
}

}  // namespace

std::vector<domain::Track> collapseCatalogRows(const std::vector<domain::Track> &rows)
{
    std::vector<domain::Track> files;
    files.reserve(rows.size());
    std::map<std::string, size_t> byPath;  // path key -> index into files

    for (const auto &row : rows) {
        const std::string key = normalizedPathKey(row.filePath);
        // No path, or a streaming row whose path names a cache on
        // another machine: nothing to establish sameness with, so it
        // stands alone rather than being folded into anything.
        if (key.empty() || !row.streamingSource.empty()) {
            files.push_back(row);
            continue;
        }

        auto [it, inserted] = byPath.emplace(key, files.size());
        if (inserted) {
            files.push_back(row);
            files.back().catalogRows.push_back({row.format, row.sourceId});
            continue;
        }
        domain::Track &existing = files[it->second];
        existing.catalogRows.push_back({row.format, row.sourceId});
        fillGapsFrom(existing, row);
    }

    return files;
}

}  // namespace seabass::application
