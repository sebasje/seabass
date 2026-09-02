#include "infrastructure/local/local_cue_store.hpp"

#include <sqlite3.h>

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>

#include "domain/track_matching.hpp"
#include "infrastructure/compression/zlib_compressor.hpp"

namespace seabass::infrastructure::local
{

namespace fs = std::filesystem;
using domain::CuePoint;
using domain::Track;

namespace
{

constexpr double DurationToleranceSeconds = 2.0;

std::string isoTimestampUtc()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    // gmtime_s takes its arguments in the opposite order from POSIX's
    // gmtime_r (destination first) and returns errno_t rather than a
    // struct tm* -- not just a rename.
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// Thin RAII wrapper so a thrown exception (or an early return) never
// leaks a prepared statement.
class Statement
{
public:
    Statement(sqlite3 *db, const char *sql) : m_db(db)
    {
        if (sqlite3_prepare_v2(db, sql, -1, &m_stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string("local cue store: failed to prepare statement: ") +
                                      sqlite3_errmsg(db));
        }
    }
    ~Statement() { sqlite3_finalize(m_stmt); }

    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;

    void bind(int index, const std::string &value)
    {
        sqlite3_bind_text(m_stmt, index, value.c_str(), -1, SQLITE_TRANSIENT);
    }
    void bind(int index, double value) { sqlite3_bind_double(m_stmt, index, value); }
    void bind(int index, int value) { sqlite3_bind_int(m_stmt, index, value); }
    void bindInt64(int index, sqlite3_int64 value) { sqlite3_bind_int64(m_stmt, index, value); }
    void bindBlob(int index, const std::string &value)
    {
        sqlite3_bind_blob(m_stmt, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    }

    // Runs to completion; throws if the statement reports an error.
    void run()
    {
        int rc = sqlite3_step(m_stmt);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            throw std::runtime_error(std::string("local cue store: statement failed: ") + sqlite3_errmsg(m_db));
        }
    }

    // For SELECTs: advances to the next row, returning false once
    // exhausted.
    bool step()
    {
        int rc = sqlite3_step(m_stmt);
        if (rc == SQLITE_ROW) {
            return true;
        }
        if (rc == SQLITE_DONE) {
            return false;
        }
        throw std::runtime_error(std::string("local cue store: statement failed: ") + sqlite3_errmsg(m_db));
    }

    std::string columnText(int index)
    {
        const unsigned char *text = sqlite3_column_text(m_stmt, index);
        return text ? reinterpret_cast<const char *>(text) : "";
    }
    double columnDouble(int index) { return sqlite3_column_double(m_stmt, index); }
    int columnInt(int index) { return sqlite3_column_int(m_stmt, index); }
    sqlite3_int64 columnInt64(int index) { return sqlite3_column_int64(m_stmt, index); }
    std::string columnBlob(int index)
    {
        const void *data = sqlite3_column_blob(m_stmt, index);
        int size = sqlite3_column_bytes(m_stmt, index);
        return data ? std::string(reinterpret_cast<const char *>(data), static_cast<size_t>(size)) : std::string();
    }

