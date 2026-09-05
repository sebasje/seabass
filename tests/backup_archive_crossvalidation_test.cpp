// Opens archives written by Zip64Writer with implementations that share
// no code with ours -- Python's zipfile (always, when python3 exists),
// Info-ZIP's unzip and 7-Zip (when installed). Our own reader must never
// be the only thing grading our writer's homework: a writer and reader
// with the same author can agree with each other and both disagree with
// the ZIP specification.
//
// Exits 77 (ctest SKIP_RETURN_CODE) when no python3 was found at
// configure time, so a bare build machine does not fail, but CI with
// python installed exercises it for real.

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "infrastructure/stick_backup/backup_manifest.hpp"
#include "infrastructure/stick_backup/posix_archive_file.hpp"
#include "infrastructure/stick_backup/zip64_reader.hpp"
#include "infrastructure/stick_backup/zip64_writer.hpp"
#include "infrastructure/stick_backup/zip_format.hpp"

using namespace seabass::infrastructure::stick_backup;
namespace fs = std::filesystem;

namespace
{

std::string envOrEmpty(const char *name)
{
    const char *value = std::getenv(name);
    if (value == nullptr) {
        return {};
    }
    std::string s(value);
    if (s.size() >= 8 && s.compare(s.size() - 8, 8, "NOTFOUND") == 0) {
        return {};
    }
    return s;
}

std::string shellQuote(const std::string &s)
{
    return "\"" + s + "\"";
}

// Runs a command, returns {exit status, stdout}.
std::pair<int, std::string> run(const std::string &command)
{
#if defined(_WIN32)
    FILE *pipe = _popen(command.c_str(), "r");
#else
    FILE *pipe = popen(command.c_str(), "r");
#endif
    if (pipe == nullptr) {
        return {-1, {}};
    }
    std::string output;
    char buffer[4096];
    while (std::fgets(buffer, sizeof buffer, pipe) != nullptr) {
        output += buffer;
    }
#if defined(_WIN32)
    int status = _pclose(pipe);
#else
    int status = pclose(pipe);
    if (status != -1) {
        status = WEXITSTATUS(status);
    }
#endif
    return {status, output};
}

std::string pseudoRandom(std::size_t size, std::uint64_t seed)
{
    std::string out(size, '\0');
    std::uint64_t x = seed * 0x9E3779B97F4A7C15ull + 1;
    for (std::size_t i = 0; i < size; ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        out[i] = static_cast<char>(x & 0xff);
    }
    return out;
}

// name -> (size, crc) as Python sees it. The script is written to a file
// so no shell quoting of Python source is involved.
std::map<std::string, std::pair<std::uint64_t, std::uint32_t>> pythonListing(const std::string &python, const fs::path &script,
                                                                           const fs::path &archive)
{
    auto [status, output] = run(shellQuote(python) + " " + shellQuote(script.string()) + " " + shellQuote(archive.string()));
    if (status != 0) {
        std::cerr << "python zipfile rejected " << archive << ":\n" << output << "\n";
        assert(false && "python zipfile rejected the archive");
    }
    std::map<std::string, std::pair<std::uint64_t, std::uint32_t>> listing;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty()) {
            continue;
        }
        std::size_t t1 = line.rfind('\t');
        std::size_t t2 = line.rfind('\t', t1 - 1);
        assert(t1 != std::string::npos && t2 != std::string::npos);
        std::string name = line.substr(0, t2);
        std::uint64_t size = std::stoull(line.substr(t2 + 1, t1 - t2 - 1));
        std::uint32_t crc = static_cast<std::uint32_t>(std::stoul(line.substr(t1 + 1)));
        listing[name] = {size, crc};
    }
    return listing;
}

void compareWithOurReader(const fs::path &archive,
                          const std::map<std::string, std::pair<std::uint64_t, std::uint32_t>> &theirs)
{
    PosixArchiveFile file(archive, PosixArchiveFile::OpenMode::ReadOnly);
    Zip64Reader reader = Zip64Reader::open(file);
    assert(theirs.size() == reader.entries().size());
    for (const CentralEntry &entry : reader.entries()) {
        auto it = theirs.find(entry.name);
        if (it == theirs.end()) {
            std::cerr << "python did not list " << entry.name << "\n";
            assert(false);
        }
        assert(it->second.first == entry.size);
        assert(it->second.second == entry.crc32);
    }
}

}  // namespace

