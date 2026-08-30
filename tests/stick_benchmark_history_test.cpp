#include <cassert>
#include <filesystem>
#include <iostream>

#include "infrastructure/benchmark/stick_benchmark_history.hpp"

using namespace seabass::infrastructure::benchmark;
namespace fs = std::filesystem;

namespace
{

BenchmarkRecord makeRecord(std::string stickIdentifier, std::string stickLabel, int score)
{
    BenchmarkRecord r;
    r.stickIdentifier = std::move(stickIdentifier);
    r.stickLabel = std::move(stickLabel);
    r.filesystem = "vfat";
    r.usbSpeedLabel = "5 Gbps (USB 3.0/3.1 Gen1 SuperSpeed)";
    r.usbSpeedMbps = 5000;
    r.databaseReadMbps = 40.0;
    r.audioReadMbps = 70.0;
    r.score = score;
    return r;
}

}  // namespace

int main()
{
    fs::path dbPath = fs::temp_directory_path() / "seabass_benchmark_history_test.db";
    fs::remove(dbPath);

    // Case 1: record + historyFor round-trips for one stick, newest first.
    {
        StickBenchmarkHistory history(dbPath.string());
        history.record(makeRecord("UUID-A", "WHALESHARK2", 80));
        history.record(makeRecord("UUID-A", "WHALESHARK2", 95));

        auto records = history.historyFor("UUID-A");
        assert(records.size() == 2);
        assert(records[0].score == 95);  // newest first
        assert(records[1].score == 80);
        assert(records[0].stickLabel == "WHALESHARK2");
        assert(records[0].filesystem == "vfat");
        assert(records[0].usbSpeedMbps == 5000);
        std::cout << "case 1 (record + historyFor round-trip, newest first) OK\n";
    }

    // Case 2: historyFor a different stick doesn't see the first stick's rows.
    {
        StickBenchmarkHistory history(dbPath.string());
        history.record(makeRecord("UUID-B", "OtherStick", 60));

        assert(history.historyFor("UUID-B").size() == 1);
        assert(history.historyFor("UUID-A").size() == 2);
        std::cout << "case 2 (per-stick history isolation) OK\n";
    }

    // Case 3: allHistory sees every stick, newest first.
    {
        StickBenchmarkHistory history(dbPath.string());
        auto all = history.allHistory();
        assert(all.size() == 3);
        assert(all[0].stickIdentifier == "UUID-B");
        std::cout << "case 3 (allHistory spans every stick) OK\n";
    }

    // Case 4: persistence survives reopening the same file.
    {
        StickBenchmarkHistory reopened(dbPath.string());
        assert(reopened.allHistory().size() == 3);
        std::cout << "case 4 (persists across reopen) OK\n";
    }

    fs::remove(dbPath);
    std::cout << "All stick_benchmark_history tests passed.\n";
    return 0;
}