    sqlite3_int64 lastInsertRowId() { return sqlite3_last_insert_rowid(m_db); }

private:
    sqlite3 *m_db;
    sqlite3_stmt *m_stmt = nullptr;
};

void exec(sqlite3 *db, const char *sql)
{
    char *errMsg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::string message = errMsg ? errMsg : "unknown error";
        sqlite3_free(errMsg);
        throw std::runtime_error("local cue store: " + message);
    }
}

// A schema migration for a database that already had backup_sessions
// before schema_version existed (this feature's own first release, before
// the column was added) -- CREATE TABLE IF NOT EXISTS never adds a column
// to an existing table, only ALTER TABLE does, and SQLite has no "ADD
// COLUMN IF NOT EXISTS." A duplicate-column error means it's already
// there, which is expected on every run after the first and not a
// failure.
void addColumnIfMissing(sqlite3 *db, const char *alterSql)
{
    char *errMsg = nullptr;
    if (sqlite3_exec(db, alterSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::string message = errMsg ? errMsg : "unknown error";
        sqlite3_free(errMsg);
        if (message.find("duplicate column name") == std::string::npos) {
            throw std::runtime_error("local cue store: " + message);
        }
    }
}

// A private, own-format serialization for snapshot blobs -- there's no
// need for a general interchange format (JSON etc.) since nothing outside
// this file ever reads it, so a tiny escaped-tab-separated scheme avoids
// vendoring a JSON library for one job. Backslash-escapes tab/newline/
// carriage-return/backslash itself, so free-text fields (title, artist,
// comment) can never corrupt the line structure.
//
// Every snapshot stores the format version it was written with (see the
// backup_sessions.schema_version column) alongside the blob, and
// deserializeTracks() dispatches on it. This is the actual backwards-
// compatibility guarantee: if this format ever needs to change, the
// version-1 parser below stays exactly as it is forever, a new
// version-N branch gets added alongside it, and every snapshot written
// under the old version keeps reading back correctly no matter how old.
// Never repurpose an existing version number for a changed format.
constexpr int CurrentSnapshotFormatVersion = 2;

std::string escapeField(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        default:
            out += c;
        }
    }
    return out;
}

std::string unescapeField(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            char next = value[++i];
            switch (next) {
            case 't':
                out += '\t';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            default:
                out += next;  // handles '\\' -> '\' too
            }
        } else {
            out += value[i];
        }
    }
    return out;
}

// Format version 1. If this ever needs to change, add serializeTracksV2 /
// deserializeTracksV2 alongside it (see CurrentSnapshotFormatVersion's
// comment above) rather than editing this one.
std::string serializeTracksV1(const std::vector<Track> &tracks)
{
    std::ostringstream out;
    for (const auto &track : tracks) {
        out << "T\t" << escapeField(track.sourceId) << '\t' << escapeField(track.filename) << '\t'
            << escapeField(track.title) << '\t' << escapeField(track.artist) << '\t' << track.durationSeconds
            << '\n';
        for (const auto &cue : track.cues) {
            out << "C\t" << (cue.kind == CuePoint::Kind::Hot ? "hot" : "memory") << '\t' << cue.hotCueNumber << '\t'
                << cue.positionMs << '\t' << escapeField(cue.color) << '\t' << escapeField(cue.comment) << '\n';
        }
    }
    return out.str();
}

std::vector<std::string> splitFields(const std::string &line)
{
    std::vector<std::string> fields;
    size_t start = 0;
    for (size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == '\t') {
            fields.push_back(line.substr(start, i - start));
            start = i + 1;
        }
    }
    return fields;
}

std::vector<Track> deserializeTracksV1(const std::string &data)
{
    std::vector<Track> tracks;
    std::istringstream in(data);
    std::string line;
    while (std::getline(in, line)) {
        auto fields = splitFields(line);
        if (fields.empty()) {
            continue;
        }
        if (fields[0] == "T" && fields.size() >= 6) {
            Track track;
            track.sourceId = unescapeField(fields[1]);
            track.filename = unescapeField(fields[2]);
            track.title = unescapeField(fields[3]);
            track.artist = unescapeField(fields[4]);
            track.durationSeconds = std::stod(fields[5]);
            tracks.push_back(std::move(track));
        } else if (fields[0] == "C" && fields.size() >= 6 && !tracks.empty()) {
            CuePoint cue;
            cue.kind = fields[1] == "hot" ? CuePoint::Kind::Hot : CuePoint::Kind::Memory;
            cue.hotCueNumber = std::stoi(fields[2]);
            cue.positionMs = std::stod(fields[3]);
            cue.color = unescapeField(fields[4]);
            cue.comment = unescapeField(fields[5]);
            tracks.back().cues.push_back(std::move(cue));
        }
    }
    return tracks;
}

