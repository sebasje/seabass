#include "infrastructure/local/local_cue_store.hpp"

#include <sqlite3.h>

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <optional>
#include <stdexcept>

#include "domain/track_matching.hpp"

namespace djconvert::infrastructure::local
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
    gmtime_r(&t, &tm);
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

}  // namespace

std::string LocalCueStore::defaultPath()
{
    const char *xdgDataHome = std::getenv("XDG_DATA_HOME");
    fs::path dataDir = (xdgDataHome && *xdgDataHome) ? fs::path(xdgDataHome)
                                                      : fs::path(std::getenv("HOME")) / ".local" / "share";
    return (dataDir / "djconvert" / "cues.db").string();
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
                           "SELECT kind, hot_cue_number, position_ms, color, comment FROM cues WHERE track_id = ?");
        cueStmt.bindInt64(1, id);
        while (cueStmt.step()) {
            CuePoint cue;
            cue.kind = cueStmt.columnText(0) == "hot" ? CuePoint::Kind::Hot : CuePoint::Kind::Memory;
            cue.hotCueNumber = cueStmt.columnInt(1);
            cue.positionMs = cueStmt.columnDouble(2);
            cue.color = cueStmt.columnText(3);
            cue.comment = cueStmt.columnText(4);
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
                INSERT INTO cues (track_id, kind, hot_cue_number, position_ms, color, comment)
                VALUES (?, ?, ?, ?, ?, ?)
            )sql");
            insertCue.bindInt64(1, trackId);
            insertCue.bind(2, cue.kind == CuePoint::Kind::Hot ? "hot" : "memory");
            insertCue.bind(3, cue.hotCueNumber);
            insertCue.bind(4, cue.positionMs);
            insertCue.bind(5, cue.color);
            insertCue.bind(6, cue.comment);
            insertCue.run();
        }
    }
}

}  // namespace djconvert::infrastructure::local
