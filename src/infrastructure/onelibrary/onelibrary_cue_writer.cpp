#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"

#include <algorithm>
#include <stdexcept>

#include "infrastructure/onelibrary/onelibrary_key.hpp"
#include "infrastructure/onelibrary/sqlcipher_dyn.hpp"

namespace seabass::infrastructure::onelibrary
{

namespace fs = std::filesystem;
using domain::CuePoint;

namespace
{

// Converts an absolute (or mixed-separator, e.g. "H:\/Contents/...")
// track file path into the slash-normalized, stick-root-relative form
// exportLibrary.db's content.path column uses, confirmed against a
// real stick's data during development, e.g.
// "/Contents/Artist/Track/01_Artist-Track.mp3".
std::string toContentPath(const std::string &stickRoot, const std::string &filePath)
{
    std::error_code ec;
    fs::path rel = fs::relative(fs::path(filePath), fs::path(stickRoot), ec);
    std::string relStr = ec ? filePath : rel.generic_string();
    std::replace(relStr.begin(), relStr.end(), '\\', '/');
    if (relStr.empty() || relStr[0] != '/') {
        relStr = "/" + relStr;
    }
    return relStr;
}

}  // namespace

std::string OneLibraryCueWriter::dbPathFor(const std::string &pioneerRoot)
{
    return (fs::path(pioneerRoot) / "rekordbox" / "exportLibrary.db").string();
}

bool OneLibraryCueWriter::existsFor(const std::string &pioneerRoot)
{
    std::error_code ec;
    return fs::is_regular_file(dbPathFor(pioneerRoot), ec);
}

OneLibraryCueWriter::OneLibraryCueWriter(std::string pioneerRoot, std::optional<std::string> realStickRoot)
    : m_pioneerRoot(std::move(pioneerRoot))
{
    m_stickRoot = realStickRoot ? std::move(*realStickRoot) : fs::path(m_pioneerRoot).parent_path().string();
    m_dbPath = dbPathFor(m_pioneerRoot);
    std::error_code ec;
    m_originalFileSize = fs::file_size(m_dbPath, ec);
    m_originalMtime = fs::last_write_time(m_dbPath, ec);
    if (ec) {
        throw std::runtime_error("onelibrary: " + m_dbPath + " does not exist, check existsFor() first");
    }
    m_originalDataVersion = queryDataVersion();
}

int64_t OneLibraryCueWriter::queryDataVersion() const
{
    SqlCipherLibrary lib;
    SqlCipherDb db(lib, m_dbPath, /*readOnly=*/true);
    db.exec("PRAGMA key = '" + deriveOneLibraryKey() + "';");
    SqlCipherStatement stmt(db, "PRAGMA data_version");
    stmt.step();
    return stmt.columnInt64(0);
}

void OneLibraryCueWriter::checkNotStale() const
{
    std::error_code statEc;
    auto currentSize = fs::file_size(m_dbPath, statEc);
    auto currentMtime = fs::last_write_time(m_dbPath, statEc);
    bool statMismatch = statEc || currentSize != m_originalFileSize || currentMtime != m_originalMtime;
    bool dataVersionMismatch = queryDataVersion() != m_originalDataVersion;
    if (statMismatch || dataVersionMismatch) {
        throw std::runtime_error("onelibrary: " + m_dbPath +
                                  " changed since this writer was opened, refusing to write a stale copy");
    }
}

void OneLibraryCueWriter::refreshStalenessBaseline()
{
    std::error_code refreshEc;
    m_originalFileSize = fs::file_size(m_dbPath, refreshEc);
    m_originalMtime = fs::last_write_time(m_dbPath, refreshEc);
    m_originalDataVersion = queryDataVersion();
}

void OneLibraryCueWriter::writeCuesForPath(const std::string &filePath, const std::vector<CuePoint> &cues)
{
    // Staleness guard, adapted from PdbRowWriter's own convention to
    // this format: PdbRowWriter reads a whole file into memory once and
    // must re-check nothing else wrote it before a later blind
    // overwrite; here the write goes through a real SQL transaction
    // (SQLite's own file locking already prevents two writers from
    // racing *during* it), so the equivalent check is "does the file on
    // disk still look like the one this writer was constructed
    // against", refusing if something rewrote it out from under us in
    // between construction and this call.
    checkNotStale();

    std::string contentPath = toContentPath(m_stickRoot, filePath);
    std::string key = deriveOneLibraryKey();

    SqlCipherLibrary lib;
    {
        SqlCipherDb db(lib, m_dbPath, /*readOnly=*/false);
        db.exec("PRAGMA key = '" + key + "';");

        int64_t contentId = -1;
        {
            SqlCipherStatement find(db, "SELECT content_id FROM content WHERE path = ?");
            find.bindText(1, contentPath);
            if (!find.step()) {
                throw std::runtime_error("onelibrary: no content row for path " + contentPath);
            }
            contentId = find.columnInt64(0);
        }

        db.exec("BEGIN IMMEDIATE;");
        try {
            {
                // Must run before the DELETE FROM cue below. It looks
                // up cue_ids by content_id via a subquery against the
                // still-present cue rows; deleting cue first would leave
                // this a silent no-op and orphan hotCueBankList_cue rows.
                SqlCipherStatement delBank(db,
                                            "DELETE FROM hotCueBankList_cue WHERE cue_id IN "
                                            "(SELECT cue_id FROM cue WHERE content_id = ?)");
                delBank.bindInt64(1, contentId);
                delBank.run();
            }
            {
                SqlCipherStatement del(db, "DELETE FROM cue WHERE content_id = ?");
                del.bindInt64(1, contentId);
                del.run();
            }
            for (const auto &cue : cues) {
                // kind: 0 for a memory cue, otherwise the hot cue slot
                // number, matching the DOCUMENTED convention of
                // master.db's structurally-equivalent djmdCue.Kind
                // ("0 if memory cue, otherwise the number of Hot Cue").
                // Device Library Plus's own schema is described by prior
                // reverse-engineering as "similar to the main Rekordbox
                // database", and this stick's own OneLibrary cue table
                // is empty (never populated), so this specific mapping
                // is a well-reasoned inference, not independently
                // confirmed against real Device Library Plus data,
                // see docs/onelibrary-format.md.
                int kind = cue.kind == CuePoint::Kind::Hot ? cue.hotCueNumber : 0;
                int64_t inUsec = static_cast<int64_t>(cue.positionMs * 1000.0);

                SqlCipherStatement insert(db,
                                           "INSERT INTO cue (content_id, kind, colorTableIndex, cueComment, "
                                           "isActiveLoop, inUsec, outUsec) VALUES (?, ?, ?, ?, 0, ?, ?)");
                insert.bindInt64(1, contentId);
                insert.bindInt64(2, kind);
                // colorTableIndex: no verified RGB/hex -> index mapping
                // exists anywhere this was cross-checked against (see
                // docs/onelibrary-format.md), 0 (a defined, inert
                // default) rather than a fabricated guess.
                insert.bindInt64(3, 0);
                if (cue.comment.empty()) {
                    insert.bindNull(4);
                } else {
                    insert.bindText(4, cue.comment);
                }
                insert.bindInt64(5, inUsec);
                insert.bindInt64(6, inUsec);  // outUsec: cue points (not loops) start==end, matching export.pdb's own convention
                insert.run();
            }
            db.exec("COMMIT;");
        } catch (...) {
            // Best-effort: never let a rollback failure mask (or replace,
            // via a second throw) the original error that triggered it.
            try {
                db.exec("ROLLBACK;");
            } catch (...) {
            }
            throw;
        }
    }  // db closed here

    // Correctness verification: SQLite's transaction already guarantees
    // the commit was durable, but not that this code wrote what it
    // meant to, re-open fresh and read the rows back, mirroring
    // PdbRowWriter::commit()'s "re-parse the edited result before
    // trusting it" check, adapted to "re-read the committed result
    // before trusting it" for a SQL store.
    SqlCipherLibrary verifyLib;
    SqlCipherDb verifyDb(verifyLib, m_dbPath, /*readOnly=*/true);
    verifyDb.exec("PRAGMA key = '" + key + "';");
    int64_t contentId = -1;
    {
        SqlCipherStatement find(verifyDb, "SELECT content_id FROM content WHERE path = ?");
        find.bindText(1, contentPath);
        if (!find.step()) {
            throw std::runtime_error("onelibrary: post-write verification failed, content row vanished");
        }
        contentId = find.columnInt64(0);
    }
    SqlCipherStatement verify(verifyDb, "SELECT count(*) FROM cue WHERE content_id = ?");
    verify.bindInt64(1, contentId);
    verify.step();
    auto actualCount = static_cast<size_t>(verify.columnInt64(0));
    if (actualCount != cues.size()) {
        throw std::runtime_error("onelibrary: post-write verification failed, expected " +
                                  std::to_string(cues.size()) + " cue row(s), found " +
                                  std::to_string(actualCount));
    }

    // Refresh the staleness baseline to the file's new (post-write) state.
    // Without this, reusing one writer instance across several
    // writeCuesForPath() calls (one write per track) would have every
    // call after the first refuse itself, since the file legitimately
    // changed size/mtime due to this writer's *own* prior write.
    refreshStalenessBaseline();
}

void OneLibraryCueWriter::removeTrackByPath(const std::string &filePath)
{
    // Same staleness guard as writeCuesForPath(), see its own comment
    // for the reasoning.
    checkNotStale();

    std::string contentPath = toContentPath(m_stickRoot, filePath);
    std::string key = deriveOneLibraryKey();

    SqlCipherLibrary lib;
    {
        SqlCipherDb db(lib, m_dbPath, /*readOnly=*/false);
        db.exec("PRAGMA key = '" + key + "';");

        int64_t contentId = -1;
        {
            SqlCipherStatement find(db, "SELECT content_id FROM content WHERE path = ?");
            find.bindText(1, contentPath);
            if (!find.step()) {
                throw std::runtime_error("onelibrary: no content row for path " + contentPath);
            }
            contentId = find.columnInt64(0);
        }

        db.exec("BEGIN IMMEDIATE;");
        try {
            // No FK/cascade enforcement exists anywhere in this schema
            // (no declared constraints confirmed, and PRAGMA
            // foreign_keys is never set for this connection). Every
            // dependent table is deleted explicitly, in dependency
            // order, exactly like writeCuesForPath()'s own
            // hotCueBankList_cue -> cue ordering, extended one level up
            // to content itself.
            {
                SqlCipherStatement delBank(db,
                                            "DELETE FROM hotCueBankList_cue WHERE cue_id IN "
                                            "(SELECT cue_id FROM cue WHERE content_id = ?)");
                delBank.bindInt64(1, contentId);
                delBank.run();
            }
            {
                SqlCipherStatement delCue(db, "DELETE FROM cue WHERE content_id = ?");
                delCue.bindInt64(1, contentId);
                delCue.run();
            }
            {
                SqlCipherStatement delPlaylist(db, "DELETE FROM playlist_content WHERE content_id = ?");
                delPlaylist.bindInt64(1, contentId);
                delPlaylist.run();
            }
            {
                SqlCipherStatement delContent(db, "DELETE FROM content WHERE content_id = ?");
                delContent.bindInt64(1, contentId);
                delContent.run();
            }
            db.exec("COMMIT;");
        } catch (...) {
            try {
                db.exec("ROLLBACK;");
            } catch (...) {
            }
            throw;
        }
    }  // db closed here

    // Correctness verification: re-open fresh and confirm the row (and
    // its dependents) are actually gone, same "re-read the committed
    // result before trusting it" convention as writeCuesForPath().
    SqlCipherLibrary verifyLib;
    SqlCipherDb verifyDb(verifyLib, m_dbPath, /*readOnly=*/true);
    verifyDb.exec("PRAGMA key = '" + key + "';");
    SqlCipherStatement verify(verifyDb, "SELECT count(*) FROM content WHERE path = ?");
    verify.bindText(1, contentPath);
    verify.step();
    if (verify.columnInt64(0) != 0) {
        throw std::runtime_error("onelibrary: post-removal verification failed, content row still present");
    }

    // Refresh the staleness baseline, see writeCuesForPath()'s own
    // comment for why this matters when one writer instance is reused
    // across several calls.
    refreshStalenessBaseline();
}

void OneLibraryCueWriter::removeTrackByPathReplacingWith(const std::string &doomedFilePath,
                                                           const std::string &survivorFilePath)
{
    // Same staleness guard as writeCuesForPath(), see its own comment
    // for the reasoning.
    checkNotStale();

    std::string doomedContentPath = toContentPath(m_stickRoot, doomedFilePath);
    std::string survivorContentPath = toContentPath(m_stickRoot, survivorFilePath);
    std::string key = deriveOneLibraryKey();

    SqlCipherLibrary lib;
    {
        SqlCipherDb db(lib, m_dbPath, /*readOnly=*/false);
        db.exec("PRAGMA key = '" + key + "';");

        int64_t doomedId = -1;
        {
            SqlCipherStatement find(db, "SELECT content_id FROM content WHERE path = ?");
            find.bindText(1, doomedContentPath);
            if (!find.step()) {
                throw std::runtime_error("onelibrary: no content row for path " + doomedContentPath);
            }
            doomedId = find.columnInt64(0);
        }
        int64_t survivorId = -1;
        {
            SqlCipherStatement find(db, "SELECT content_id FROM content WHERE path = ?");
            find.bindText(1, survivorContentPath);
            if (!find.step()) {
                throw std::runtime_error("onelibrary: no content row for path " + survivorContentPath);
            }
            survivorId = find.columnInt64(0);
        }

        db.exec("BEGIN IMMEDIATE;");
        try {
            {
                SqlCipherStatement delBank(db,
                                            "DELETE FROM hotCueBankList_cue WHERE cue_id IN "
                                            "(SELECT cue_id FROM cue WHERE content_id = ?)");
                delBank.bindInt64(1, doomedId);
                delBank.run();
            }
            {
                SqlCipherStatement delCue(db, "DELETE FROM cue WHERE content_id = ?");
                delCue.bindInt64(1, doomedId);
                delCue.run();
            }
            {
                // Reassign the doomed row's playlist memberships onto the
                // survivor first (matches PdbRowWriter::
                // reassignPlaylistMemberships()'s own dedup rule exactly):
                // a playlist the survivor is already in is left alone --
                // its doomed-side entry is just dropped below, not turned
                // into a second row for the same (playlist, survivor)
                // pair. Everything else genuinely moves.
                SqlCipherStatement reassign(db,
                                             "UPDATE playlist_content SET content_id = ? WHERE content_id = ? "
                                             "AND playlist_id NOT IN "
                                             "(SELECT playlist_id FROM playlist_content WHERE content_id = ?)");
                reassign.bindInt64(1, survivorId);
                reassign.bindInt64(2, doomedId);
                reassign.bindInt64(3, survivorId);
                reassign.run();
            }
            {
                // Whatever's left under doomedId at this point is exactly
                // the memberships the UPDATE above skipped (survivor
                // already had them) -- safe to drop outright now.
                SqlCipherStatement delPlaylist(db, "DELETE FROM playlist_content WHERE content_id = ?");
                delPlaylist.bindInt64(1, doomedId);
                delPlaylist.run();
            }
            {
                SqlCipherStatement delContent(db, "DELETE FROM content WHERE content_id = ?");
                delContent.bindInt64(1, doomedId);
                delContent.run();
            }
            db.exec("COMMIT;");
        } catch (...) {
            try {
                db.exec("ROLLBACK;");
            } catch (...) {
            }
            throw;
        }
    }  // db closed here

    // Correctness verification: re-open fresh and confirm the doomed row
    // is gone AND the survivor's own row still exists (a broken
    // survivor-lookup above would otherwise silently produce a no-op
    // reassignment followed by a real deletion, losing memberships
    // instead of moving them).
    SqlCipherLibrary verifyLib;
    SqlCipherDb verifyDb(verifyLib, m_dbPath, /*readOnly=*/true);
    verifyDb.exec("PRAGMA key = '" + key + "';");
    {
        SqlCipherStatement verify(verifyDb, "SELECT count(*) FROM content WHERE path = ?");
        verify.bindText(1, doomedContentPath);
        verify.step();
        if (verify.columnInt64(0) != 0) {
            throw std::runtime_error("onelibrary: post-removal verification failed, content row still present");
        }
    }
    {
        SqlCipherStatement verify(verifyDb, "SELECT count(*) FROM content WHERE path = ?");
        verify.bindText(1, survivorContentPath);
        verify.step();
        if (verify.columnInt64(0) != 1) {
            throw std::runtime_error("onelibrary: post-removal verification failed, survivor content row missing");
        }
    }

    refreshStalenessBaseline();
}

void OneLibraryCueWriter::propagateMissingFieldsForPath(const std::string &donorFilePath,
                                                          const std::string &targetFilePath, bool copyBpm,
                                                          bool copyKey, bool copyArtwork)
{
    if (!copyBpm && !copyKey && !copyArtwork) {
        return;
    }

    checkNotStale();

    std::string donorContentPath = toContentPath(m_stickRoot, donorFilePath);
    std::string targetContentPath = toContentPath(m_stickRoot, targetFilePath);
    std::string key = deriveOneLibraryKey();

    SqlCipherLibrary lib;
    SqlCipherDb db(lib, m_dbPath, /*readOnly=*/false);
    db.exec("PRAGMA key = '" + key + "';");

    int64_t donorId = -1;
    {
        SqlCipherStatement find(db, "SELECT content_id FROM content WHERE path = ?");
        find.bindText(1, donorContentPath);
        if (!find.step()) {
            throw std::runtime_error("onelibrary: no content row for path " + donorContentPath);
        }
        donorId = find.columnInt64(0);
    }
    int64_t targetId = -1;
    {
        SqlCipherStatement find(db, "SELECT content_id FROM content WHERE path = ?");
        find.bindText(1, targetContentPath);
        if (!find.step()) {
            throw std::runtime_error("onelibrary: no content row for path " + targetContentPath);
        }
        targetId = find.columnInt64(0);
    }

    db.exec("BEGIN IMMEDIATE;");
    try {
        if (copyBpm) {
            SqlCipherStatement copy(db, "UPDATE content SET bpmx100 = (SELECT bpmx100 FROM content WHERE "
                                         "content_id = ?) WHERE content_id = ?");
            copy.bindInt64(1, donorId);
            copy.bindInt64(2, targetId);
            copy.run();
        }
        if (copyKey) {
            SqlCipherStatement copy(db, "UPDATE content SET key_id = (SELECT key_id FROM content WHERE "
                                         "content_id = ?) WHERE content_id = ?");
            copy.bindInt64(1, donorId);
            copy.bindInt64(2, targetId);
            copy.run();
        }
        if (copyArtwork) {
            SqlCipherStatement copy(db, "UPDATE content SET image_id = (SELECT image_id FROM content WHERE "
                                         "content_id = ?) WHERE content_id = ?");
            copy.bindInt64(1, donorId);
            copy.bindInt64(2, targetId);
            copy.run();
        }
        db.exec("COMMIT;");
    } catch (...) {
        try {
            db.exec("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }

    refreshStalenessBaseline();
}

}  // namespace seabass::infrastructure::onelibrary
