#pragma once

#include <string>
#include <vector>

namespace djconvert::infrastructure::cleanup
{

struct PendingDeletion
{
    std::string timestampUtc;  // ISO 8601, set by append()
    std::string format;        // "rekordbox" or "engine"
    std::string filePath;
    std::string title;
    std::string artist;
    std::string backupId;  // the backup covering the DB edit that orphaned this file
};

// Append-only "garbage bin": a durable record of audio files a duplicate
// cleanup's database edit has orphaned (their track/playlist rows
// removed) but that haven't actually been deleted from disk -- so a
// later pass can review and act on them instead of that information
// being lost. One JSON object per line, human-inspectable like
// FileOperationLog but structured so it can be parsed back reliably.
// Nothing calls this yet (no Clean Up screen exists); it's proven by
// its own test so it's ready for that caller.
class PendingDeletionManifest
{
public:
    explicit PendingDeletionManifest(std::string manifestPath);

    // Sets entry.timestampUtc to now and appends it as one line.
    void append(PendingDeletion entry);

    std::vector<PendingDeletion> list() const;

private:
    std::string m_manifestPath;
};

}  // namespace djconvert::infrastructure::cleanup
