#include "infrastructure/local/metadata_store.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>

#include "domain/track_matching.hpp"
#include "infrastructure/hashing/sha256.hpp"
#include "infrastructure/local/sqlite_statement.hpp"
#include "infrastructure/paths/seabass_paths.hpp"

namespace seabass::infrastructure::local
{

namespace fs = std::filesystem;
using domain::CuePoint;
using domain::PlaylistMembership;
using domain::Track;

namespace
{

constexpr const char *Context = "metadata store";
constexpr int SchemaVersion = 1;

// The shared RAII statement and exec() with this store's error-message
// context bound in, so call sites stay two arguments.
struct Stmt : Statement
{
    Stmt(sqlite3 *db, const char *sql) : Statement(db, sql, Context) {}
};

void exec(sqlite3 *db, const char *sql)
{
    local::exec(db, sql, Context);
}

// How a track is spelled for display once it is off the stick.
//
// Stick-relative, because an absolute path is a claim about a mount
// point that will not be there next time. Display only: it is not what
// tracks are matched on. Purely lexical -- lexically_relative rather
// than fs::relative -- so it never touches the filesystem and gives the
// same answer for a stick that is no longer plugged in.
std::string stickRelativePath(const std::string &filePath, const fs::path &stickRoot)
{
    if (filePath.empty()) {
        return {};
    }
    if (!stickRoot.empty()) {
        fs::path relative = fs::path(filePath).lexically_normal().lexically_relative(stickRoot.lexically_normal());
        const std::string text = relative.generic_string();
        // lexically_relative walks up with ".." when the path is not
        // under the root at all; such an answer says nothing about the
        // stick and must not become a key.
        if (!text.empty() && text != "." && text.rfind("..", 0) != 0) {
            return text;
        }
    }
    // A path we cannot place on the stick still has a filename, and a
    // filename is a weaker key rather than no key. Losing the track
    // entirely would be worse.
    return fs::path(filePath).filename().generic_string();
}

// What two rows have to agree on to be the same track: artist, title
// and length, the same rule domain::matchTracks() uses everywhere else
// in Seabass.
//
// Not the file path. A path is the strongest signal when both sides are
// looking at one stick, and the weakest thing to key a store on: the
// whole point of this store is that it outlives the stick, and a
// re-export renames folders, a rebuilt library moves Contents/ around,
// and the same track bought again lands somewhere else entirely.
// Artist and title travel with the recording.
//
// Length is not part of the key, because two readings of one file differ
// by rounding; it is a guard applied to the candidates the key finds,
// with the same tolerance and the same "only when both readings are
// real" rule matchTracks uses. A zero duration means unreadable, not a
// zero-length track, and gating on it would split one track into two
// rows the moment one catalog failed to report a length.
constexpr double DurationToleranceSeconds = 2.0;

std::string matchKeyFor(const Track &track)
{
    if (auto key = domain::titleArtistKey(track)) {
        return "ta:" + *key;
    }
    // Missing title or artist: fall back to the filename, exactly as
    // matchTracks does, rather than dropping the track.
    const std::string filename = domain::normalizeFilename(track.filename);
    return filename.empty() ? std::string() : "fn:" + filename;
}

bool durationsAgree(double a, double b)
{
    if (a <= 0.0 || b <= 0.0) {
        return true;
    }
    return std::abs(a - b) <= DurationToleranceSeconds;
}

std::string lowercased(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string kindToText(CuePoint::Kind kind)
{
    return kind == CuePoint::Kind::Hot ? "hot" : "memory";
}

CuePoint::Kind kindFromText(const std::string &text)
{
    return text == "hot" ? CuePoint::Kind::Hot : CuePoint::Kind::Memory;
}

std::string readWholeFile(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

// ---- construction ---------------------------------------------------

MetadataStore::MetadataStore(fs::path databasePath) : m_databasePath(std::move(databasePath))
{
    openAndMigrate();
}

MetadataStore::~MetadataStore()
{
    if (m_db) {
        sqlite3_close(m_db);
    }
}

fs::path MetadataStore::defaultDatabasePath()
{
    return paths::localMetadataDir() / "metadata.db";
}

fs::path MetadataStore::artworkDir() const
{
    return m_databasePath.parent_path() / "artwork";
}

// The version an existing database was written with, or 0 when it has
// none (no file, or one written before schema_version existed).
namespace
{

int schemaVersionOf(const fs::path &databasePath)
{
    std::error_code ec;
    if (!fs::exists(databasePath, ec)) {
        return 0;
    }
    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(databasePath.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;  // there is a file, and it is not a database we can read
    }
    int version = 0;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT version FROM schema_version", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            version = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return version;
}

}  // namespace

void MetadataStore::openAndMigrate()
{
    std::error_code ec;
    fs::create_directories(m_databasePath.parent_path(), ec);

    // A database from a different schema is moved aside rather than
    // migrated or deleted.
    //
    // Seabass is pre-1.0 and does not pay back-compatibility tax, so
    // there is no migration to write. But this store is the one place
    // that may hold cues no stick has any more, so deleting it is not
    // available either: "we changed the schema" is not a reason to lose
    // a DJ's work. Renaming costs nothing, leaves the file where a
    // person can find it, and CREATE TABLE IF NOT EXISTS then builds a
    // clean one -- which a single Back Up Now refills from the stick.
    const int existing = schemaVersionOf(m_databasePath);
    if (existing != 0 && existing != SchemaVersion) {
        // isoTimestampUtc()'s colons (the "T14:23:45Z" part) are fine as
        // a stored value -- every other call site uses it that way --
        // but not as part of a filename: NTFS reads a colon there as the
        // start of an Alternate Data Stream name, so fs::rename() failed
        // to find any such "file", set ec, and this correctly-but-
        // needlessly threw "could not move it aside" on every Windows
        // run that ever hit this path.
        std::string timestamp = isoTimestampUtc();
        std::replace(timestamp.begin(), timestamp.end(), ':', '-');
        fs::path superseded = m_databasePath;
        superseded += ".superseded-" + std::to_string(existing) + "-" + timestamp;
        fs::rename(m_databasePath, superseded, ec);
        if (ec) {
            throw std::runtime_error(std::string(Context) + ": found a database written by another version of "
                                      "Seabass and could not move it aside: " + ec.message());
        }
    }

    if (sqlite3_open(m_databasePath.string().c_str(), &m_db) != SQLITE_OK) {
        const std::string message = m_db ? sqlite3_errmsg(m_db) : "could not open database";
        sqlite3_close(m_db);
        m_db = nullptr;
        throw std::runtime_error(std::string(Context) + ": " + message);
    }

    // Two connections exist in the running app: the one the browse view
    // reads through and the one a backup writes through. A reader that
    // arrives mid-transaction should wait rather than fail the page.
    sqlite3_busy_timeout(m_db, 5000);
    exec(m_db, "PRAGMA foreign_keys = ON;");
    exec(m_db, R"sql(
        CREATE TABLE IF NOT EXISTS tracks (
            id INTEGER PRIMARY KEY,
            match_key TEXT NOT NULL,
            relative_path TEXT NOT NULL,
            filename TEXT NOT NULL,
            title TEXT NOT NULL DEFAULT '',
            artist TEXT NOT NULL DEFAULT '',
            duration_seconds REAL NOT NULL DEFAULT 0,
            bpm REAL NOT NULL DEFAULT 0,
            music_key TEXT NOT NULL DEFAULT '',
            rating INTEGER,
            comment TEXT NOT NULL DEFAULT '',
            play_count INTEGER,
            last_played_at TEXT NOT NULL DEFAULT '',
            artwork_sha TEXT NOT NULL DEFAULT '',
            artwork_extension TEXT NOT NULL DEFAULT '',
            library_id TEXT NOT NULL DEFAULT '',
            stick_label TEXT NOT NULL DEFAULT '',
            source_format TEXT NOT NULL DEFAULT '',
            first_seen TEXT NOT NULL,
            updated_at TEXT NOT NULL
        );
    )sql");
    exec(m_db, R"sql(
        CREATE TABLE IF NOT EXISTS cues (
            track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
            kind TEXT NOT NULL,
            hot_number INTEGER NOT NULL DEFAULT 0,
            position_ms REAL NOT NULL DEFAULT 0,
            color TEXT NOT NULL DEFAULT '',
            comment TEXT NOT NULL DEFAULT '',
            is_loop INTEGER NOT NULL DEFAULT 0,
            loop_end_ms REAL NOT NULL DEFAULT 0
        );
    )sql");
    exec(m_db, R"sql(
        CREATE TABLE IF NOT EXISTS playlists (
            track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
            name TEXT NOT NULL,
            position INTEGER NOT NULL DEFAULT -1
        );
    )sql");
    exec(m_db, "CREATE INDEX IF NOT EXISTS tracks_by_match_key ON tracks(match_key);");
    exec(m_db, "CREATE INDEX IF NOT EXISTS cues_by_track ON cues(track_id);");
    exec(m_db, "CREATE INDEX IF NOT EXISTS playlists_by_track ON playlists(track_id);");
    exec(m_db, "CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL);");

    Stmt version(m_db, "SELECT version FROM schema_version");
    if (!version.step()) {
        Stmt insert(m_db, "INSERT INTO schema_version (version) VALUES (?)");
        insert.bind(1, SchemaVersion);
        insert.run();
    }
}

// ---- artwork --------------------------------------------------------

namespace
{

struct ArtworkIntake
{
    std::string sha;
    std::string extension;
    bool copied = false;
    std::uint64_t bytes = 0;
};

// Copies a cover image next to the database under its own SHA-256, and
// answers with the hash the row should keep.
//
// Content-addressed rather than one copy per track: a library where 1500
// tracks share 300 album covers stores 300 files, and a second backup run
// over the same stick writes none of them again. The extension is kept
// alongside the hash so a viewer can be handed a path with a type on it;
// the hash alone would leave every image extensionless.
ArtworkIntake intakeArtwork(const std::string &sourcePath, const fs::path &artworkDir)
{
    ArtworkIntake intake;
    if (sourcePath.empty()) {
        return intake;
    }
    const std::string bytes = readWholeFile(sourcePath);
    if (bytes.empty()) {
        return intake;
    }

    intake.sha = hashing::toHex(hashing::Sha256::of(bytes));
    intake.extension = lowercased(fs::path(sourcePath).extension().generic_string());
    if (intake.extension.empty()) {
        intake.extension = ".jpg";
    }

    std::error_code ec;
    fs::create_directories(artworkDir, ec);
    const fs::path destination = artworkDir / (intake.sha + intake.extension);
    if (fs::exists(destination, ec)) {
        return intake;
    }

    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out) {
        // A cover we cannot copy is not a reason to lose the track's
        // cues. Drop the image, keep the row.
        intake.sha.clear();
        intake.extension.clear();
        return intake;
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        fs::remove(destination, ec);
        intake.sha.clear();
        intake.extension.clear();
        return intake;
    }

    intake.copied = true;
    intake.bytes = bytes.size();
    return intake;
}

}  // namespace

std::uint64_t MetadataStore::artworkBytesOnDisk() const
{
    std::error_code ec;
    std::uint64_t total = 0;
    for (const auto &entry : fs::directory_iterator(artworkDir(), ec)) {
        if (entry.is_regular_file(ec)) {
            total += entry.file_size(ec);
        }
    }
    return total;
}

// ---- writing --------------------------------------------------------

MetadataBackupSummary MetadataStore::store(const std::vector<Track> &tracks, const MetadataSource &source,
                                            ConflictPolicy policy, application::ProgressReporter &progress,
                                            const application::CancellationToken &cancel)
{
    MetadataBackupSummary summary;
    const std::string now = isoTimestampUtc();
    const fs::path artwork = artworkDir();

    progress.start("Storing metadata", tracks.size());
    exec(m_db, "BEGIN IMMEDIATE");
    // Any throw between here and COMMIT must not leave the database in a
    // transaction; a ROLLBACK on an already-finished transaction is
    // harmless, one that never runs is not.
    struct RollbackGuard
    {
        sqlite3 *db;
        bool committed = false;
        ~RollbackGuard()
        {
            if (!committed) {
                sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
            }
        }
    } guard{m_db};

    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if (cancel.cancelled()) {
            summary.cancelled = true;
            break;
        }
        progress.tick(i);

        const Track &track = tracks[i];
        summary.tracksSeen++;

        // A streaming link names no file on any stick -- its path
        // points into a cache on whatever computer manages playback --
        // so there is nothing a restore could ever put back on it.
        if (!track.streamingSource.empty()) {
            summary.tracksWithoutIdentity++;
            continue;
        }
        const std::string matchKey = matchKeyFor(track);
        if (matchKey.empty()) {
            // No artist and title, and no filename either. There is
            // nothing to recognise this row by later, so storing it
            // would only ever produce a row nothing can match.
            summary.tracksWithoutIdentity++;
            continue;
        }
        const std::string relativePath = stickRelativePath(track.filePath, source.stickRoot);

        std::int64_t id = 0;
        bool exists = false;
        std::optional<int> storedRating;
        std::optional<int> storedPlayCount;
        std::string storedComment;
        std::string storedArtworkSha;
        {
            // Every row this artist and title could mean, then the first
            // whose length agrees. Two rows under one key is the real
            // case this handles: a radio edit and an extended mix.
            Stmt find(m_db,
                      "SELECT id, rating, comment, play_count, artwork_sha, duration_seconds "
                      "FROM tracks WHERE match_key = ? ORDER BY id");
            find.bind(1, matchKey);
            while (find.step()) {
                if (!durationsAgree(find.columnDouble(5), track.durationSeconds)) {
                    continue;
                }
                exists = true;
                id = find.columnInt64(0);
                storedRating = find.columnOptionalInt(1);
                storedComment = find.columnText(2);
                storedPlayCount = find.columnOptionalInt(3);
                storedArtworkSha = find.columnText(4);
                break;
            }
        }

        std::vector<CuePoint> storedCues;
        if (exists) {
            storedCues = cuesFor(id);
        }

        // What the policy decides, per authored field group. A group the
        // destination does not have yet is filled in either way: that is
        // not a conflict, it is a blank.
        const bool cuesConflict = !storedCues.empty() && !track.cues.empty() &&
                                   !domain::cueSetsEqual(storedCues, track.cues);
        const bool ratingConflict = storedRating.has_value() && track.rating.has_value() &&
                                     *storedRating != *track.rating;
        const bool commentConflict = !storedComment.empty() && !track.comment.empty() &&
                                      storedComment != track.comment;
        const bool playCountConflict = storedPlayCount.has_value() && track.playCount.has_value() &&
                                        *storedPlayCount != *track.playCount;
        const bool keepStored = policy == ConflictPolicy::Skip;

        const bool writeCues = !track.cues.empty() && (!cuesConflict || !keepStored);
        const bool writeRating = track.rating.has_value() && (!ratingConflict || !keepStored);
        const bool writeComment = !track.comment.empty() && (!commentConflict || !keepStored);
        const bool writePlayCount = track.playCount.has_value() && (!playCountConflict || !keepStored);

        // Artwork is only fetched when the row has none: a cover is not
        // authored data, so a stored one is as good as an incoming one
        // and re-hashing every image on every run would make a second
        // backup cost as much as the first.
        ArtworkIntake artworkIntake;
        if (storedArtworkSha.empty() && !track.artworkPath.empty()) {
            artworkIntake = intakeArtwork(track.artworkPath, artwork);
            if (artworkIntake.copied) {
                summary.artworkFilesAdded++;
                summary.artworkBytesAdded += artworkIntake.bytes;
            }
        }

        std::string lastPlayed;
        if (track.lastPlayedAt) {
            const std::time_t asTime = std::chrono::system_clock::to_time_t(*track.lastPlayedAt);
            std::tm tm{};
#if defined(_WIN32)
            gmtime_s(&tm, &asTime);
#else
            gmtime_r(&asTime, &tm);
#endif
            char buffer[32];
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
            lastPlayed = buffer;
        }

        if (!exists) {
            Stmt insert(m_db, R"sql(
                INSERT INTO tracks (match_key, relative_path, filename, title, artist,
                                    duration_seconds, bpm, music_key, rating, comment,
                                    play_count, last_played_at, artwork_sha, artwork_extension,
                                    library_id, stick_label, source_format, first_seen, updated_at)
                VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
            )sql");
            insert.bind(1, matchKey);
            insert.bind(2, relativePath);
            insert.bind(3, track.filename);
            insert.bind(4, track.title);
            insert.bind(5, track.artist);
            insert.bind(6, track.durationSeconds);
            insert.bind(7, track.bpm);
            insert.bind(8, track.key);
            insert.bind(9, track.rating);
            insert.bind(10, track.comment);
            insert.bind(11, track.playCount);
            insert.bind(12, lastPlayed);
            insert.bind(13, artworkIntake.sha);
            insert.bind(14, artworkIntake.extension);
            insert.bind(15, source.libraryId);
            insert.bind(16, source.stickLabel);
            insert.bind(17, track.format);
            insert.bind(18, now);
            insert.bind(19, now);
            insert.run();
            id = sqlite3_last_insert_rowid(m_db);
            summary.tracksAdded++;
        }

        // Identity fields (title, artist, duration, bpm, key, where it
        // was last seen) always refresh: they are what the software
        // wrote, not what the DJ authored, and the freshest reading of
        // them is the best one.
        if (exists) {
            Stmt update(m_db, R"sql(
                UPDATE tracks SET match_key = ?, relative_path = ?, filename = ?, title = ?, artist = ?,
                                  duration_seconds = ?, bpm = ?, music_key = ?,
                                  library_id = ?, stick_label = ?, source_format = ?, updated_at = ?
                WHERE id = ?
            )sql");
            // match_key refreshes with the rest: a title corrected on
            // the stick has to be findable under the corrected spelling,
            // or the next backup files it as a second track.
            update.bind(1, matchKey);
            update.bind(2, relativePath);
            update.bind(3, track.filename);
            update.bind(4, track.title);
            update.bind(5, track.artist);
            update.bind(6, track.durationSeconds);
            update.bind(7, track.bpm);
            update.bind(8, track.key);
            update.bind(9, source.libraryId);
            update.bind(10, source.stickLabel);
            update.bind(11, track.format);
            update.bind(12, now);
            update.bindInt64(13, id);
            update.run();

            if (writeRating) {
                Stmt set(m_db, "UPDATE tracks SET rating = ? WHERE id = ?");
                set.bind(1, track.rating);
                set.bindInt64(2, id);
                set.run();
            }
            if (writeComment) {
                Stmt set(m_db, "UPDATE tracks SET comment = ? WHERE id = ?");
                set.bind(1, track.comment);
                set.bindInt64(2, id);
                set.run();
            }
            if (writePlayCount) {
                Stmt set(m_db, "UPDATE tracks SET play_count = ?, last_played_at = ? WHERE id = ?");
                set.bind(1, track.playCount);
                set.bind(2, lastPlayed);
                set.bindInt64(3, id);
                set.run();
            }
            if (!artworkIntake.sha.empty() && storedArtworkSha.empty()) {
                Stmt set(m_db, "UPDATE tracks SET artwork_sha = ?, artwork_extension = ? WHERE id = ?");
                set.bind(1, artworkIntake.sha);
                set.bind(2, artworkIntake.extension);
                set.bindInt64(3, id);
                set.run();
            }
        }

        if (writeCues) {
            Stmt clear(m_db, "DELETE FROM cues WHERE track_id = ?");
            clear.bindInt64(1, id);
            clear.run();
            Stmt insert(m_db, R"sql(
                INSERT INTO cues (track_id, kind, hot_number, position_ms, color, comment, is_loop, loop_end_ms)
                VALUES (?,?,?,?,?,?,?,?)
            )sql");
            for (const auto &cue : track.cues) {
                insert.reset();
                insert.bindInt64(1, id);
                insert.bind(2, kindToText(cue.kind));
                insert.bind(3, cue.hotCueNumber);
                insert.bind(4, cue.positionMs);
                insert.bind(5, cue.color);
                insert.bind(6, cue.comment);
                insert.bind(7, cue.isLoop ? 1 : 0);
                insert.bind(8, cue.loopEndMs);
                insert.run();
                summary.cuesStored++;
            }
        }

        // Playlist membership is authored but not conflicting: it is a
        // set, the freshest reading of it is the right one, and there is
        // no case where a stale membership is worth keeping over a
        // current one. Only replaced when the incoming reading has any,
        // so a catalog that does not report playlists cannot erase what
        // another one did.
        if (!track.playlists.empty()) {
            Stmt clear(m_db, "DELETE FROM playlists WHERE track_id = ?");
            clear.bindInt64(1, id);
            clear.run();
            Stmt insert(m_db, "INSERT INTO playlists (track_id, name, position) VALUES (?,?,?)");
            for (const auto &playlist : track.playlists) {
                insert.reset();
                insert.bindInt64(1, id);
                insert.bind(2, playlist.name);
                insert.bind(3, playlist.position);
                insert.run();
            }
        }

        if (exists) {
            // Mutually exclusive, so the four counts add up to the
            // tracks seen. A track that both took something new and kept
            // something old counts as updated: "skipped" reads as
            // "nothing of yours was replaced", and it has to stay true.
            const bool wroteSomething = writeCues || writeRating || writeComment || writePlayCount;
            const bool keptSomething = keepStored &&
                (cuesConflict || ratingConflict || commentConflict || playCountConflict);
            if (wroteSomething) {
                summary.tracksUpdated++;
            } else if (keptSomething) {
                summary.tracksSkipped++;
            } else {
                summary.tracksUnchanged++;
            }
        }
    }

    exec(m_db, "COMMIT");
    guard.committed = true;
    progress.finish();
    return summary;
}

// ---- reading --------------------------------------------------------

std::vector<CuePoint> MetadataStore::cuesFor(std::int64_t trackId)
{
    std::vector<CuePoint> cues;
    Stmt stmt(m_db,
              "SELECT kind, hot_number, position_ms, color, comment, is_loop, loop_end_ms "
              "FROM cues WHERE track_id = ? ORDER BY position_ms");
    stmt.bindInt64(1, trackId);
    while (stmt.step()) {
        CuePoint cue;
        cue.kind = kindFromText(stmt.columnText(0));
        cue.hotCueNumber = stmt.columnInt(1);
        cue.positionMs = stmt.columnDouble(2);
        cue.color = stmt.columnText(3);
        cue.comment = stmt.columnText(4);
        cue.isLoop = stmt.columnInt(5) != 0;
        cue.loopEndMs = stmt.columnDouble(6);
        cues.push_back(std::move(cue));
    }
    return cues;
}

std::vector<PlaylistMembership> MetadataStore::playlistsFor(std::int64_t trackId)
{
    std::vector<PlaylistMembership> playlists;
    Stmt stmt(m_db, "SELECT name, position FROM playlists WHERE track_id = ? ORDER BY name");
    stmt.bindInt64(1, trackId);
    while (stmt.step()) {
        PlaylistMembership membership;
        membership.name = stmt.columnText(0);
        membership.position = stmt.columnInt(1);
        playlists.push_back(std::move(membership));
    }
    return playlists;
}

std::vector<Track> MetadataStore::readAll()
{
    std::vector<Track> tracks;
    {
        Stmt stmt(m_db, R"sql(
            SELECT id, relative_path, filename, title, artist, duration_seconds, bpm,
                   music_key, rating, comment, play_count
            FROM tracks ORDER BY id
        )sql");
        while (stmt.step()) {
            Track track;
            track.sourceId = std::to_string(stmt.columnInt64(0));
            track.format = "metadata-store";
            // Stick-relative, deliberately: an absolute path from
            // whichever stick this row was last read off would be a claim
            // about a file that is not there. matchTracks() treats a
            // filePath that finds no twin as no signal and falls through
            // to title+artist, which is exactly right here.
            track.filePath = stmt.columnText(1);
            track.filename = stmt.columnText(2);
            track.title = stmt.columnText(3);
            track.artist = stmt.columnText(4);
            track.durationSeconds = stmt.columnDouble(5);
            track.bpm = stmt.columnDouble(6);
            track.key = stmt.columnText(7);
            track.rating = stmt.columnOptionalInt(8);
            track.comment = stmt.columnText(9);
            track.playCount = stmt.columnOptionalInt(10);
            tracks.push_back(std::move(track));
        }
    }
    for (auto &track : tracks) {
        track.cues = cuesFor(std::stoll(track.sourceId));
    }
    return tracks;
}

int MetadataStore::trackCount(const std::string &search)
{
    const std::string pattern = "%" + lowercased(search) + "%";
    Stmt stmt(m_db,
              "SELECT COUNT(*) FROM tracks WHERE ? = '' OR lower(title) LIKE ? "
              "OR lower(artist) LIKE ? OR lower(filename) LIKE ?");
    stmt.bind(1, search);
    stmt.bind(2, pattern);
    stmt.bind(3, pattern);
    stmt.bind(4, pattern);
    return stmt.step() ? stmt.columnInt(0) : 0;
}

std::vector<StoredTrack> MetadataStore::browse(const std::string &search, int limit, int offset)
{
    std::vector<StoredTrack> rows;
    const std::string pattern = "%" + lowercased(search) + "%";
    Stmt stmt(m_db, R"sql(
        SELECT t.id, t.relative_path, t.filename, t.title, t.artist, t.duration_seconds,
               t.bpm, t.music_key, t.rating, t.comment, t.play_count, t.last_played_at,
               t.artwork_sha, t.artwork_extension, t.stick_label, t.source_format, t.updated_at,
               (SELECT COUNT(*) FROM cues c WHERE c.track_id = t.id),
               (SELECT COUNT(*) FROM playlists p WHERE p.track_id = t.id)
        FROM tracks t
        WHERE ? = '' OR lower(t.title) LIKE ? OR lower(t.artist) LIKE ? OR lower(t.filename) LIKE ?
        ORDER BY lower(t.artist), lower(t.title), t.id
        LIMIT ? OFFSET ?
    )sql");
    stmt.bind(1, search);
    stmt.bind(2, pattern);
    stmt.bind(3, pattern);
    stmt.bind(4, pattern);
    stmt.bind(5, limit);
    stmt.bind(6, offset);
    while (stmt.step()) {
        StoredTrack row;
        row.id = stmt.columnInt64(0);
        row.relativePath = stmt.columnText(1);
        row.filename = stmt.columnText(2);
        row.title = stmt.columnText(3);
        row.artist = stmt.columnText(4);
        row.durationSeconds = stmt.columnDouble(5);
        row.bpm = stmt.columnDouble(6);
        row.key = stmt.columnText(7);
        row.rating = stmt.columnOptionalInt(8);
        row.comment = stmt.columnText(9);
        row.playCount = stmt.columnOptionalInt(10);
        row.lastPlayedAt = stmt.columnText(11);
        const std::string sha = stmt.columnText(12);
        if (!sha.empty()) {
            row.artworkPath = (artworkDir() / (sha + stmt.columnText(13))).string();
        }
        row.stickLabel = stmt.columnText(14);
        row.sourceFormat = stmt.columnText(15);
        row.updatedAt = stmt.columnText(16);
        row.cueCount = stmt.columnInt(17);
        row.playlistCount = stmt.columnInt(18);
        rows.push_back(std::move(row));
    }
    return rows;
}

}  // namespace seabass::infrastructure::local
