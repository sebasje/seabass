#include "infrastructure/benchmark/stick_speed_benchmark.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace djconvert::infrastructure::benchmark
{

namespace
{

// Best-effort: asks the kernel to drop any cached pages for this file
// before it's read, so a benchmark run measures real device I/O instead
// of a page-cache hit left over from the earlier library scan or a
// previous benchmark run -- confirmed on real hardware to otherwise make
// repeat runs look inconsistently, misleadingly faster ("hit and miss,
// faster on take two"). Linux-only: POSIX_FADV_DONTNEED isn't available
// on macOS (this project has no macOS target anyway) or Windows, so this
// is a no-op there, same "Linux fully verified, other platforms best-
// effort" precedent as infrastructure::system::readStickHardwareInfo().
void dropCacheFor(const std::string &path)
{
#if defined(__linux__)
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd >= 0) {
        posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
        ::close(fd);
    }
#else
    (void)path;
#endif
}

double measureGroup(const std::vector<std::string> &files, std::uint64_t sampleBytesPerFile,
                     std::uint64_t &bytesReadOut, int &filesReadOut)
{
    // Dropped before the timer starts, deliberately outside the measured
    // window -- this is real overhead a cold read wouldn't otherwise
    // pay, not something to charge against the throughput number.
    for (const auto &path : files) {
        dropCacheFor(path);
    }

    std::vector<char> buffer(sampleBytesPerFile);
    std::uint64_t totalBytes = 0;
    int filesRead = 0;

    auto start = std::chrono::steady_clock::now();
    for (const auto &path : files) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            continue;
        }
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        std::streamsize got = in.gcount();
        if (got > 0) {
            totalBytes += static_cast<std::uint64_t>(got);
            filesRead++;
        }
    }
    double elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    bytesReadOut += totalBytes;
    filesReadOut += filesRead;

    if (elapsedSeconds <= 0.0 || totalBytes == 0) {
        return 0.0;
    }
    constexpr double bytesPerMiB = 1024.0 * 1024.0;
    return (static_cast<double>(totalBytes) / bytesPerMiB) / elapsedSeconds;
}

// Weighted average, audio counts for more of the score since that's what
// dominates real DJ use (loading tracks quickly matters far more than
// the database file itself, which is small and read once at scan time).
// The reference point (60 MiB/s -> score 100) is calibrated against a
// decent USB 3.0 flash drive doing everyday small-file reads, nowhere
// near a drive's advertised sequential-read spec, since that's not the
// access pattern a DJ library actually produces. Not calibrated against
// any manufacturer benchmark -- purely comparative, for tracking the
// same laptop's sticks against each other over time. Uncapped above 100:
// a genuinely fast NVMe-backed enclosure exceeding the reference is
// itself useful signal, not something to clip away.
int computeScore(double databaseReadMbps, double audioReadMbps)
{
    constexpr double referenceMbps = 60.0;
    double effective = 0.75 * audioReadMbps + 0.25 * databaseReadMbps;
    return std::max(0, static_cast<int>(std::lround(effective / referenceMbps * 100.0)));
}

}  // namespace

SpeedBenchmarkResult StickSpeedBenchmark::run(const std::vector<std::string> &databaseFiles,
                                               const std::vector<std::string> &audioFiles,
                                               std::uint64_t sampleBytesPerFile)
{
    SpeedBenchmarkResult result;
    result.databaseReadMbps = measureGroup(databaseFiles, sampleBytesPerFile, result.bytesRead, result.filesRead);
    result.audioReadMbps = measureGroup(audioFiles, sampleBytesPerFile, result.bytesRead, result.filesRead);
    result.score = computeScore(result.databaseReadMbps, result.audioReadMbps);
    return result;
}

}  // namespace djconvert::infrastructure::benchmark