// Format version 2: adds isLoop/loopEndMs after V1's fields (never
// inserted in between -- see splitFields' size >= N checks below, which
// let a V2 line be read by nothing but the V2 parser while staying an
// obviously additive change). V1 snapshots have no loop data to lose:
// nothing wrote loop cues into this store before CuePoint gained the
// concept, so there's no migration to perform, only a new parser to add.
std::string serializeTracksV2(const std::vector<Track> &tracks)
{
    std::ostringstream out;
    for (const auto &track : tracks) {
        out << "T\t" << escapeField(track.sourceId) << '\t' << escapeField(track.filename) << '\t'
            << escapeField(track.title) << '\t' << escapeField(track.artist) << '\t' << track.durationSeconds
            << '\n';
        for (const auto &cue : track.cues) {
            out << "C\t" << (cue.kind == CuePoint::Kind::Hot ? "hot" : "memory") << '\t' << cue.hotCueNumber << '\t'
                << cue.positionMs << '\t' << escapeField(cue.color) << '\t' << escapeField(cue.comment) << '\t'
                << (cue.isLoop ? 1 : 0) << '\t' << cue.loopEndMs << '\n';
        }
    }
    return out.str();
}

std::vector<Track> deserializeTracksV2(const std::string &data)
{
    std::vector<Track> tracks;
    std::istringstream in(data);
    std::string line;
    while (std::getline(in, line)) {
        auto fields = splitFields(line);
        if (fields.empty()) {
            continue;
        }
        if (fields[0] == "T" && fields.size() >= 6) {
            Track track;
            track.sourceId = unescapeField(fields[1]);
            track.filename = unescapeField(fields[2]);
            track.title = unescapeField(fields[3]);
            track.artist = unescapeField(fields[4]);
            track.durationSeconds = std::stod(fields[5]);
            tracks.push_back(std::move(track));
        } else if (fields[0] == "C" && fields.size() >= 8 && !tracks.empty()) {
            CuePoint cue;
            cue.kind = fields[1] == "hot" ? CuePoint::Kind::Hot : CuePoint::Kind::Memory;
            cue.hotCueNumber = std::stoi(fields[2]);
            cue.positionMs = std::stod(fields[3]);
            cue.color = unescapeField(fields[4]);
            cue.comment = unescapeField(fields[5]);
            cue.isLoop = fields[6] == "1";
            cue.loopEndMs = std::stod(fields[7]);
            tracks.back().cues.push_back(std::move(cue));
        }
    }
    return tracks;
}

// Dispatches to the parser for whichever version the snapshot was actually
// written with -- see CurrentSnapshotFormatVersion's comment: every past
// version's parser stays available here forever, so a snapshot backed up
// years ago on an older Seabass always reads back correctly.
std::vector<Track> deserializeTracks(const std::string &data, int formatVersion)
{
    switch (formatVersion) {
    case 1:
        return deserializeTracksV1(data);
    case 2:
        return deserializeTracksV2(data);
    default:
        throw std::runtime_error("local cue store: snapshot was written with format version " +
                                  std::to_string(formatVersion) +
                                  ", which this version of Seabass doesn't know how to read "
                                  "(a downgrade?) -- try a newer version of the app");
    }
}

}  // namespace

std::string LocalCueStore::defaultPath()
{
#if defined(_WIN32)
    // Windows has no XDG_DATA_HOME/HOME -- LOCALAPPDATA is the equivalent
    // convention for per-user app data that shouldn't roam. Falling
    // through to USERPROFILE if it's ever unset (Windows always sets it
    // for real user sessions) rather than crashing.
    const char *localAppData = std::getenv("LOCALAPPDATA");
    fs::path dataDir = (localAppData && *localAppData) ? fs::path(localAppData)
                                                        : fs::path(std::getenv("USERPROFILE")) / "AppData" / "Local";
#else
    const char *xdgDataHome = std::getenv("XDG_DATA_HOME");
    fs::path dataDir = (xdgDataHome && *xdgDataHome) ? fs::path(xdgDataHome)
                                                      : fs::path(std::getenv("HOME")) / ".local" / "share";
#endif
    return (dataDir / "seabass" / "cues.db").string();
}

