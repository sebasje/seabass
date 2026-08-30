#include "infrastructure/benchmark/stick_benchmark_history.hpp"

#include <sqlite3.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <stdexcept>

namespace seabass::infrastructure::benchmark
{

namespace fs = std::filesystem;

namespace
{

std::string isoTimestampUtc()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// Same thin RAII wrapper as LocalCueStore's own (not shared between the
// two -- each infrastructure SQLite user keeps its own small copy rather
// than factoring this into a shared header, matching this codebase's
// existing convention).
class Statement
{
public:
    Statement(sqlite3 *db, const char *sql) : m_db(db)
    {
        if (sqlite3_prepare_v2(db, sql, -1, &m_stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string("benchmark history: failed to prepare statement: ") +
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

    void run()
    {
        int rc = sqlite3_step(m_stmt);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            throw std::runtime_error(std::string("benchmark history: statement failed: ") + sqlite3_errmsg(m_db));
        }
    }

    bool step()
    {
        int rc = sqlite3_step(m_stmt);
        if (rc == SQLITE_ROW) {
            return true;
        }
        if (rc == SQLITE_DONE) {
            return false;
        }
        throw std::runtime_error(std::string("benchmark history: statement failed: ") + sqlite3_errmsg(m_db));
    }

    std::string columnText(int index)
    {
        const unsigned char *text = sqlite3_column_text(m_stmt, index);
        return text ? reinterpret_cast<const char *>(text) : "";
    }
    double columnDouble(int index) { return sqlite3_column_double(m_stmt, index); }
    int columnInt(int index) { return sqlite3_column_int(m_stmt, index); }
    sqlite3_int64 columnInt64(int index) { return sqlite3_column_int64(m_stmt, index); }

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
        throw std::runtime_error("benchmark history: " + message);
    }
}

std::vector<BenchmarkRecord> queryAll(sqlite3 *db, const char *sql, const std::string *stickIdentifier)
{
    Statement stmt(db, sql);
    if (stickIdentifier) {
        stmt.bind(1, *stickIdentifier);
    }
    std::vector<BenchmarkRecord> records;
    while (stmt.step()) {
        BenchmarkRecord r;
        r.id = stmt.columnInt64(0);
        r.ranAt = stmt.columnText(1);
        r.stickLabel = stmt.columnText(2);
        r.stickIdentifier = stmt.columnText(3);
        r.filesystem = stmt.columnText(4);
        r.usbSpeedLabel = stmt.columnText(5);
        r.usbSpeedMbps = stmt.columnDouble(6);
        r.databaseReadMbps = stmt.columnDouble(7);
        r.audioReadMbps = stmt.columnDouble(8);
        r.score = stmt.columnInt(9);
        records.push_back(std::move(r));
    }
    return records;
}

}  // namespace

std::string StickBenchmarkHistory::defaultPath()
{
#if defined(_WIN32)
    const char *localAppData = std::getenv("LOCALAPPDATA");
    fs::path dataDir = (localAppData && *localAppData) ? fs::path(localAppData)
                                                        : fs::path(std::getenv("USERPROFILE")) / "AppData" / "Local";
#else
    const char *xdgDataHome = std::getenv("XDG_DATA_HOME");
    fs::path dataDir = (xdgDataHome && *xdgDataHome) ? fs::path(xdgDataHome)
                                                      : fs::path(std::getenv("HOME")) / ".local" / "share";
#endif
    return (dataDir / "seabass" / "benchmark_history.db").string();
}

StickBenchmarkHistory::StickBenchmarkHistory(std::string path)
{
    fs::create_directories(fs::path(path).parent_path());
    if (sqlite3_open(path.c_str(), &m_db) != SQLITE_OK) {
        std::string message = m_db ? sqlite3_errmsg(m_db) : "unknown error";
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        throw std::runtime_error("benchmark history: failed to open " + path + ": " + message);
    }
    exec(m_db, R"(
        CREATE TABLE IF NOT EXISTS benchmark_runs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            ran_at TEXT NOT NULL,
            stick_label TEXT NOT NULL,
            stick_identifier TEXT NOT NULL,
            filesystem TEXT NOT NULL,
            usb_speed_label TEXT NOT NULL,
            usb_speed_mbps REAL NOT NULL,
            database_read_mbps REAL NOT NULL,
            audio_read_mbps REAL NOT NULL,
            score INTEGER NOT NULL
        );
    )");
    exec(m_db, "CREATE INDEX IF NOT EXISTS idx_benchmark_runs_stick ON benchmark_runs(stick_identifier);");
}

StickBenchmarkHistory::~StickBenchmarkHistory()
{
    if (m_db) {
        sqlite3_close(m_db);
    }
}

void StickBenchmarkHistory::record(const BenchmarkRecord &r)
{
    Statement stmt(m_db, R"(
        INSERT INTO benchmark_runs
            (ran_at, stick_label, stick_identifier, filesystem, usb_speed_label,
             usb_speed_mbps, database_read_mbps, audio_read_mbps, score)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
    )");
    stmt.bind(1, isoTimestampUtc());
    stmt.bind(2, r.stickLabel);
    stmt.bind(3, r.stickIdentifier);
    stmt.bind(4, r.filesystem);
    stmt.bind(5, r.usbSpeedLabel);
    stmt.bind(6, r.usbSpeedMbps);
    stmt.bind(7, r.databaseReadMbps);
    stmt.bind(8, r.audioReadMbps);
    stmt.bind(9, r.score);
    stmt.run();
}

std::vector<BenchmarkRecord> StickBenchmarkHistory::historyFor(const std::string &stickIdentifier) const
{
    return queryAll(m_db,
                     "SELECT id, ran_at, stick_label, stick_identifier, filesystem, usb_speed_label, "
                     "usb_speed_mbps, database_read_mbps, audio_read_mbps, score FROM benchmark_runs "
                     "WHERE stick_identifier = ? ORDER BY id DESC;",
                     &stickIdentifier);
}

std::vector<BenchmarkRecord> StickBenchmarkHistory::allHistory() const
{
    return queryAll(m_db,
                     "SELECT id, ran_at, stick_label, stick_identifier, filesystem, usb_speed_label, "
                     "usb_speed_mbps, database_read_mbps, audio_read_mbps, score FROM benchmark_runs "
                     "ORDER BY id DESC;",
                     nullptr);
}

}  // namespace seabass::infrastructure::benchmark
