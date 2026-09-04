#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "domain/track.hpp"

namespace seabass::gui
{

// Caches the result of scanning a catalog (rekordbox/engine/onelibrary),
// keyed on (format, path), so the many controllers that each independently
// call ScanLibrary today (8 controllers, 26 call sites at the time this was
// written) can share one real disk read per catalog per session instead of
// re-scanning the same removable-media data on every page open.
//
// Lives in gui/, not domain/application: constructing the right
// infrastructure reader for a given format string is exactly the job every
// controller's own scan-task function already does (see e.g.
// SyncController::runAnalyzeTask), just consolidated here instead of
// duplicated per controller.
//
// tracksFor() is blocking -- call it from whatever background thread
// (QtConcurrent::run task) used to call ScanLibrary directly, same as
// before. It is safe to call concurrently from multiple threads for
// different (or the same) keys: a second caller for a key another thread
// is already scanning waits for that scan to finish rather than triggering
// a redundant one.
//
// Freshness is checked by comparing the catalog's own database file's
// mtime against what was cached; a write elsewhere (Sync's apply(), Clean
// Up, ...) should call invalidate() explicitly right after, rather than
// relying purely on the next mtime check, so a caller never has to wait
// out a filesystem timestamp granularity window to see its own write.
//
// Note: unlike the per-controller scan tasks it replaces, a cache-miss
// scan here has no progress reporting wired through it (no ProgressReporter
// plumbed to whichever controller triggered it) -- a real simplification,
// not an oversight. Reintroducing per-call progress would need the cache
// to know which controller's progress properties to update, which breaks
// the whole point of sharing one cache across controllers that don't know
// about each other.
class LibraryCatalogCache
{
public:
    using ScanFn = std::function<std::vector<domain::Track>(const std::string &format, const std::string &path)>;
    using MtimeFn =
        std::function<std::chrono::system_clock::time_point(const std::string &format, const std::string &path)>;

    // Real behavior: constructs the matching infrastructure reader
    // ("rekordbox"/"engine"/"onelibrary") and stats that catalog's own
    // database file.
    LibraryCatalogCache();
    // Test seam: inject fakes so cache hit/miss/invalidate/concurrency
    // behavior can be verified without real stick data or real filesystem
    // timestamps.
    LibraryCatalogCache(ScanFn scanFn, MtimeFn mtimeFn);

    std::vector<domain::Track> tracksFor(const std::string &format, const std::string &path);

    // Call after writing to this catalog (Sync's apply()/applyOne(), Clean
    // Up writes, ...) so the next tracksFor() re-scans unconditionally
    // instead of trusting a possibly-stale mtime comparison.
    void invalidate(const std::string &format, const std::string &path);

private:
    struct Entry
    {
        std::vector<domain::Track> tracks;
        std::chrono::system_clock::time_point mtime;
        bool valid = false;
        bool inProgress = false;
    };

    static std::string keyFor(const std::string &format, const std::string &path);

    ScanFn m_scanFn;
    MtimeFn m_mtimeFn;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::unordered_map<std::string, Entry> m_entries;
};

}  // namespace seabass::gui