LocalCueStore::LocalCueStore(std::string path)
{
    fs::create_directories(fs::path(path).parent_path());

    if (sqlite3_open(path.c_str(), &m_db) != SQLITE_OK) {
        std::string message = m_db ? sqlite3_errmsg(m_db) : "failed to open database";
        sqlite3_close(m_db);
        throw std::runtime_error("local cue store: " + message);
    }

    exec(m_db, "PRAGMA foreign_keys = ON;");
    exec(m_db, R"sql(
        CREATE TABLE IF NOT EXISTS tracks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            filename_normalized TEXT NOT NULL,
            filename TEXT NOT NULL,
            title TEXT NOT NULL DEFAULT '',
            artist TEXT NOT NULL DEFAULT '',
            title_artist_key TEXT,
            duration_seconds REAL NOT NULL DEFAULT 0,
            source_format TEXT NOT NULL,
            source_label TEXT NOT NULL,
            backed_up_at TEXT NOT NULL
        );
    )sql");
    exec(m_db, R"sql(
        CREATE TABLE IF NOT EXISTS cues (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
            kind TEXT NOT NULL,
            hot_cue_number INTEGER NOT NULL DEFAULT 0,
            position_ms REAL NOT NULL,
            color TEXT NOT NULL DEFAULT '',
            comment TEXT NOT NULL DEFAULT ''
        );
    )sql");
    exec(m_db, R"sql(
        CREATE TABLE IF NOT EXISTS backup_sessions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            created_at TEXT NOT NULL,
            stick_label TEXT NOT NULL,
            source_format TEXT NOT NULL,
            description TEXT NOT NULL DEFAULT '',
            track_count INTEGER NOT NULL,
            cue_count INTEGER NOT NULL,
            uncompressed_size_bytes INTEGER NOT NULL,
            compressed_size_bytes INTEGER NOT NULL,
            data BLOB NOT NULL
        );
    )sql");
    addColumnIfMissing(m_db, "ALTER TABLE backup_sessions ADD COLUMN schema_version INTEGER NOT NULL DEFAULT 1");
    addColumnIfMissing(m_db, "ALTER TABLE cues ADD COLUMN is_loop INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing(m_db, "ALTER TABLE cues ADD COLUMN loop_end_ms REAL NOT NULL DEFAULT 0");
}

LocalCueStore::~LocalCueStore()
{
    sqlite3_close(m_db);
}

std::vector<Track> LocalCueStore::readAll()
{
    std::vector<Track> tracks;

    Statement trackStmt(m_db,
                         "SELECT id, filename, title, artist, duration_seconds FROM tracks ORDER BY id");
    while (trackStmt.step()) {
        Track track;
        sqlite3_int64 id = trackStmt.columnInt64(0);
        track.sourceId = std::to_string(id);
        track.filename = trackStmt.columnText(1);
        track.title = trackStmt.columnText(2);
        track.artist = trackStmt.columnText(3);
        track.durationSeconds = trackStmt.columnDouble(4);

        Statement cueStmt(m_db,
                           "SELECT kind, hot_cue_number, position_ms, color, comment, is_loop, loop_end_ms "
                           "FROM cues WHERE track_id = ?");
        cueStmt.bindInt64(1, id);
        while (cueStmt.step()) {
            CuePoint cue;
            cue.kind = cueStmt.columnText(0) == "hot" ? CuePoint::Kind::Hot : CuePoint::Kind::Memory;
            cue.hotCueNumber = cueStmt.columnInt(1);
            cue.positionMs = cueStmt.columnDouble(2);
            cue.color = cueStmt.columnText(3);
            cue.comment = cueStmt.columnText(4);
            cue.isLoop = cueStmt.columnInt(5) != 0;
            cue.loopEndMs = cueStmt.columnDouble(6);
            track.cues.push_back(std::move(cue));
        }

        tracks.push_back(std::move(track));
    }

    return tracks;
}

