#include <sqlite3.h>

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

#include "application/use_cases/backup_stick.hpp"
#include "application/use_cases/compact_stick_backup.hpp"
#include "application/use_cases/restore_stick_backup.hpp"
#include "infrastructure/stick_backup/posix_archive_file.hpp"
#include "infrastructure/stick_backup/restore_path_sanitizer.hpp"
#include "infrastructure/stick_backup/stick_tree_walker.hpp"
#include "infrastructure/stick_backup/zip64_reader.hpp"
#include "infrastructure/stick_backup/zip64_writer.hpp"
#include "infrastructure/stick_backup/zip_format.hpp"

using namespace seabass::application;
using namespace seabass::infrastructure::stick_backup;
using seabass::infrastructure::hashing::Sha256;
namespace fs = std::filesystem;

namespace
{

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

void writeFile(const fs::path &p, const std::string &content, std::int64_t mtime)
{
    fs::create_directories(p.parent_path());
    {
        std::ofstream out(p, std::ios::binary);
        out << content;
    }
    fs::last_write_time(p, fromUnixSeconds(mtime));
}

std::string readFile(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void createEngineDb(const fs::path &path)
{
    fs::create_directories(path.parent_path());
    sqlite3 *db = nullptr;
    assert(sqlite3_open(path.string().c_str(), &db) == SQLITE_OK);
    char *error = nullptr;
    assert(sqlite3_exec(db, "CREATE TABLE Track(id INTEGER PRIMARY KEY, path TEXT); INSERT INTO Track(path) VALUES('Contents/a.mp3')",
                        nullptr, nullptr, &error) == SQLITE_OK);
    sqlite3_close(db);
}

// path -> content for files, "<dir>" for directories, of everything the
// walker would back up.
std::map<std::string, std::string> snapshot(const fs::path &root)
{
    std::map<std::string, std::string> out;
    for (const TreeEntry &e : walkStickTree(root, CancellationToken::none()).entries) {
        out[e.relativePath] = e.isDirectory ? "<dir>" : readFile(root / pathFromUtf8(e.relativePath));
    }
    return out;
}

std::size_t tempFilesUnder(const fs::path &root)
{
    std::size_t n = 0;
    for (const auto &entry : fs::recursive_directory_iterator(root)) {
        if (entry.path().filename().string().find(".seabass-restore-tmp") != std::string::npos) {
            ++n;
        }
    }
    return n;
}

struct Fixture
{
    fs::path root;
    fs::path stick;
    fs::path archive;
    fs::path target;
    BackupStickOptions backup;
    RestoreOptions restore;

    explicit Fixture(const std::string &name)
        : root(fs::temp_directory_path() / ("seabass_backup_restore_test_" + name)), stick(root / "stick"),
          archive(root / "Seabass Backups" / "STICK.zip"), target(root / "target")
    {
        fs::remove_all(root);
        writeFile(stick / "Contents" / "a.mp3", pseudoRandom(90'000, 1), 1'700'000'000);
        writeFile(stick / "Contents" / "Sub" / "b.mp3", pseudoRandom(40'000, 2), 1'700'000'001);
        writeFile(stick / "Contents" / "Caf\xC3\xA9.mp3", pseudoRandom(3'000, 3), 1'700'000'002);
        writeFile(stick / "PIONEER" / "rekordbox" / "export.pdb", pseudoRandom(4096, 4), 1'700'000'003);
        writeFile(stick / "empty.txt", "", 1'700'000'004);
        createEngineDb(stick / "Engine Library" / "Database2" / "m.db");
        fs::create_directories(stick / "Engine Library" / "Music");
        const char *loopback = std::getenv("SEABASS_LOOPBACK_MOUNT");
        if (loopback != nullptr && *loopback != '\0') {
            target = fs::path(loopback) / ("seabass_restore_" + name);
            fs::remove_all(target);
        }
        fs::create_directories(target);
        backup.stickRoot = stick;
        backup.archivePath = archive;
        backup.stickIdentifier = "uuid";
        backup.stickLabel = "STICK";
        restore.archivePath = archive;
        restore.targetRoot = target;
    }
    ~Fixture()
    {
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::remove_all(target, ec);
    }
};

}  // namespace

int main()
{
    // ---- Sanitizer ----
    {
        std::string reason;
        bool isDir = false;
        assert(sanitizeEntryName("Contents/Sub/b.mp3", TargetOs::Posix, &reason) == fs::path("Contents") / "Sub" / "b.mp3");
        assert(sanitizeEntryName("dir/", TargetOs::Posix, &reason, &isDir) == fs::path("dir") && isDir);
        for (const char *bad : {"", "/", "../x", "a/../b", "/abs", "C:/x", "c:x", "a//b", "a/./b", "back\\slash", "."}) {
            assert(!sanitizeEntryName(bad, TargetOs::Posix, &reason).has_value());
            assert(!sanitizeEntryName(bad, TargetOs::Windows, &reason).has_value());
        }
        std::string nul("nu\0l", 4);
        assert(!sanitizeEntryName(nul, TargetOs::Posix, &reason).has_value());
        for (const char *windowsOnly : {"aux.mp3", "COM1", "Music/nul", "trailing.", "trailing ", "bad:name", "q?", "a<b", "pipe|x"}) {
            assert(sanitizeEntryName(windowsOnly, TargetOs::Posix, &reason).has_value());
            assert(!sanitizeEntryName(windowsOnly, TargetOs::Windows, &reason).has_value());
        }
        assert(sanitizeEntryName("auxiliary.mp3", TargetOs::Windows, &reason).has_value());
        assert(sanitizeEntryName("Contents/Caf\xC3\xA9.mp3", TargetOs::Windows, &reason).has_value());
        std::cout << "case 1 (entry-name sanitizer: traversal, absolute, drive letters, backslash, NUL, Windows reserved names) OK\n";
    }

    // ---- Round trip: backup, restore, zero changes ----
    {
        Fixture f("roundtrip");
        assert(BackupStick::execute(f.backup).status == BackupOutcomeStatus::Complete);
        std::map<std::string, std::string> expected = snapshot(f.stick);

        RestorePreview preview = RestoreStickBackup::preview(f.restore);
        assert(preview.error.empty());
        assert(preview.stickLabel == "STICK" && preview.status == BackupStatus::Complete);
        assert(preview.filesToWrite == 6 && preview.filesUnchanged == 0 && preview.rejected.empty());
        assert(preview.extras == 0 && !preview.targetHasEngineLibrary);

        RestoreSummary summary = RestoreStickBackup::execute(f.restore);
        assert(summary.status == RestoreSummary::Status::Restored);
        assert(summary.filesWritten == 6 && summary.writeErrors.empty() && summary.rejected.empty());
        assert(snapshot(f.target) == expected);
        for (const TreeEntry &e : walkStickTree(f.stick, CancellationToken::none()).entries) {
            if (!e.isDirectory) {
                std::int64_t restored = toUnixSeconds(fs::last_write_time(f.target / pathFromUtf8(e.relativePath)));
                assert(std::llabs(restored - e.mtimeUnix) <= 2);
            }
        }
        assert(tempFilesUnder(f.target) == 0);

        // The property everything hinges on: a backup taken from the
        // restored tree finds nothing to do.
        BackupStickOptions again = f.backup;
        again.stickRoot = f.target;
        BackupPreview zero = BackupStick::preview(again);
        assert(zero.added == 0 && zero.changed == 0 && zero.removed == 0 && !zero.databaseChanged);
        std::cout << "case 2 (restore is byte-, name- and mtime-exact; re-backup of the restored tree sees zero changes) OK\n";

        RestoreSummary repeat = RestoreStickBackup::execute(f.restore);
        assert(repeat.status == RestoreSummary::Status::Restored);
        assert(repeat.filesWritten == 0 && repeat.filesUnchanged == 6);
        std::cout << "case 3 (restoring again writes nothing) OK\n";
    }

    // ---- Overlay vs exact ----
    {
        Fixture f("modes");
        assert(BackupStick::execute(f.backup).status == BackupOutcomeStatus::Complete);
        assert(RestoreStickBackup::execute(f.restore).status == RestoreSummary::Status::Restored);
        writeFile(f.target / "Contents" / "a.mp3", "tampered", 1'700'500'000);
        fs::remove(f.target / "empty.txt");
        writeFile(f.target / "Contents" / "extra.mp3", pseudoRandom(1000, 9), 1'700'500'001);
        fs::create_directories(f.target / "Extra Dir");

        RestorePreview preview = RestoreStickBackup::preview(f.restore);
        assert(preview.filesToWrite == 2 && preview.extras == 2 && preview.targetHasEngineLibrary);

        RestoreSummary overlay = RestoreStickBackup::execute(f.restore);
        assert(overlay.status == RestoreSummary::Status::Restored);
        assert(overlay.filesWritten == 2 && overlay.extrasRemoved == 0);
        assert(readFile(f.target / "Contents" / "a.mp3") == readFile(f.stick / "Contents" / "a.mp3"));
        assert(fs::exists(f.target / "Contents" / "extra.mp3") && fs::exists(f.target / "Extra Dir"));

        RestoreOptions exact = f.restore;
        exact.exact = true;
        RestoreSummary mirrored = RestoreStickBackup::execute(exact);
        assert(mirrored.status == RestoreSummary::Status::Restored);
        assert(mirrored.filesWritten == 0 && mirrored.extrasRemoved == 2);
        assert(!fs::exists(f.target / "Contents" / "extra.mp3") && !fs::exists(f.target / "Extra Dir"));
        assert(snapshot(f.target) == snapshot(f.stick));
        std::cout << "case 4 (overlay keeps extras and repairs differences; exact removes extras) OK\n";
    }

    // ---- Library check plumbing and problem reporting ----
    {
        Fixture f("check");
        assert(BackupStick::execute(f.backup).status == BackupOutcomeStatus::Complete);
        f.restore.libraryCheck = [](const fs::path &) { return std::optional<std::vector<std::string>>{{"Contents/missing.mp3"}}; };
        RestoreSummary summary = RestoreStickBackup::execute(f.restore);
        assert(summary.status == RestoreSummary::Status::RestoredWithProblems);
        assert(summary.missingTrackPaths && summary.missingTrackPaths->size() == 1);
        std::cout << "case 5 (post-restore library check result surfaces as a problem) OK\n";
    }

    // ---- Adversarial archive: bad names rejected, nothing escapes the target ----
    {
        Fixture f("adversarial");
        std::string good = "harmless", evil = "should never land";
        fs::create_directories(f.archive.parent_path());
        {
            PosixArchiveFile file(f.archive, PosixArchiveFile::OpenMode::ReadWrite);
            Zip64Writer writer(file, {});
            BackupManifest manifest;
            manifest.stickLabel = "EVIL";
            manifest.createdAtUnix = 1'757'000'000;
            for (auto [name, content] : {std::pair{"ok/good.txt", good}, std::pair{"../evil.txt", evil}, std::pair{"/abs.txt", evil}}) {
                seabass::infrastructure::hashing::Sha256Digest sha;
                CentralEntry e = writer.addFileFromMemory(name, 1'700'000'000, zip::bytesOf(content), &sha);
                manifest.rows.push_back({ManifestRow::Kind::File, name, e.size, e.mtimeUnix, sha, "", e.crc32});
            }
            writer.finish(manifest.serialize(), std::string(ManifestEntryName), manifest.createdAtUnix);
            file.barrier();
        }
        RestorePreview preview = RestoreStickBackup::preview(f.restore);
        assert(preview.rejected.size() == 2 && preview.filesToWrite == 1);
        RestoreSummary summary = RestoreStickBackup::execute(f.restore);
        assert(summary.status == RestoreSummary::Status::RestoredWithProblems);
        assert(summary.rejected.size() == 2 && summary.filesWritten == 1);
        assert(readFile(f.target / "ok" / "good.txt") == good);
        assert(!fs::exists(f.target.parent_path() / "evil.txt") && !fs::exists(f.target / "evil.txt") && !fs::exists("/abs.txt"));
        std::cout << "case 6 (traversal and absolute names rejected and reported; the good entry restored) OK\n";
    }

    // ---- Damaged data is never written ----
    {
        Fixture f("damaged");
        assert(BackupStick::execute(f.backup).status == BackupOutcomeStatus::Complete);
        {
            PosixArchiveFile file(f.archive, PosixArchiveFile::OpenMode::ReadWrite);
            Zip64Reader reader = Zip64Reader::open(file);
            std::size_t victim = *reader.findEntry("Contents/a.mp3");
            std::uint64_t at = reader.dataOffset(victim) + 1000;
            std::vector<std::byte> one(1);
            file.readAt(at, one);
            // No writeAt on purpose -- rewrite the file with one flipped byte.
            std::string bytes = readFile(f.archive);
            bytes[static_cast<std::size_t>(at)] ^= 0x01;
            std::ofstream(f.archive, std::ios::binary) << bytes;
        }
        RestoreSummary summary = RestoreStickBackup::execute(f.restore);
        assert(summary.status == RestoreSummary::Status::RestoredWithProblems);
        assert(summary.writeErrors.size() == 1 && summary.writeErrors[0].find("Contents/a.mp3") != std::string::npos);
        assert(!fs::exists(f.target / "Contents" / "a.mp3"));
        assert(fs::exists(f.target / "Contents" / "Sub" / "b.mp3"));
        assert(tempFilesUnder(f.target) == 0);
        std::cout << "case 7 (a damaged entry is reported and not written; the rest is) OK\n";
    }

    // ---- Cancel leaves no half-written file ----
    {
        Fixture f("cancel");
        assert(BackupStick::execute(f.backup).status == BackupOutcomeStatus::Complete);
        f.restore.onProgress = [&](const RestoreProgress &p) {
            if (p.phase == RestoreProgress::Phase::Writing && p.filesDone == 2) {
                f.restore.cancel.cancel();
            }
        };
        RestoreSummary summary = RestoreStickBackup::execute(f.restore);
        assert(summary.status == RestoreSummary::Status::Cancelled);
        assert(summary.filesWritten == 2);
        assert(tempFilesUnder(f.target) == 0);
        std::cout << "case 8 (cancelled restore: completed files intact, no temp files) OK\n";
    }

    // ---- A compacted archive restores identically ----
    {
        Fixture f("compacted");
        assert(BackupStick::execute(f.backup).status == BackupOutcomeStatus::Complete);
        writeFile(f.stick / "Contents" / "a.mp3", pseudoRandom(95'000, 5), 1'700'100'000);
        assert(BackupStick::execute(f.backup).status == BackupOutcomeStatus::Complete);
        CompactStickBackupOptions compact;
        compact.archivePath = f.archive;
        assert(CompactStickBackup::execute(compact).status == CompactionOutcome::Status::Compacted);
        assert(RestoreStickBackup::execute(f.restore).status == RestoreSummary::Status::Restored);
        assert(snapshot(f.target) == snapshot(f.stick));
        std::cout << "case 9 (restore from a compacted archive) OK\n";
    }

    std::cout << "all cases passed\n";
    return 0;
}
