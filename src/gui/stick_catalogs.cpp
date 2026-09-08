#include "gui/stick_catalogs.hpp"

#include <filesystem>

#include "gui/library_catalog_cache.hpp"
#include "infrastructure/engine/engine_library_layout.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/stick_layout.hpp"

namespace seabass::gui
{

namespace fs = std::filesystem;

StickCatalogRead readAllStickCatalogs(const std::string &libraryPath, application::ProgressReporter &progress,
                                        const application::CancellationToken &cancel)
{
    StickCatalogRead result;
    if (libraryPath.empty()) {
        return result;
    }

    const fs::path root = fs::path(libraryPath).parent_path();
    const fs::path pioneerRoot = root / "PIONEER";

    auto read = [&](const char *format, const std::string &path,
                    std::optional<std::vector<domain::Track>> &into) {
        try {
            into = LibraryCatalogCache::instance().tracksFor(format, path, progress, cancel);
        } catch (const application::OperationCancelled &) {
            throw;  // a cancelled scan is not a failed catalog
        } catch (const std::exception &) {
            result.failed.emplace_back(format);
        }
    };

    std::error_code ec;
    if (fs::exists(pioneerRoot / "rekordbox" / "export.pdb", ec)) {
        read("rekordbox", pioneerRoot.string(), result.catalogs.rekordbox);
    }
    if (fs::exists(infrastructure::engine::engineMainDatabasePath(root), ec)) {
        read("engine", infrastructure::engine::engineLibraryPath(root).string(), result.catalogs.engine);
    }
    if (infrastructure::onelibrary::OneLibraryCueWriter::existsFor(pioneerRoot.string())) {
        read("onelibrary", pioneerRoot.string(), result.catalogs.oneLibrary);
    }
    return result;
}

}  // namespace seabass::gui
