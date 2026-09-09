#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "application/ports/cancellation_token.hpp"
#include "application/ports/library_reader.hpp"
#include "application/ports/progress_reporter.hpp"
#include "domain/track.hpp"

struct sqlite3;

namespace seabass::infrastructure::local
{

// What to do when the stick and the store disagree about a field the DJ
// authored. One answer per run, applied to every conflict in it -- a
// per-track prompt over 1500 tracks is a decision nobody finishes.
//
// A field the destination does not have yet is never a conflict: filling
// in a blank is not overwriting, and both directions always fill blanks.
enum class ConflictPolicy {
    // Take the incoming value. The default when reading a stick into the
    // store: the stick is where the DJ works, so it is the newer truth.
    Overwrite,
    // Keep what is already stored. The default when writing back to a
    // stick, which may have been re-cued since the backup.
    Skip,
};

// Where a batch of tracks came from, so a stored row can say which stick
// it was last seen on.
struct MetadataSource
{
    std::filesystem::path stickRoot;  // to reduce absolute paths to stick-relative ones
    std::string libraryId;            // application::StickIdentity::libraryId()
    std::string stickLabel;           // for display only
};

struct MetadataBackupSummary
{
    int tracksSeen = 0;
    int tracksAdded = 0;
    int tracksUpdated = 0;
    // At least one authored field differed and the policy said keep the
    // stored one. Counted per track, not per field.
    int tracksSkipped = 0;
    // Already stored and nothing incoming was new.
    int tracksUnchanged = 0;
    // No resolvable file on this stick (an unresolved row, or a
    // streaming link), so nothing to key the row on. Not an error, and
    // counted separately so the totals still add up.
    int tracksWithoutFile = 0;
    int cuesStored = 0;
    int artworkFilesAdded = 0;
    std::uint64_t artworkBytesAdded = 0;
    bool cancelled = false;
};

// One row as the browse view needs it: enough to render a line without a
// second query, and no cues (those are fetched for the one row a user
// opens, not for the page they scroll past).
struct StoredTrack
{
    std::int64_t id = 0;
    std::string relativePath;
    std::string filename;
    std::string title;
    std::string artist;
    double durationSeconds = 0.0;
    double bpm = 0.0;
    std::string key;
    std::optional<int> rating;
    std::string comment;
    std::optional<int> playCount;
    std::string lastPlayedAt;
    // Absolute path to the copied cover image, empty when there is none.
    std::string artworkPath;
    std::string stickLabel;
    std::string sourceFormat;
    std::string updatedAt;
    int cueCount = 0;
    int playlistCount = 0;
};

// The DJ's own work, kept on this computer instead of only on a stick
// that gets reformatted. See docs/metadata-backup-plan.md for what is
// stored and why, and for the matching rules.
//
// Backed by SQLite, which libdjinterop already requires, so this adds no
// dependency. Cover art is not in the database: images are copied to
// artworkDir() under their own SHA-256, and the row keeps the hash. A
// library where 1500 tracks share 300 covers stores 300 files, and a
// re-run never writes an image twice.
//
// Implements LibraryReader so the store can be matched against a stick
// with the same domain::matchTracks() used for rekordbox<->Engine sync.
// One matcher, not two.
class MetadataStore : public application::LibraryReader
{
public:
    // Defaults to ~/Seabass/metadata/metadata.db, creating the schema on
    // first use. An explicit path is accepted for tests, and artwork then
    // lives in an "artwork" directory beside it.
    explicit MetadataStore(std::filesystem::path databasePath = defaultDatabasePath());
    ~MetadataStore() override;

    MetadataStore(const MetadataStore &) = delete;
    MetadataStore &operator=(const MetadataStore &) = delete;

    // Every stored track, as domain tracks, for matching. format is
    // "metadata-store" and sourceId is the row id as text, so a caller
    // can get back to the row a match came from.
    std::vector<domain::Track> readAll() override;

    // Reads `tracks` into the store. Only ever writes here, never to the
    // stick they came from.
    //
    // Tracks with no authored data at all (no cues, no rating, no
    // comment, no play count) are still stored: the browse view is a
    // record of what was on the stick, and a track with nothing on it
    // today may be worth annotating tomorrow. What is skipped is a track
    // with no resolvable file path, which has no key to be found again by.
    MetadataBackupSummary store(const std::vector<domain::Track> &tracks, const MetadataSource &source,
                                 ConflictPolicy policy, application::ProgressReporter &progress,
                                 const application::CancellationToken &cancel);

    // ---- browse -------------------------------------------------------
    // `search` matches title, artist or filename, case-insensitively;
    // empty matches everything. Ordered by artist then title. Paged in
    // SQL rather than in the caller, so showing twenty rows costs twenty
    // rows.
    std::vector<StoredTrack> browse(const std::string &search, int limit, int offset);
    int trackCount(const std::string &search = "");
    std::vector<domain::CuePoint> cuesFor(std::int64_t trackId);
    std::vector<domain::PlaylistMembership> playlistsFor(std::int64_t trackId);

    // Total bytes of copied cover art, for the page that has to say what
    // this feature costs on disk.
    std::uint64_t artworkBytesOnDisk() const;

    static std::filesystem::path defaultDatabasePath();
    // Where cover art copies live, derived from the database path.
    std::filesystem::path artworkDir() const;

private:
    void openAndMigrate();

    std::filesystem::path m_databasePath;
    sqlite3 *m_db = nullptr;
};

}  // namespace seabass::infrastructure::local
