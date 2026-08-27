#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "infrastructure/backup/filesystem_backup_store.hpp"

using namespace djconvert::infrastructure::backup;
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
    fs::path root = fs::temp_directory_path() / "djconvert_filesystem_backup_store_test";
    fs::remove_all(root);
    fs::create_directories(root);

    fs::path backupsDir = root / ".djconvert-backups";
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

        writeFile(targetFile, "corrupted by something later");
        assert(readFile(targetFile) == "corrupted by something later");

        assert(store.restore(record.id));
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
        assert(store.restore(record.id));
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

        assert(store.remove(record1.id));
        auto remaining = store.list();
        assert(remaining.size() == 1);
        assert(remaining[0].id == record2.id);
        assert(!store.remove(record1.id));  // already gone
        std::cout << "case 4 (deleting one backup leaves the other intact) OK\n";
    }

    fs::remove_all(root);
    std::cout << "all cases passed\n";
    return 0;
}
