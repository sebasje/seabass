#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "infrastructure/cleanup/pending_deletion_applier.hpp"

using namespace seabass::infrastructure::cleanup;
namespace fs = std::filesystem;

namespace
{

void touch(const fs::path &path)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path) << "audio data";
}

PendingDeletion makeEntry(const std::string &filePath, const std::string &backupId = "backup-1")
{
    PendingDeletion e;
    e.format = "rekordbox";
    e.filePath = filePath;
    e.title = "Some Track";
    e.artist = "Some Artist";
    e.backupId = backupId;
    return e;
}

}  // namespace

int main()
{
    fs::path root = fs::temp_directory_path() / "seabass_pending_deletion_applier_test";
    fs::remove_all(root);
    fs::create_directories(root);
    fs::path manifestPath = root / ".seabass-pending-deletions.jsonl";

    // A real file on disk is actually deleted, and cleared from the
    // manifest -- this is the one place in the app that permanently
    // destroys real audio content, so this is the test that matters
    // most: the file must genuinely be gone afterward, not just
    // reported as gone.
    {
        fs::remove(manifestPath);
        fs::path filePath = root / "orphaned.mp3";
        touch(filePath);
        PendingDeletionManifest manifest(manifestPath.string());
        manifest.append(makeEntry(filePath.string()));

        auto outcomes = applyPendingDeletions(manifest.list(), manifest);

        assert(outcomes.size() == 1);
        assert(outcomes[0].status == PendingDeletionOutcome::Status::Deleted);
        assert(!fs::exists(filePath));  // genuinely gone from disk
        assert(manifest.list().empty());  // and cleared from the manifest
        std::cout << "case 1 (real file: actually deleted from disk, cleared from manifest) OK\n";
    }

    // A file already gone from disk (e.g. removed by hand since the
    // manifest entry was recorded) is treated as done, not an error --
    // still cleared from the manifest, since there's nothing left to
    // delete.
    {
        fs::remove(manifestPath);
        fs::path filePath = root / "already_gone.mp3";
        PendingDeletionManifest manifest(manifestPath.string());
        manifest.append(makeEntry(filePath.string()));
        assert(!fs::exists(filePath));  // never created

        auto outcomes = applyPendingDeletions(manifest.list(), manifest);

        assert(outcomes.size() == 1);
        assert(outcomes[0].status == PendingDeletionOutcome::Status::AlreadyAbsent);
        assert(manifest.list().empty());
        std::cout << "case 2 (already-absent file: treated as done, cleared from manifest) OK\n";
    }

    // A file that exists but can't actually be deleted (its parent
    // directory has no write permission) fails, and -- critically --
    // stays in the manifest so a later pass can retry it, rather than
    // being silently forgotten.
    {
        fs::remove(manifestPath);
        fs::path lockedDir = root / "locked";
        fs::create_directories(lockedDir);
        fs::path filePath = lockedDir / "cant_delete.mp3";
        touch(filePath);
        fs::permissions(lockedDir, fs::perms::owner_read | fs::perms::owner_exec);

        PendingDeletionManifest manifest(manifestPath.string());
        manifest.append(makeEntry(filePath.string()));

        auto outcomes = applyPendingDeletions(manifest.list(), manifest);

        fs::permissions(lockedDir, fs::perms::owner_all);  // restore so cleanup can remove it below

        assert(outcomes.size() == 1);
        assert(outcomes[0].status == PendingDeletionOutcome::Status::Failed);
        assert(!outcomes[0].failureReason.empty());
        assert(fs::exists(filePath));  // genuinely untouched
        assert(manifest.list().size() == 1);  // stays in the manifest for a future retry
        std::cout << "case 3 (undeletable file: fails safely, stays in the manifest for retry) OK\n";
    }

    // A mixed batch: only the entries that actually get processed
    // (deleted or already-absent) are cleared -- an untouched entry
    // (e.g. one resolvePendingDeletions() would have excluded, deliberately
    // never passed in here) is left alone in the manifest.
    {
        fs::remove(manifestPath);
        fs::path deletableFile = root / "batch_deletable.mp3";
        touch(deletableFile);
        fs::path keepFile = root / "batch_still_referenced.mp3";
        touch(keepFile);

        PendingDeletionManifest manifest(manifestPath.string());
        manifest.append(makeEntry(deletableFile.string()));
        manifest.append(makeEntry(keepFile.string()));  // simulates an entry NOT passed to applyPendingDeletions

        std::vector<PendingDeletion> toDelete = {makeEntry(deletableFile.string())};
        auto outcomes = applyPendingDeletions(toDelete, manifest);

        assert(outcomes.size() == 1);
        assert(outcomes[0].status == PendingDeletionOutcome::Status::Deleted);
        assert(!fs::exists(deletableFile));
        assert(fs::exists(keepFile));  // untouched -- never in the deletion list

        auto remaining = manifest.list();
        assert(remaining.size() == 1);
        assert(remaining[0].filePath == keepFile.string());  // only the processed entry was cleared
        std::cout << "case 4 (mixed batch: only processed entries cleared, others left in the manifest) OK\n";
    }

    std::cout << "all cases passed\n";
    return 0;
}
