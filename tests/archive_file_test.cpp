#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "infrastructure/stick_backup/in_memory_archive_file.hpp"
#include "infrastructure/stick_backup/posix_archive_file.hpp"

using namespace seabass::infrastructure::stick_backup;
namespace fs = std::filesystem;

namespace
{

std::vector<std::byte> bytesOf(const std::string &s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

std::string stringOf(const std::vector<std::byte> &b)
{
    return std::string(reinterpret_cast<const char *>(b.data()), b.size());
}

std::string readAll(const ArchiveFile &file)
{
    std::vector<std::byte> buf(file.size());
    file.readAt(0, buf);
    return stringOf(buf);
}

void appendString(ArchiveFile &file, const std::string &s)
{
    std::vector<std::byte> b = bytesOf(s);
    file.append(b);
}

}  // namespace

int main()
{
    fs::path root = fs::temp_directory_path() / "seabass_archive_file_test";
    fs::remove_all(root);
    fs::create_directories(root);

    // ---- PosixArchiveFile ----
    {
        fs::path path = root / "a.zip";
        {
            PosixArchiveFile file(path, PosixArchiveFile::OpenMode::ReadWrite);
            assert(file.size() == 0);
            appendString(file, "hello ");
            appendString(file, "world");
            assert(file.size() == 11);
            file.barrier();
            assert(readAll(file) == "hello world");

            std::vector<std::byte> mid(5);
            file.readAt(6, mid);
            assert(stringOf(mid) == "world");

            bool threw = false;
            try {
                std::vector<std::byte> tooFar(6);
                file.readAt(6, tooFar);
            } catch (const ArchiveIoError &) {
                threw = true;
            }
            assert(threw);

            file.truncate(5);
            assert(file.size() == 5);
            assert(readAll(file) == "hello");
            file.barrier();
        }
        std::cout << "case 1 (posix: append/readAt/truncate/barrier, short read throws) OK\n";

        {
            // Reopening ReadWrite never truncates; size comes from the real file.
            PosixArchiveFile again(path, PosixArchiveFile::OpenMode::ReadWrite);
            assert(again.size() == 5);
            appendString(again, "!");
            assert(readAll(again) == "hello!");
        }
        {
            PosixArchiveFile ro(path, PosixArchiveFile::OpenMode::ReadOnly);
            assert(readAll(ro) == "hello!");
            bool threw = false;
            try {
                appendString(ro, "x");
            } catch (const ArchiveIoError &) {
                threw = true;
            }
            assert(threw);
            ro.barrier();  // no-op, must not throw
        }
        bool missingThrew = false;
        try {
            PosixArchiveFile missing(root / "does_not_exist.zip", PosixArchiveFile::OpenMode::ReadOnly);
        } catch (const ArchiveIoError &) {
            missingThrew = true;
        }
        assert(missingThrew);
        std::cout << "case 2 (posix: reopen keeps content, read-only refuses writes, missing file throws) OK\n";
    }

    // ---- InMemoryArchiveFile: plain file behaviour ----
    {
        InMemoryArchiveFile file;
        appendString(file, "abc");
        appendString(file, "def");
        assert(file.size() == 6);
        assert(readAll(file) == "abcdef");
        file.truncate(2);
        assert(readAll(file) == "ab");
        assert(file.truncatedImage(1) == bytesOf("a"));
        assert(file.truncatedImage(99) == bytesOf("ab"));
        bool threw = false;
        try {
            file.truncate(10);
        } catch (const ArchiveIoError &) {
            threw = true;
        }
        assert(threw);
        std::cout << "case 3 (in-memory: behaves like a file) OK\n";
    }

    // ---- InMemoryArchiveFile: durability model ----
    {
        auto clock = std::make_shared<FaultClock>();
        InMemoryArchiveFile file(clock);
        auto keepAll = [](std::size_t) { return true; };
        auto dropAll = [](std::size_t) { return false; };

        appendString(file, "AAAA");  // mutation 0, tick 0
        file.barrier();              // barrier at tick 0 -> clock 1
        appendString(file, "BBBB");  // mutation 1, tick 1
        appendString(file, "CCCC");  // mutation 2, tick 1
        file.barrier();              // barrier at tick 1 -> clock 2
        appendString(file, "DDDD");  // mutation 3, tick 2 (never flushed)

        // Crash before any barrier: everything is in flight.
        assert(file.crashImage(0, dropAll).empty());
        assert(stringOf(file.crashImage(0, keepAll)) == "AAAABBBBCCCCDDDD");

        // After the first barrier: AAAA durable, the rest in flight.
        assert(stringOf(file.crashImage(1, dropAll)) == "AAAA");
        // Lost BBBB but kept CCCC -> a zero-filled hole where BBBB was.
        std::string reordered = stringOf(file.crashImage(1, [](std::size_t i) { return i == 2; }));
        assert(reordered.size() == 12);
        assert(reordered.substr(0, 4) == "AAAA");
        assert(reordered.substr(4, 4) == std::string(4, '\0'));
        assert(reordered.substr(8, 4) == "CCCC");

        // After the second barrier: A, B, C durable; D still in flight.
        assert(stringOf(file.crashImage(2, dropAll)) == "AAAABBBBCCCC");
        assert(stringOf(file.crashImage(2, keepAll)) == "AAAABBBBCCCCDDDD");

        // A crash tick beyond every barrier still leaves the unflushed
        // tail in flight -- there was no barrier after it.
        assert(stringOf(file.crashImage(50, dropAll)) == "AAAABBBBCCCC");
        std::cout << "case 4 (in-memory: barrier-based durability, lost writes read as zeros) OK\n";
    }

    // ---- Two files on one clock (archive + journal) ----
    {
        auto clock = std::make_shared<FaultClock>();
        InMemoryArchiveFile archive(clock);
        InMemoryArchiveFile journal(clock);
        auto dropAll = [](std::size_t) { return false; };

        appendString(journal, "J");   // journal mutation 0, tick 0
        journal.barrier();            // tick 0 -> 1
        appendString(archive, "X");   // archive mutation 0, tick 1
        archive.barrier();            // tick 1 -> 2
        journal.truncate(0);          // journal mutation 1, tick 2 (clear, not yet flushed)
        journal.barrier();            // tick 2 -> 3

        // Crash after the archive's barrier but before the journal's
        // clear was flushed: the archive has X, the journal still says J.
        assert(stringOf(archive.crashImage(2, dropAll)) == "X");
        assert(stringOf(journal.crashImage(2, dropAll)) == "J");
        // The archive's own barrier does nothing for the journal's
        // in-flight truncate -- fsync is per file.
        // After the journal's barrier the clear is durable.
        assert(journal.crashImage(3, dropAll).empty());
        // Before the archive's barrier, X is in flight even though the
        // journal had already flushed.
        assert(archive.crashImage(1, dropAll).empty());
        assert(clock->ticks == 3);
        std::cout << "case 5 (in-memory: two files share a clock, fsync is per file) OK\n";
    }

    fs::remove_all(root);
    std::cout << "all cases passed\n";
    return 0;
}
