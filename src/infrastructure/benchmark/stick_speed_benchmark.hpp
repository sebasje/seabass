#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace seabass::infrastructure::benchmark
{

struct SpeedBenchmarkResult
{
    double databaseReadMbps = 0.0;  // MiB/s reading the catalog's own database file(s)
    double audioReadMbps = 0.0;     // MiB/s reading a sample of real audio files
    int filesRead = 0;
    std::uint64_t bytesRead = 0;
    // An invented, comparative-only score -- see the .cpp's own doc
    // comment for the formula and what it's calibrated against. Only
    // meaningful for comparing sticks against each other on the same
    // computer, never as an absolute/manufacturer-spec number.
    int score = 0;
};

class StickSpeedBenchmark
{
public:
    // Reads real bytes from real files already on the stick. databaseFiles
    // and audioFiles are timed as two separate groups (a DJ library is
    // thousands of individually-opened small files, not one big
    // sequential read, so the two groups measure meaningfully different
    // things: one small hot-path database read vs. many discrete audio
    // file opens). Best-effort: a file that fails to open is silently
    // skipped. Reads at most sampleBytesPerFile from the front of each
    // file. Every file's cached pages are explicitly dropped (Linux only,
    // see the .cpp's dropCacheFor()) before the timed read starts, so
    // repeat runs measure real device I/O every time rather than getting
    // faster/inconsistent once the earlier scan or a previous run has
    // warmed the page cache -- this makes each call slower than a naive
    // read, on purpose.
    static SpeedBenchmarkResult run(const std::vector<std::string> &databaseFiles,
                                     const std::vector<std::string> &audioFiles,
                                     std::uint64_t sampleBytesPerFile = 4 * 1024 * 1024);
};

}  // namespace seabass::infrastructure::benchmark
