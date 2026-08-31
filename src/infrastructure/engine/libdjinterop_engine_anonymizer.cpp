#include "infrastructure/engine/libdjinterop_engine_anonymizer.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <vector>

#include <djinterop/djinterop.hpp>

#include "infrastructure/anonymization_placeholder.hpp"

namespace seabass::infrastructure::engine
{

namespace fs = std::filesystem;

namespace
{

std::string placeholder(const std::string &kind, size_t index)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%03zu", index);
    return kind + " " + buf;
}

// Removes `doomed` from `pl`'s track list if present (no survivor to
// repoint to -- unlike LibdjinteropEngineCleanupWriter's consolidation
// case, this is a plain deletion), then recurses into child playlists.
// Rebuilds via clear_tracks()/add_track_back() rather than
// playlist::remove_track(), for the same reason documented in
// libdjinterop_engine_cleanup_writer.cpp: that method deletes by
// PlaylistEntity id, not track id, in this vendored libdjinterop
// version.
void removeFromPlaylistTree(djinterop::playlist pl, const djinterop::track &doomed)
{
    auto tracks = pl.tracks();
    bool containsDoomed = std::any_of(tracks.begin(), tracks.end(),
                                       [&](const djinterop::track &t) { return t.id() == doomed.id(); });
    if (containsDoomed) {
        pl.clear_tracks();
        for (const auto &t : tracks) {
            if (t.id() != doomed.id()) {
                pl.add_track_back(t);
            }
        }
    }
    for (auto &child : pl.children()) {
        removeFromPlaylistTree(child, doomed);
    }
}

// Renames every playlist/folder in the tree rooted at `pl`, depth-first.
int renamePlaylistTree(djinterop::playlist pl, size_t &nextIndex)
{
    pl.set_name(placeholder("Playlist", nextIndex++));
    int renamed = 1;
    for (auto &child : pl.children()) {
        renamed += renamePlaylistTree(child, nextIndex);
    }
    return renamed;
}

void copyTreeIfPresent(const fs::path &from, const fs::path &to)
{
    std::error_code ec;
    if (!fs::exists(from, ec)) {
        return;
    }
    fs::copy(from, to, fs::copy_options::recursive, ec);
    if (ec) {
        throw std::runtime_error("failed to copy " + from.string() + " to " + to.string() + ": " + ec.message());
    }
}

}  // namespace

EngineAnonymizationResult anonymizeEngineLibrary(const std::string &sourceRoot, const std::string &destinationRoot,
                                                  std::optional<size_t> maxTracks, application::ProgressReporter &reporter)
{
    EngineAnonymizationResult result;

    std::error_code ec;
    if (fs::exists(destinationRoot, ec) && !fs::is_empty(destinationRoot, ec)) {
        result.errorMessage = destinationRoot + " already exists and isn't empty -- refusing to write into it";
        return result;
    }
    fs::create_directories(destinationRoot, ec);

    try {
        copyTreeIfPresent(fs::path(sourceRoot) / "Database2", fs::path(destinationRoot) / "Database2");

        if (!djinterop::engine::database_exists(destinationRoot)) {
            result.errorMessage = "no Engine Library found at " + sourceRoot;
            return result;
        }
        auto db = djinterop::engine::load_database(destinationRoot);

        std::vector<djinterop::track> allTracks = db.tracks();
        reporter.start("Anonymizing Engine library", allTracks.size());

        std::vector<djinterop::track> kept = allTracks;
        std::vector<djinterop::track> dropped;
        if (maxTracks && kept.size() > *maxTracks) {
            auto splitPoint = kept.begin() + static_cast<std::vector<djinterop::track>::difference_type>(*maxTracks);
            dropped.assign(splitPoint, kept.end());
            // erase(), not resize(): djinterop::track has no default
            // constructor (it wraps a pimpl handle), which resize()
            // would need for any growth-shaped operation.
            kept.erase(splitPoint, kept.end());
        }

        for (const auto &doomed : dropped) {
            for (auto &root : db.root_playlists()) {
                removeFromPlaylistTree(root, doomed);
            }
            db.remove_track(doomed);
        }

        // Unlike rekordbox's normalized artist table, Engine stores
        // artist as a plain string per track -- map each distinct real
        // artist name to one placeholder (rather than one placeholder
        // per track) so tracks that really do share an artist still
        // group together after anonymization, matching real-world
        // "browse by artist" structure.
        std::unordered_map<std::string, std::string> artistPlaceholderByRealName;
        size_t nextArtistIndex = 0;
        size_t nextCueLabelIndex = 0;

        size_t trackIndex = 0;
        for (auto &t : kept) {
            std::string realArtist = t.artist().value_or("");
            auto artistIt = artistPlaceholderByRealName.find(realArtist);
            if (artistIt == artistPlaceholderByRealName.end()) {
                artistIt = artistPlaceholderByRealName
                               .emplace(realArtist, placeholder("Artist", nextArtistIndex++))
                               .first;
            }

            // Filename keyed off the real filename via
            // anonymizationFilenamePlaceholder() (not a per-run
            // sequential index) so the same real track gets the same
            // obfuscated filename here and in the independently-run
            // rekordbox anonymizer -- see that function's own comment
            // for why that's what domain::TrackMatcher's cross-catalog
            // sync matching actually needs to keep working against
            // anonymized data.
            std::string realFilename = t.filename();
            std::string obfuscatedFilename = anonymizationFilenamePlaceholder(realFilename);
            t.set_title(anonymizationPlaceholder("Track", realFilename));
            t.set_artist(artistIt->second);
            t.set_comment(anonymizationPlaceholder("Comment", realFilename));
            t.set_relative_path("Contents/" + obfuscatedFilename);

            // Hot cue/loop *labels* are the Engine-side equivalent of
            // rekordbox's cue comments -- real free text a DJ typed per
            // cue point, not just structural position/color data. Only
            // the label changes; position()/loop bounds/color etc. stay
            // exactly as read back from each present entry.
            auto hotCues = t.hot_cues();
            for (auto &cue : hotCues) {
                if (cue) {
                    cue->label = placeholder("Cue", nextCueLabelIndex++);
                }
            }
            t.set_hot_cues(hotCues);

            auto loops = t.loops();
            for (auto &l : loops) {
                if (l) {
                    l->label = placeholder("Cue", nextCueLabelIndex++);
                }
            }
            t.set_loops(loops);

            ++trackIndex;
            reporter.tick(trackIndex);
        }

        size_t playlistIndex = 0;
        for (auto &root : db.root_playlists()) {
            result.playlistsRenamed += renamePlaylistTree(root, playlistIndex);
        }

        reporter.finish();
        result.tracksKept = static_cast<int>(kept.size());
        result.tracksDropped = static_cast<int>(dropped.size());
    } catch (const std::exception &e) {
        result.errorMessage = e.what();
    }
    return result;
}

}  // namespace seabass::infrastructure::engine
