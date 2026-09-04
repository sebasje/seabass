#include "gui/library_catalog_cache.hpp"

#include <filesystem>
#include <stdexcept>

#include "application/use_cases/scan_library.hpp"
#include "infrastructure/engine/libdjinterop_engine_reader.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/onelibrary/onelibrary_reader.hpp"
#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"

namespace seabass::gui
{

namespace
{

namespace fs = std::filesystem;

// The catalog file whose mtime stands in for "has this catalog changed
// since it was last scanned" -- same files SyncController::runAnalyzeTask
// already checks for its own (narrower) staleness purposes.
fs::path freshnessFile(const std::string &format, const std::string &path)
{
    if (format == "rekordbox") {
        return fs::path(path) / "rekordbox" / "export.pdb";
    }
    if (format == "engine") {
        return fs::path(path) / "Database2" / "m.db";
    }
    if (format == "onelibrary") {
        return infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(path);
    }
    throw std::invalid_argument("LibraryCatalogCache: unknown format \"" + format + "\"");
}

std::chrono::system_clock::time_point realMtime(const std::string &format, const std::string &path)
{
    return std::chrono::clock_cast<std::chrono::system_clock>(fs::last_write_time(freshnessFile(format, path)));
}

std::vector<domain::Track> realScan(const std::string &format, const std::string &path,
                                     application::ProgressReporter &progress)
{
    if (format == "rekordbox") {
        infrastructure::rekordbox::KaitaiRekordboxReader reader(path);
        reader.setProgressReporter(progress);
        return application::ScanLibrary(reader).execute();
    }
    if (format == "engine") {
        infrastructure::engine::LibdjinteropEngineReader reader(path);
        reader.setProgressReporter(progress);
        return application::ScanLibrary(reader).execute();
    }
    if (format == "onelibrary") {
        infrastructure::onelibrary::OneLibraryReader reader(path);
        reader.setProgressReporter(progress);
        return application::ScanLibrary(reader).execute();
    }
    throw std::invalid_argument("LibraryCatalogCache: unknown format \"" + format + "\"");
}

}  // namespace

LibraryCatalogCache &LibraryCatalogCache::instance()
{
    static LibraryCatalogCache cache;
    return cache;
}

LibraryCatalogCache::LibraryCatalogCache() : m_scanFn(realScan), m_mtimeFn(realMtime) {}

LibraryCatalogCache::LibraryCatalogCache(ScanFn scanFn, MtimeFn mtimeFn)
    : m_scanFn(std::move(scanFn)), m_mtimeFn(std::move(mtimeFn))
{
}

std::string LibraryCatalogCache::keyFor(const std::string &format, const std::string &path)
{
    return format + "\n" + path;
}

std::vector<domain::Track> LibraryCatalogCache::tracksFor(const std::string &format, const std::string &path,
                                                            application::ProgressReporter &progress)
{
    const std::string key = keyFor(format, path);
    const auto currentMtime = m_mtimeFn(format, path);

    std::unique_lock<std::mutex> lock(m_mutex);

    // If another thread is already scanning this exact key, wait for it
    // to finish rather than also triggering a scan -- the two most likely
    // near-simultaneous callers (e.g. Stick Statistics kicking off three
    // scans right as Sync Cue Points also opens) would otherwise both pay
    // the real disk-read cost.
    m_cv.wait(lock, [&] {
        auto it = m_entries.find(key);
        return it == m_entries.end() || !it->second.inProgress;
    });

    auto it = m_entries.find(key);
    if (it != m_entries.end() && it->second.valid && it->second.mtime == currentMtime) {
        return it->second.tracks;
    }

    m_entries[key].inProgress = true;
    lock.unlock();

    std::vector<domain::Track> tracks;
    std::exception_ptr error;
    try {
        tracks = m_scanFn(format, path, progress);
    } catch (...) {
        error = std::current_exception();
    }

    lock.lock();
    Entry &entry = m_entries[key];
    entry.inProgress = false;
    if (!error) {
        entry.tracks = tracks;
        entry.mtime = currentMtime;
        entry.valid = true;
    }
    // On error, entry is left exactly as it was: a failed re-scan
    // shouldn't evict a previously-good cached result out from under a
    // concurrent waiter that's about to read it.
    lock.unlock();
    m_cv.notify_all();

    if (error) {
        std::rethrow_exception(error);
    }
    return tracks;
}

void LibraryCatalogCache::invalidate(const std::string &format, const std::string &path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.erase(keyFor(format, path));
}

}  // namespace seabass::gui
