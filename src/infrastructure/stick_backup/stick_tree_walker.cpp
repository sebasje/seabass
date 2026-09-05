#include "infrastructure/stick_backup/stick_tree_walker.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <system_error>

#include "infrastructure/engine/engine_library_layout.hpp"

namespace seabass::infrastructure::stick_backup
{

namespace fs = std::filesystem;

namespace
{

constexpr std::array<std::string_view, 5> ExcludedRootDirectories = {
    "System Volume Information", "$RECYCLE.BIN", ".Trashes", ".Spotlight-V100", ".fseventsd",
};
constexpr std::string_view WriteLockPath = ".seabass-backups/.write.lock";

std::string_view firstComponent(std::string_view relativePath)
{
    std::size_t slash = relativePath.find('/');
    return slash == std::string_view::npos ? relativePath : relativePath.substr(0, slash);
}

std::string_view lastComponent(std::string_view relativePath)
{
    std::size_t slash = relativePath.rfind('/');
    return slash == std::string_view::npos ? relativePath : relativePath.substr(slash + 1);
}

}  // namespace

bool isExcludedFromBackup(std::string_view relativePath, bool isDirectory)
{
    std::string_view first = firstComponent(relativePath);
    for (std::string_view excluded : ExcludedRootDirectories) {
        if (first == excluded) {
            return true;
        }
    }
    if (relativePath == WriteLockPath) {
        return true;
    }
    return !isDirectory && engine::isSqliteShmFile(lastComponent(relativePath));
}

std::int64_t toUnixSeconds(fs::file_time_type time)
{
    using namespace std::chrono;
    auto sys = clock_cast<system_clock>(time);
    return duration_cast<seconds>(sys.time_since_epoch()).count();
}

fs::file_time_type fromUnixSeconds(std::int64_t secondsSinceEpoch)
{
    using namespace std::chrono;
    system_clock::time_point sys{seconds(secondsSinceEpoch)};
    return clock_cast<fs::file_time_type::clock>(sys);
}

std::string pathToUtf8(const fs::path &path)
{
    std::u8string u8 = path.generic_u8string();
    return std::string(reinterpret_cast<const char *>(u8.data()), u8.size());
}

fs::path pathFromUtf8(std::string_view utf8)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
}

TreeWalk walkStickTree(const fs::path &root, application::CancellationToken cancel)
{
    TreeWalk walk;
    std::error_code ec;
    // Lexical, not fs::relative(): the latter canonicalizes, which follows
    // symlinks (a link would be recorded under its target's name) and
    // costs a realpath per entry.
    fs::path rootNormalized = root.lexically_normal();
    if (rootNormalized.filename().empty()) {
        rootNormalized = rootNormalized.parent_path();
    }
    fs::recursive_directory_iterator it(rootNormalized, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        walk.skipped.push_back(root.string() + ": " + ec.message());
        return walk;
    }
    std::size_t sinceCancelCheck = 0;
    for (fs::recursive_directory_iterator end; it != end; it.increment(ec)) {
        if (ec) {
            walk.skipped.push_back(it->path().string() + ": " + ec.message());
            ec.clear();
            continue;
        }
        if (++sinceCancelCheck >= 256) {
            sinceCancelCheck = 0;
            if (cancel.cancelled()) {
                walk.cancelled = true;
                break;
            }
        }
        const fs::directory_entry &entry = *it;
        std::string relative = pathToUtf8(entry.path().lexically_relative(rootNormalized));
        if (relative.empty() || relative == "." || relative.compare(0, 2, "..") == 0) {
            walk.skipped.push_back(entry.path().string() + ": could not relativize");
            continue;
        }

        std::error_code statusEc;
        fs::file_status linkStatus = entry.symlink_status(statusEc);
        if (statusEc) {
            walk.skipped.push_back(relative + ": " + statusEc.message());
            it.disable_recursion_pending();
            continue;
        }
        if (fs::is_symlink(linkStatus)) {
            walk.skipped.push_back(relative + ": symbolic link");
            it.disable_recursion_pending();
            continue;
        }
        const bool isDirectory = fs::is_directory(linkStatus);
        if (isExcludedFromBackup(relative, isDirectory)) {
            if (isDirectory) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!isDirectory && !fs::is_regular_file(linkStatus)) {
            walk.skipped.push_back(relative + ": not a regular file");
            continue;
        }

        TreeEntry treeEntry;
        treeEntry.relativePath = std::move(relative);
        treeEntry.isDirectory = isDirectory;
        fs::file_time_type mtime = entry.last_write_time(statusEc);
        if (statusEc) {
            walk.skipped.push_back(treeEntry.relativePath + ": " + statusEc.message());
            if (isDirectory) {
                it.disable_recursion_pending();
            }
            continue;
        }
        treeEntry.mtimeUnix = toUnixSeconds(mtime);
        if (!isDirectory) {
            treeEntry.size = entry.file_size(statusEc);
            if (statusEc) {
                walk.skipped.push_back(treeEntry.relativePath + ": " + statusEc.message());
                continue;
            }
            walk.totalFileBytes += treeEntry.size;
        }
        walk.entries.push_back(std::move(treeEntry));
    }
    std::sort(walk.entries.begin(), walk.entries.end(),
              [](const TreeEntry &a, const TreeEntry &b) { return a.relativePath < b.relativePath; });
    return walk;
}

}  // namespace seabass::infrastructure::stick_backup
