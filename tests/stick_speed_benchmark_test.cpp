#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "infrastructure/benchmark/stick_speed_benchmark.hpp"

using namespace seabass::infrastructure::benchmark;
namespace fs = std::filesystem;

namespace
{

void writeFile(const fs::path &path, std::size_t bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    std::string chunk(bytes, 'x');
    out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
}

}  // namespace

int main()
{
    fs::path root = fs::temp_directory_path() / "seabass_speed_benchmark_test";
    fs::remove_all(root);
    fs::create_directories(root);

    // Case 1: reads real files, reports non-zero throughput and correct
    // byte/file counts.
    {
        fs::path db = root / "m.db";
        fs::path audio1 = root / "a.mp3";
        fs::path audio2 = root / "b.mp3";
        writeFile(db, 64 * 1024);
        writeFile(audio1, 128 * 1024);
        writeFile(audio2, 128 * 1024);

        auto result =
            StickSpeedBenchmark::run({db.string()}, {audio1.string(), audio2.string()}, /*sampleBytesPerFile=*/1024 * 1024);
        assert(result.filesRead == 3);
        assert(result.bytesRead == 64 * 1024 + 128 * 1024 + 128 * 1024);
        assert(result.databaseReadMbps > 0.0);
        assert(result.audioReadMbps > 0.0);
        assert(result.score >= 0);
        std::cout << "case 1 (real files measured, byte counts correct) OK\n";
    }

    // Case 2: a missing file is skipped rather than aborting the whole run.
    {
        fs::path audio = root / "c.mp3";
        writeFile(audio, 64 * 1024);
        auto result = StickSpeedBenchmark::run({(root / "does-not-exist.db").string()}, {audio.string()});
        assert(result.filesRead == 1);
        assert(result.databaseReadMbps == 0.0);
        assert(result.audioReadMbps > 0.0);
        std::cout << "case 2 (missing file skipped, not aborted) OK\n";
    }

    // Case 3: no files at all -> zero everything, no crash.
    {
        auto result = StickSpeedBenchmark::run({}, {});
        assert(result.filesRead == 0);
        assert(result.bytesRead == 0);
        assert(result.score == 0);
        std::cout << "case 3 (empty input is safe) OK\n";
    }

    // Case 4: sampleBytesPerFile caps how much of a larger file is read,
    // reading only the front of it rather than the whole thing.
    {
        fs::path big = root / "big.wav";
        writeFile(big, 4 * 1024 * 1024);
        auto result = StickSpeedBenchmark::run({}, {big.string()}, /*sampleBytesPerFile=*/1024 * 1024);
        assert(result.bytesRead == 1024 * 1024);
        std::cout << "case 4 (sample size caps bytes read from a larger file) OK\n";
    }

    fs::remove_all(root);
    std::cout << "All stick_speed_benchmark tests passed.\n";
    return 0;
}