void LocalCueStore::upsert(const std::vector<Track> &tracks, const std::string &sourceFormat,
                            const std::string &sourceLabel)
{
    std::string backedUpAt = isoTimestampUtc();

    for (const auto &track : tracks) {
        if (track.cues.empty()) {
            continue;
        }

        std::optional<sqlite3_int64> existingId;
        auto key = domain::titleArtistKey(track);
        if (key) {
            Statement find(m_db, "SELECT id, duration_seconds FROM tracks WHERE title_artist_key = ?");
            find.bind(1, *key);
            while (find.step()) {
                if (std::abs(find.columnDouble(1) - track.durationSeconds) <= DurationToleranceSeconds) {
                    existingId = find.columnInt64(0);
                    break;
                }
            }
        }
        if (!existingId) {
            Statement find(m_db, "SELECT id, duration_seconds FROM tracks WHERE filename_normalized = ?");
            find.bind(1, domain::normalizeFilename(track.filename));
            while (find.step()) {
                if (std::abs(find.columnDouble(1) - track.durationSeconds) <= DurationToleranceSeconds) {
                    existingId = find.columnInt64(0);
                    break;
                }
            }
        }

        sqlite3_int64 trackId;
        if (existingId) {
            trackId = *existingId;
            Statement update(m_db, R"sql(
                UPDATE tracks SET filename_normalized = ?, filename = ?, title = ?, artist = ?,
                    title_artist_key = ?, duration_seconds = ?, source_format = ?, source_label = ?,
                    backed_up_at = ?
                WHERE id = ?
            )sql");
            update.bind(1, domain::normalizeFilename(track.filename));
            update.bind(2, track.filename);
            update.bind(3, track.title);
            update.bind(4, track.artist);
            if (key) {
                update.bind(5, *key);
            }
            update.bind(6, track.durationSeconds);
            update.bind(7, sourceFormat);
            update.bind(8, sourceLabel);
            update.bind(9, backedUpAt);
            update.bindInt64(10, trackId);
            update.run();

            Statement deleteCues(m_db, "DELETE FROM cues WHERE track_id = ?");
            deleteCues.bindInt64(1, trackId);
            deleteCues.run();
        } else {
            Statement insert(m_db, R"sql(
                INSERT INTO tracks (filename_normalized, filename, title, artist, title_artist_key,
                    duration_seconds, source_format, source_label, backed_up_at)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            )sql");
            insert.bind(1, domain::normalizeFilename(track.filename));
            insert.bind(2, track.filename);
            insert.bind(3, track.title);
            insert.bind(4, track.artist);
            if (key) {
                insert.bind(5, *key);
            }
            insert.bind(6, track.durationSeconds);
            insert.bind(7, sourceFormat);
            insert.bind(8, sourceLabel);
            insert.bind(9, backedUpAt);
            insert.run();
            trackId = insert.lastInsertRowId();
        }

        for (const auto &cue : track.cues) {
            Statement insertCue(m_db, R"sql(
                INSERT INTO cues (track_id, kind, hot_cue_number, position_ms, color, comment, is_loop, loop_end_ms)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            )sql");
            insertCue.bindInt64(1, trackId);
            insertCue.bind(2, cue.kind == CuePoint::Kind::Hot ? "hot" : "memory");
            insertCue.bind(3, cue.hotCueNumber);
            insertCue.bind(4, cue.positionMs);
            insertCue.bind(5, cue.color);
            insertCue.bind(6, cue.comment);
            insertCue.bind(7, cue.isLoop ? 1 : 0);
            insertCue.bind(8, cue.loopEndMs);
            insertCue.run();
        }
    }
}

