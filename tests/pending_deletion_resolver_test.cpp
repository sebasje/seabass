#include <cassert>
#include <iostream>
#include <vector>

#include "infrastructure/cleanup/pending_deletion_resolver.hpp"

using namespace djconvert::domain;
using namespace djconvert::infrastructure::cleanup;

namespace
{

PendingDeletion makePending(std::string filePath, std::string title)
{
    PendingDeletion p;
    p.format = "rekordbox";
    p.filePath = std::move(filePath);
    p.title = std::move(title);
    p.artist = "Artist";
    p.backupId = "20260101T000000-duplicate-file-cleanup";
    return p;
}

Track makeTrack(std::string filePath)
{
    Track t;
    t.sourceId = "1";
    t.filePath = std::move(filePath);
    return t;
}

}  // namespace

int main()
{
    // A pending entry whose file is genuinely unreferenced by any current
    // track is safe to delete -- this is the exact bug-shaped scenario:
    // the file really is an orphaned duplicate.
    {
        std::vector<PendingDeletion> pending = {makePending("/stick/Contents/dup.mp3", "Duplicate Track")};
        std::vector<Track> current = {makeTrack("/stick/Contents/survivor.mp3")};

        auto result = resolvePendingDeletions(pending, current);
        assert(result.safeToDelete.size() == 1);
        assert(result.safeToDelete[0].filePath == "/stick/Contents/dup.mp3");
        assert(result.stillReferenced.empty());
        std::cout << "case 1 (unreferenced file -> safe to delete) OK\n";
    }

    // A pending entry whose file path is STILL referenced by a current
    // track (the manifest is stale, or something re-pointed at it since)
    // must never be deleted, no matter what the manifest says.
    {
        std::vector<PendingDeletion> pending = {makePending("/stick/Contents/still-used.mp3", "Still Used")};
        std::vector<Track> current = {makeTrack("/stick/Contents/still-used.mp3")};

        auto result = resolvePendingDeletions(pending, current);
        assert(result.safeToDelete.empty());
        assert(result.stillReferenced.size() == 1);
        assert(result.stillReferenced[0].filePath == "/stick/Contents/still-used.mp3");
        std::cout << "case 2 (still-referenced file -> left alone, reported) OK\n";
    }

    // Path-separator differences between how the manifest recorded a
    // path and how a fresh scan reports it still match -- Windows paths
    // round-trip through both styles depending on which code path
    // produced them.
    {
        std::vector<PendingDeletion> pending = {makePending("C:\\Stick\\Contents\\dup.mp3", "Dup")};
        std::vector<Track> current = {makeTrack("C:/Stick/Contents/dup.mp3")};

        auto result = resolvePendingDeletions(pending, current);
        assert(result.safeToDelete.empty());
        assert(result.stillReferenced.size() == 1);
        std::cout << "case 3 (path-separator-insensitive matching) OK\n";
    }

    // An entry with no resolved file path at all is left alone rather
    // than guessed at -- nothing to safely verify against.
    {
        std::vector<PendingDeletion> pending = {makePending("", "No Path")};
        std::vector<Track> current;

        auto result = resolvePendingDeletions(pending, current);
        assert(result.safeToDelete.empty());
        assert(result.stillReferenced.size() == 1);
        std::cout << "case 4 (empty filePath -> left alone, never guessed at) OK\n";
    }

    // A mixed batch classifies each entry independently.
    {
        std::vector<PendingDeletion> pending = {
            makePending("/stick/Contents/orphan-a.mp3", "Orphan A"),
            makePending("/stick/Contents/used.mp3", "Used"),
            makePending("/stick/Contents/orphan-b.mp3", "Orphan B"),
        };
        std::vector<Track> current = {makeTrack("/stick/Contents/used.mp3")};

        auto result = resolvePendingDeletions(pending, current);
        assert(result.safeToDelete.size() == 2);
        assert(result.stillReferenced.size() == 1);
        assert(result.stillReferenced[0].filePath == "/stick/Contents/used.mp3");
        std::cout << "case 5 (mixed batch classified independently) OK\n";
    }

    std::cout << "all cases passed\n";
    return 0;
}
