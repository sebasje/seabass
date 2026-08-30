#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

namespace seabass::infrastructure::benchmark
{

// One past speed-benchmark run, persisted locally on this computer (never
// on the stick itself -- the whole point is comparing sticks against each
// other on the same laptop over time, which a per-stick record can't do).
struct BenchmarkRecord
{
    std::int64_t id = 0;
    std::string ranAt;  // ISO 8601 UTC, set by record(), ignored on input
    std::string stickLabel;
    // Best-effort stable identifier for the physical stick (filesystem
    // UUID when available, see infrastructure::system::StickHardwareInfo),
    // used to group a stick's own history together across sessions even
    // if its volume label changes.
    std::string stickIdentifier;
    std::string filesystem;
    std::string usbSpeedLabel;
    double usbSpeedMbps = 0.0;
    double databaseReadMbps = 0.0;
    double audioReadMbps = 0.0;
    int score = 0;
};

// SQLite-backed (same rationale as LocalCueStore: libdjinterop already
// requires SQLite to build, so this adds no new third-party dependency),
// stored under this computer's own app-data directory, independent of
// any stick.
class StickBenchmarkHistory
{
public:
    // path: defaults to $XDG_DATA_HOME/seabass/benchmark_history.db (or
    // the Windows/fallback equivalent, same convention as
    // LocalCueStore::defaultPath()), creating the schema on first use. An
    // explicit path is accepted for tests.
    explicit StickBenchmarkHistory(std::string path = defaultPath());
    ~StickBenchmarkHistory();

    StickBenchmarkHistory(const StickBenchmarkHistory &) = delete;
    StickBenchmarkHistory &operator=(const StickBenchmarkHistory &) = delete;

    // Stamps ranAt with the current UTC time and inserts a new row;
    // record.id/ranAt on the input are ignored.
    void record(const BenchmarkRecord &record);

    // Every past run for this exact stickIdentifier, newest first.
    std::vector<BenchmarkRecord> historyFor(const std::string &stickIdentifier) const;

    // Every run ever recorded on this computer, newest first -- for
    // comparing this stick against others previously tested here.
    std::vector<BenchmarkRecord> allHistory() const;

    static std::string defaultPath();

private:
    sqlite3 *m_db = nullptr;
};

}  // namespace seabass::infrastructure::benchmark