std::int64_t LocalCueStore::createSnapshot(const std::vector<Track> &tracks, const std::string &sourceFormat,
                                            const std::string &stickLabel, const std::string &description)
{
    std::vector<Track> withCues;
    int cueCount = 0;
    for (const auto &track : tracks) {
        if (track.cues.empty()) {
            continue;
        }
        withCues.push_back(track);
        cueCount += static_cast<int>(track.cues.size());
    }

    std::string serialized = serializeTracksV2(withCues);
    std::string compressed = compression::compress(serialized);

    Statement insert(m_db, R"sql(
        INSERT INTO backup_sessions (created_at, stick_label, source_format, description, track_count,
            cue_count, uncompressed_size_bytes, compressed_size_bytes, schema_version, data)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )sql");
    insert.bind(1, isoTimestampUtc());
    insert.bind(2, stickLabel);
    insert.bind(3, sourceFormat);
    insert.bind(4, description);
    insert.bind(5, static_cast<int>(withCues.size()));
    insert.bind(6, cueCount);
    insert.bindInt64(7, static_cast<sqlite3_int64>(serialized.size()));
    insert.bindInt64(8, static_cast<sqlite3_int64>(compressed.size()));
    insert.bind(9, CurrentSnapshotFormatVersion);
    insert.bindBlob(10, compressed);
    insert.run();
    return insert.lastInsertRowId();
}

std::vector<BackupSessionSummary> LocalCueStore::listSnapshots()
{
    std::vector<BackupSessionSummary> summaries;
    Statement stmt(m_db, R"sql(
        SELECT id, created_at, stick_label, source_format, description, track_count, cue_count,
            uncompressed_size_bytes, compressed_size_bytes, schema_version
        FROM backup_sessions ORDER BY id DESC
    )sql");
    while (stmt.step()) {
        BackupSessionSummary summary;
        summary.id = stmt.columnInt64(0);
        summary.createdAt = stmt.columnText(1);
        summary.stickLabel = stmt.columnText(2);
        summary.sourceFormat = stmt.columnText(3);
        summary.description = stmt.columnText(4);
        summary.trackCount = stmt.columnInt(5);
        summary.cueCount = stmt.columnInt(6);
        summary.uncompressedSizeBytes = static_cast<std::uint64_t>(stmt.columnInt64(7));
        summary.compressedSizeBytes = static_cast<std::uint64_t>(stmt.columnInt64(8));
        summary.schemaVersion = stmt.columnInt(9);
        summaries.push_back(std::move(summary));
    }
    return summaries;
}

std::vector<Track> LocalCueStore::readSnapshot(std::int64_t id)
{
    Statement stmt(m_db, "SELECT data, uncompressed_size_bytes, schema_version FROM backup_sessions WHERE id = ?");
    stmt.bindInt64(1, id);
    if (!stmt.step()) {
        throw std::runtime_error("local cue store: no such backup session " + std::to_string(id));
    }
    std::string compressed = stmt.columnBlob(0);
    auto uncompressedSize = static_cast<size_t>(stmt.columnInt64(1));
    int formatVersion = stmt.columnInt(2);
    std::string serialized = compression::decompress(compressed, uncompressedSize);
    return deserializeTracks(serialized, formatVersion);
}

void LocalCueStore::setSnapshotDescription(std::int64_t id, const std::string &description)
{
    Statement update(m_db, "UPDATE backup_sessions SET description = ? WHERE id = ?");
    update.bind(1, description);
    update.bindInt64(2, id);
    update.run();
}

bool LocalCueStore::deleteSnapshot(std::int64_t id)
{
    Statement del(m_db, "DELETE FROM backup_sessions WHERE id = ?");
    del.bindInt64(1, id);
    del.run();
    return sqlite3_changes(m_db) > 0;
}

}  // namespace seabass::infrastructure::local
