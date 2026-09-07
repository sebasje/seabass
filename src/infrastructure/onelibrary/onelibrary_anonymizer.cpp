#include "infrastructure/onelibrary/onelibrary_anonymizer.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <vector>

#include "infrastructure/anonymization_placeholder.hpp"
#include "infrastructure/onelibrary/onelibrary_key.hpp"
#include "infrastructure/onelibrary/sqlcipher_dyn.hpp"

namespace seabass::infrastructure::onelibrary
{

namespace fs = std::filesystem;

namespace
{

std::string basenameOf(const std::string &path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Which columns actually exist. The schema has changed across Device
// Library Plus versions, and a scrub that assumes a column is there fails
// the whole export on a library that predates it.
std::set<std::string> columnsOf(const SqlCipherDb &db, const std::string &table)
{
    std::set<std::string> columns;
    try {
        SqlCipherStatement stmt(db, "PRAGMA table_info(" + table + ")");
        while (stmt.step()) {
            columns.insert(stmt.columnText(1));
        }
    } catch (const std::exception &) {
        // No such table in this schema version.
    }
    return columns;
}

bool tableExists(const SqlCipherDb &db, const std::string &table)
{
    SqlCipherStatement stmt(db, "SELECT count(*) FROM sqlite_master WHERE type='table' AND name=?");
    stmt.bindText(1, table);
    return stmt.step() && stmt.columnInt64(0) > 0;
}

// Replaces every distinct value in one text column with an indexed
// placeholder, so rows that really shared a value still share one and the
// browse-by structure survives.
int renameDistinctValues(const SqlCipherDb &db, const std::string &table, const std::string &idColumn,
                         const std::string &textColumn, const std::string &kind)
{
    std::map<std::string, int64_t> ids;
    {
        SqlCipherStatement stmt(db, "SELECT " + idColumn + ", " + textColumn + " FROM " + table);
        while (stmt.step()) {
            if (stmt.columnIsNull(1)) {
                continue;
            }
            ids[stmt.columnText(1)] = stmt.columnInt64(0);
        }
    }
    // Re-read, because two rows can share a value and both need updating.
    std::vector<std::pair<int64_t, std::string>> rows;
    {
        SqlCipherStatement stmt(db, "SELECT " + idColumn + ", " + textColumn + " FROM " + table);
        while (stmt.step()) {
            if (stmt.columnIsNull(1)) {
                continue;
            }
            rows.emplace_back(stmt.columnInt64(0), stmt.columnText(1));
        }
    }
    std::map<std::string, std::string> placeholderFor;
    size_t next = 0;
    int changed = 0;
    for (const auto &[id, value] : rows) {
        auto it = placeholderFor.find(value);
        if (it == placeholderFor.end()) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%03zu", next++);
            it = placeholderFor.emplace(value, kind + " " + buf).first;
        }
        SqlCipherStatement update(db, "UPDATE " + table + " SET " + textColumn + " = ? WHERE " + idColumn + " = ?");
        update.bindText(1, it->second);
        update.bindInt64(2, id);
        update.run();
        ++changed;
    }
    return changed;
}

}  // namespace

OneLibraryAnonymizationResult anonymizeOneLibraryDatabase(const std::string &dbPath)
{
    OneLibraryAnonymizationResult result;
    std::error_code ec;
    if (!fs::is_regular_file(dbPath, ec)) {
        result.errorMessage = dbPath + " does not exist";
        return result;
    }

    try {
        const std::string key = deriveOneLibraryKey();
        SqlCipherLibrary lib;
        SqlCipherDb db(lib, dbPath, /*readOnly=*/false);
        db.exec("PRAGMA key = '" + key + "';");
        db.exec("BEGIN");

        // --- content: the tracks themselves ---
        const auto contentColumns = columnsOf(db, "content");
        std::vector<std::tuple<int64_t, std::string, std::string>> tracks;  // id, path, fileName
        {
            const bool hasFileName = contentColumns.count("fileName") > 0;
            SqlCipherStatement stmt(db, hasFileName ? "SELECT content_id, path, fileName FROM content"
                                                     : "SELECT content_id, path, path FROM content");
            while (stmt.step()) {
                tracks.emplace_back(stmt.columnInt64(0), stmt.columnText(1), stmt.columnText(2));
            }
        }
        for (const auto &[id, path, fileName] : tracks) {
            // The real filename is the shared key across all three
            // catalogs. Prefer the stored filename, fall back to the
            // path's own last segment when the column is absent or empty.
            const std::string realFilename = fileName.empty() ? basenameOf(path) : fileName;
            const std::string obfuscatedFilename = anonymizationFilenamePlaceholder(realFilename);

            SqlCipherStatement update(db, "UPDATE content SET title = ?, path = ?"
                                          + std::string(contentColumns.count("fileName") ? ", fileName = ?" : "")
                                          + (contentColumns.count("comment") ? ", comment = ?" : "")
                                          + " WHERE content_id = ?");
            int bind = 1;
            update.bindText(bind++, anonymizationPlaceholder("Track", realFilename));
            // The leading slash is part of this format's own convention:
            // the reader strips exactly one character before joining
            // against the stick root, and the writer puts one back when
            // it derives a content path. Writing "Contents/x.mp3" instead
            // of "/Contents/x.mp3" makes every lookup miss by one
            // character and report "no content row for /ontents/x.mp3".
            update.bindText(bind++, "/Contents/" + obfuscatedFilename);
            if (contentColumns.count("fileName")) {
                update.bindText(bind++, obfuscatedFilename);
            }
            if (contentColumns.count("comment")) {
                update.bindText(bind++, anonymizationPlaceholder("Comment", realFilename));
            }
            update.bindInt64(bind, id);
            update.run();
            ++result.tracksScrubbed;
        }

        // --- the normalized lookup tables beside it ---
        if (tableExists(db, "artist")) {
            result.artistsRenamed = renameDistinctValues(db, "artist", "artist_id", "name", "Artist");
        }
        if (tableExists(db, "album")) {
            result.albumsRenamed = renameDistinctValues(db, "album", "album_id", "name", "Album");
        }
        if (tableExists(db, "genre")) {
            result.genresRenamed = renameDistinctValues(db, "genre", "genre_id", "name", "Genre");
        }
        if (tableExists(db, "label")) {
            result.labelsRenamed = renameDistinctValues(db, "label", "label_id", "name", "Label");
        }
        if (tableExists(db, "playlist")) {
            result.playlistsRenamed = renameDistinctValues(db, "playlist", "playlist_id", "name", "Playlist");
        }

        // --- cue comments: free text a DJ types themselves ---
        if (tableExists(db, "cue") && columnsOf(db, "cue").count("cueComment")) {
            std::vector<int64_t> cueIds;
            {
                SqlCipherStatement stmt(db, "SELECT rowid FROM cue WHERE cueComment IS NOT NULL AND cueComment <> ''");
                while (stmt.step()) {
                    cueIds.push_back(stmt.columnInt64(0));
                }
            }
            size_t index = 0;
            for (int64_t rowid : cueIds) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%03zu", index++);
                SqlCipherStatement update(db, "UPDATE cue SET cueComment = ? WHERE rowid = ?");
                update.bindText(1, std::string("Cue ") + buf);
                update.bindInt64(2, rowid);
                update.run();
                ++result.cueCommentsScrubbed;
            }
        }

        // --- artwork: the images themselves are not shipped, so the rows
        // pointing at them must not keep naming real files either ---
        if (tableExists(db, "image") && columnsOf(db, "image").count("path")) {
            db.exec("UPDATE image SET path = 'Artwork/removed.jpg'");
        }

        db.exec("COMMIT");
        // Rebuilds the file so no scrubbed text survives in free pages.
        // Without it the old values sit in the database's own slack space
        // and a hex dump reads them straight back.
        db.exec("VACUUM");
    } catch (const std::exception &e) {
        result.errorMessage = e.what();
    }
    return result;
}

}  // namespace seabass::infrastructure::onelibrary
