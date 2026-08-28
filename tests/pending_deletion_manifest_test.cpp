#include <cassert>
#include <filesystem>
#include <iostream>

#include "infrastructure/cleanup/pending_deletion_manifest.hpp"

using namespace djconvert::infrastructure::cleanup;
namespace fs = std::filesystem;

int main()
{
    fs::path root = fs::temp_directory_path() / "djconvert_pending_deletion_manifest_test";
    fs::remove_all(root);
    fs::create_directories(root);
    fs::path manifestPath = root / ".djconvert-pending-deletions.jsonl";

    // A fresh manifest that doesn't exist yet on disk lists as empty,
    // not an error.
    {
        PendingDeletionManifest manifest(manifestPath.string());
        assert(manifest.list().empty());
        std::cout << "case 1 (missing file -> empty list) OK\n";
    }

    // Append/list round trip, including a value with characters that
    // need JSON escaping (quotes, backslash, a real path separator).
    {
        PendingDeletionManifest manifest(manifestPath.string());

        PendingDeletion a;
        a.format = "rekordbox";
        a.filePath = "/Volumes/STICK/Contents/track \"one\".mp3";
        a.title = "Voices In My Head";
        a.artist = "Artist A";
        a.backupId = "20260101T000000-duplicate-file-cleanup";
        manifest.append(a);

        PendingDeletion b;
        b.format = "engine";
        b.filePath = "C:\\Music\\track two.flac";
        b.title = "Track Two";
        b.artist = "Artist B";
        b.backupId = "20260101T000001-duplicate-file-cleanup";
        manifest.append(b);

        auto entries = manifest.list();
        assert(entries.size() == 2);

        assert(entries[0].format == "rekordbox");
        assert(entries[0].filePath == "/Volumes/STICK/Contents/track \"one\".mp3");
        assert(entries[0].title == "Voices In My Head");
        assert(entries[0].artist == "Artist A");
        assert(entries[0].backupId == "20260101T000000-duplicate-file-cleanup");
        assert(!entries[0].timestampUtc.empty());

        assert(entries[1].format == "engine");
        assert(entries[1].filePath == "C:\\Music\\track two.flac");
        assert(entries[1].title == "Track Two");

        std::cout << "case 2 (append/list round trip, JSON escaping) OK\n";
    }

    // A second manifest instance opened on the same path sees prior
    // entries plus its own new append -- confirms append-only, not
    // truncate-on-open.
    {
        PendingDeletionManifest manifest(manifestPath.string());
        PendingDeletion c;
        c.format = "rekordbox";
        c.filePath = "/Volumes/STICK/Contents/track three.mp3";
        c.title = "Track Three";
        c.artist = "Artist C";
        c.backupId = "20260101T000002-duplicate-file-cleanup";
        manifest.append(c);

        assert(manifest.list().size() == 3);
        std::cout << "case 3 (append-only across instances) OK\n";
    }

    std::cout << "all cases passed\n";
    return 0;
}
