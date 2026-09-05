#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "infrastructure/backup/filesystem_backup_store.hpp"

using namespace seabass::infrastructure::backup;
namespace fs = std::filesystem;

namespace
{

void writeFile(const fs::path &path, const std::string &content)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    out << content;
}

std::string readFile(const fs::path &path)
{
    std::ifstream in(path);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return content;
}

}  // namespace

int main()
{
    fs::path root = fs::temp_directory_path() / "seabass_filesystem_backup_store_test";
    fs::remove_all(root);
    fs::create_directories(root);

    fs::path backupsDir = root / ".seabass-backups";
    fs::path targetFile = root / "m.db";
    writeFile(targetFile, "original contents");

    // Basic backup + restore round trip.
    {
        FilesystemBackupStore store(backupsDir.string());
        auto record = store.backup({targetFile.string()}, "sync");
        assert(!record.id.empty());

        auto records = store.list();
        assert(records.size() == 1);
        assert(records[0].id == record.id);
        assert(records[0].filePaths.size() == 1);
        assert(records[0].filePaths[0] == fs::absolute(targetFile).string());

        writeFile(targetFile, "corrupted by something later");
        assert(readFile(targetFile) == "corrupted by something later");

        bool restored = store.restore(record.id);
        assert(restored);
        assert(readFile(targetFile) == "original contents");

        // restore() itself backs up what it overwrote first -- the
        // "corrupted" version should now be recoverable too.
        auto afterRestore = store.list();
        assert(afterRestore.size() == 2);
        std::cout << "case 1 (backup + restore round trip, restore backs up what it overwrites) OK\n";
    }

    // A manifest with no MANIFEST-VERSION header (predating this feature)
    // is treated as version 1, not refused.
    {
        fs::remove_all(backupsDir);
        FilesystemBackupStore store(backupsDir.string());
        auto record = store.backup({targetFile.string()}, "sync");

        // Rewrite the manifest to strip the version header, simulating a
        // backup made before versioning existed.
        fs::path manifestPath = fs::path(record.path) / ".manifest";
        std::string original = readFile(manifestPath);
        size_t firstNewline = original.find('\n');
        std::string withoutHeader = original.substr(firstNewline + 1);
        writeFile(manifestPath, withoutHeader);

        writeFile(targetFile, "changed again");
        bool restored = store.restore(record.id);
        assert(restored);
        assert(readFile(targetFile) == "original contents");
        std::cout << "case 2 (manifest without a version header still restores, as version 1) OK\n";
    }

    // A manifest claiming a future format version this build doesn't
    // understand is refused, not misinterpreted.
    {
        fs::remove_all(backupsDir);
        FilesystemBackupStore store(backupsDir.string());
        auto record = store.backup({targetFile.string()}, "sync");

        fs::path manifestPath = fs::path(record.path) / ".manifest";
        std::string original = readFile(manifestPath);
        size_t firstNewline = original.find('\n');
        std::string rest = original.substr(firstNewline + 1);
        writeFile(manifestPath, "MANIFEST-VERSION\t999\n" + rest);

        writeFile(targetFile, "should stay untouched");
        assert(!store.restore(record.id));
        assert(readFile(targetFile) == "should stay untouched");
        std::cout << "case 3 (unrecognized manifest version refuses to restore) OK\n";
    }

    // Deleting a single backup removes just that one.
    {
        fs::remove_all(backupsDir);
        FilesystemBackupStore store(backupsDir.string());
        auto record1 = store.backup({targetFile.string()}, "sync");
        auto record2 = store.backup({targetFile.string()}, "sync");
        assert(store.list().size() == 2);

        bool removed = store.remove(record1.id);
        assert(removed);
        auto remaining = store.list();
        assert(remaining.size() == 1);
        assert(remaining[0].id == record2.id);
        assert(!store.remove(record1.id));  // already gone
        std::cout << "case 4 (deleting one backup leaves the other intact) OK\n";
    }

    // prune(): removes only the oldest backups beyond keepCount, leaves
    // the newest keepCount intact, and returns exactly the bytes freed.
    {
        fs::remove_all(backupsDir);
        FilesystemBackupStore store(backupsDir.string());
        // Same label for every call, matching case 4's own convention:
        // ids are timestamp-*and*-label-prefixed, so distinct labels
        // created within the same second wouldn't sort chronologically
        // (lexical order would go by label text, not creation order) --
        // the same label instead forces the "-1"/"-2"/... disambiguating
        // suffix (filesystem_backup_store.cpp's own backup() comment),
        // which *does* sort chronologically.
        auto r1 = store.backup({targetFile.string()}, "sync");
        auto r2 = store.backup({targetFile.string()}, "sync");
        auto r3 = store.backup({targetFile.string()}, "sync");
        auto r4 = store.backup({targetFile.string()}, "sync");
        auto r5 = store.backup({targetFile.string()}, "sync");
        assert(store.list().size() == 5);

        auto beforeRecords = store.list();
        std::uint64_t expectedFreed = 0;
        for (const auto &r : beforeRecords) {
            if (r.id == r1.id || r.id == r2.id) {
                expectedFreed += r.sizeBytes;
            }
        }

        auto freed = store.prune(3);
        assert(freed == expectedFreed);
        assert(freed > 0);

        auto remaining = store.list();
        assert(remaining.size() == 3);
        for (const auto &r : remaining) {
            assert(r.id != r1.id);  // oldest two: pruned
            assert(r.id != r2.id);
        }
        bool has3 = false, has4 = false, has5 = false;
        for (const auto &r : remaining) {
            if (r.id == r3.id) has3 = true;
            if (r.id == r4.id) has4 = true;
            if (r.id == r5.id) has5 = true;
        }
        assert(has3 && has4 && has5);  // newest three: kept
        std::cout << "case 5 (prune: removes only the oldest backups beyond keepCount) OK\n";
    }

    // prune(): asking to keep at least as many as exist is a genuine
    // no-op -- nothing removed, 0 bytes freed, not an error and not an
    // off-by-one that removes one anyway.
    {
        fs::remove_all(backupsDir);
        FilesystemBackupStore store(backupsDir.string());
        store.backup({targetFile.string()}, "one");
        store.backup({targetFile.string()}, "two");
        assert(store.list().size() == 2);

        std::uint64_t prunedCount1 = store.prune(2);
        assert(prunedCount1 == 0);
        assert(store.list().size() == 2);

        std::uint64_t prunedCount2 = store.prune(10);
        assert(prunedCount2 == 0);  // keepCount well beyond what exists
        assert(store.list().size() == 2);
        std::cout << "case 6 (prune: keepCount >= existing count is a true no-op) OK\n";
    }

    // prune(0): the explicit "keep nothing" edge case removes every
    // backup, not just every-but-one -- worth pinning down since off-
    // by-one bugs love this exact boundary.
    {
        fs::remove_all(backupsDir);
        FilesystemBackupStore store(backupsDir.string());
        store.backup({targetFile.string()}, "one");
        store.backup({targetFile.string()}, "two");
        assert(store.list().size() == 2);

        auto freed = store.prune(0);
        assert(freed > 0);
        assert(store.list().empty());
        std::cout << "case 7 (prune(0): removes every backup, the true empty-keep edge case) OK\n";
    }

    fs::remove_all(root);
    std::cout << "all cases passed\n";
    return 0;
}