int main()
{
    std::string python = envOrEmpty("SEABASS_PYTHON3");
    std::string unzip = envOrEmpty("SEABASS_UNZIP");
    std::string sevenZip = envOrEmpty("SEABASS_SEVENZIP");
    if (python.empty()) {
        std::cout << "skipped: no python3 available (SEABASS_PYTHON3 unset)\n";
        return 77;
    }

    fs::path root = fs::temp_directory_path() / "seabass_backup_archive_crossvalidation_test";
    fs::remove_all(root);
    fs::create_directories(root);

    fs::path script = root / "listing.py";
    {
        std::ofstream out(script);
        out << "import sys, zipfile\n"
               "z = zipfile.ZipFile(sys.argv[1])\n"
               "bad = z.testzip()\n"
               "if bad is not None:\n"
               "    print('testzip failed on', bad)\n"
               "    sys.exit(2)\n"
               "for i in z.infolist():\n"
               "    print(i.filename, i.file_size, i.CRC, sep='\\t')\n";
    }

    // Archive 1: the realistic shape -- nested dirs, unicode, empty file,
    // a few MB, a manifest.
    fs::path realistic = root / "realistic.zip";
    {
        PosixArchiveFile file(realistic, PosixArchiveFile::OpenMode::ReadWrite);
        Zip64Writer writer(file, {});
        writer.addDirectory("Contents", 1'700'000'000);
        writer.addDirectory("Contents/Amelie Lens", 1'700'000'001);
        writer.addFileFromMemory("Contents/Amelie Lens/whatever you do.mp3", 1'700'000'002, zip::bytesOf(pseudoRandom(5u << 20, 1)));
        writer.addFileFromMemory("Contents/Caf\xC3\xA9 del Mar.mp3", 1'700'000'003, zip::bytesOf(pseudoRandom(4096, 2)));
        writer.addFileFromMemory("Contents/empty.txt", 1'700'000'004, zip::bytesOf(""));
        writer.addDirectory("Engine Library/Music", 1'700'000'005);
        writer.addFileFromMemory("Engine Library/Database2/m.db", 1'700'000'006, zip::bytesOf(pseudoRandom(300'000, 3)));
        BackupManifest manifest;
        manifest.stickIdentifier = "uuid";
        manifest.stickLabel = "WHALESHARK2";
        manifest.createdAtUnix = 1'700'000'007;
        writer.finish(manifest.serialize(), std::string(ManifestEntryName), manifest.createdAtUnix);
        file.barrier();
    }
    compareWithOurReader(realistic, pythonListing(python, script, realistic));
    std::cout << "case 1 (python zipfile: testzip clean, names/sizes/CRCs agree) OK\n";

    // Archive 2: forces the zip64 entry-count path in the *reader* under test.
    fs::path many = root / "many.zip";
    {
        PosixArchiveFile file(many, PosixArchiveFile::OpenMode::ReadWrite);
        Zip64Writer writer(file, {});
        for (std::size_t i = 0; i < 66'000; ++i) {
            writer.addFileFromMemory("n/" + std::to_string(i), 1, zip::bytesOf(i % 2 == 0 ? "" : "x"));
        }
        BackupManifest manifest;
        manifest.createdAtUnix = 1;
        writer.finish(manifest.serialize(), std::string(ManifestEntryName), 1);
        file.barrier();
    }
    compareWithOurReader(many, pythonListing(python, script, many));
    std::cout << "case 2 (python zipfile: 66 000 entries via the zip64 record) OK\n";

    if (!unzip.empty()) {
        auto [status, output] = run(shellQuote(unzip) + " -tqq " + shellQuote(realistic.string()) + " 2>&1");
        if (status != 0) {
            std::cerr << output << "\n";
        }
        assert(status == 0);
        std::cout << "case 3 (unzip -t) OK\n";
    } else {
        std::cout << "case 3 (unzip -t) skipped -- unzip not found\n";
    }

    if (!sevenZip.empty()) {
        auto [status, output] = run(shellQuote(sevenZip) + " t " + shellQuote(realistic.string()) + " 2>&1");
        if (status != 0) {
            std::cerr << output << "\n";
        }
        assert(status == 0);
        std::cout << "case 4 (7z t) OK\n";
    } else {
        std::cout << "case 4 (7z t) skipped -- 7z not found\n";
    }

    fs::remove_all(root);
    std::cout << "all cases passed\n";
    return 0;
}
