#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "domain/track.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/onelibrary/onelibrary_key.hpp"
#include "infrastructure/onelibrary/sqlcipher_dyn.hpp"

using namespace djconvert::infrastructure::onelibrary;
using namespace djconvert::domain;
namespace fs = std::filesystem;

namespace
{

// Builds a minimal-but-real exportLibrary.db fixture: SQLCipher-encrypted
// with the real, derived, universal OneLibrary key, containing just
// enough schema (content, cue, hotCueBankList_cue) for OneLibraryCueWriter
// to operate on. Column names/types match a real stick's `.schema` output
// exactly (captured during development, see docs/onelibrary-format.md),
// trimmed to what these tests exercise.
void createFixture(const std::string &pioneerRoot)
{
    fs::create_directories(fs::path(pioneerRoot) / "rekordbox");
    std::string dbPath = OneLibraryCueWriter::dbPathFor(pioneerRoot);

    std::string key = deriveOneLibraryKey();
    SqlCipherLibrary lib;
    SqlCipherDb db(lib, dbPath, /*readOnly=*/false);
    db.exec("PRAGMA key = '" + key + "';");
    db.exec(
        "CREATE TABLE content(content_id integer primary key, title varchar, path varchar);");
    db.exec(
        "CREATE TABLE cue(cue_id integer primary key, content_id integer, kind integer, "
        "colorTableIndex integer, cueComment varchar, isActiveLoop integer, inUsec integer, "
        "outUsec integer);");
    db.exec("CREATE TABLE hotCueBankList_cue(hotCueBankList_id integer, cue_id integer, sequenceNo integer);");
    db.exec("CREATE TABLE playlist_content(content_id integer, playlist_id integer, sequenceNo integer);");
    db.exec("INSERT INTO content (content_id, title, path) VALUES (1, 'Test Track', '/Contents/Test Track.mp3');");
}

std::vector<CuePoint> sampleCues()
{
    CuePoint hot1{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "drop"};
    CuePoint hot2{CuePoint::Kind::Hot, 2, 5500.5, "#00FF00", ""};
    CuePoint mem{CuePoint::Kind::Memory, 0, 12345.0, "#0000FF", "breakdown"};
    return {hot1, hot2, mem};
}

// Fresh empty scratch dir with a fixture in it, ready for one test case.
fs::path freshScratch()
{
    fs::path scratch = fs::temp_directory_path() / "djconvert_onelibrary_test";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch);
    return scratch;
}

}  // namespace

int main()
{
    // Case 1: write cues, read them back through an entirely independent
    // second connection (not the writer's own success return), proves
    // a real round-trip, not just "the write call didn't throw".
    {
        fs::path scratch = freshScratch();
        fs::path pioneerRoot = scratch / "PIONEER";
        createFixture(pioneerRoot.string());
        assert(OneLibraryCueWriter::existsFor(pioneerRoot.string()));

        OneLibraryCueWriter writer(pioneerRoot.string());
        std::string filePath = (scratch / "Contents" / "Test Track.mp3").string();
        writer.writeCuesForPath(filePath, sampleCues());

        std::string key = deriveOneLibraryKey();
        SqlCipherLibrary lib;
        SqlCipherDb db(lib, OneLibraryCueWriter::dbPathFor(pioneerRoot.string()), /*readOnly=*/true);
        db.exec("PRAGMA key = '" + key + "';");

        SqlCipherStatement countStmt(db, "SELECT count(*) FROM cue WHERE content_id = 1");
        countStmt.step();
        assert(countStmt.columnInt64(0) == 3);

        SqlCipherStatement hotStmt(
            db, "SELECT inUsec, cueComment FROM cue WHERE content_id = 1 AND kind = 1");
        assert(hotStmt.step());
        assert(hotStmt.columnInt64(0) == 1000000);  // 1000.0ms -> 1,000,000us
        assert(hotStmt.columnText(1) == "drop");

        SqlCipherStatement memStmt(db, "SELECT inUsec FROM cue WHERE content_id = 1 AND kind = 0");
        assert(memStmt.step());
        assert(memStmt.columnInt64(0) == 12345000);  // 12345.0ms -> 12,345,000us

        std::cout << "case 1 (round-trip write/read via an independent reader) OK\n";
    }

    // Case 2: the staleness guard refuses a write if the file changed
    // since this writer was constructed (simulating another process/
    // connection touching it in between).
    {
        fs::path scratch = freshScratch();
        fs::path pioneerRoot = scratch / "PIONEER";
        createFixture(pioneerRoot.string());
        OneLibraryCueWriter writer(pioneerRoot.string());

        {
            std::string key = deriveOneLibraryKey();
            SqlCipherLibrary lib;
            SqlCipherDb db(lib, OneLibraryCueWriter::dbPathFor(pioneerRoot.string()), /*readOnly=*/false);
            db.exec("PRAGMA key = '" + key + "';");
            db.exec("INSERT INTO content (content_id, title, path) VALUES (2, 'Other', '/Contents/Other.mp3');");
        }

        bool threw = false;
        try {
            writer.writeCuesForPath((scratch / "Contents" / "Test Track.mp3").string(), sampleCues());
        } catch (const std::exception &) {
            threw = true;
        }
        assert(threw);
        std::cout << "case 2 (staleness guard refuses a write after external modification) OK\n";
    }

    // Case 3: writing cues for a path with no matching content.path row
    // fails cleanly, and doesn't corrupt/touch anything else.
    {
        fs::path scratch = freshScratch();
        fs::path pioneerRoot = scratch / "PIONEER";
        createFixture(pioneerRoot.string());
        OneLibraryCueWriter writer(pioneerRoot.string());

        bool threw = false;
        try {
            writer.writeCuesForPath((scratch / "Contents" / "Nonexistent.mp3").string(), sampleCues());
        } catch (const std::exception &) {
            threw = true;
        }
        assert(threw);

        std::string key = deriveOneLibraryKey();
        SqlCipherLibrary lib;
        SqlCipherDb db(lib, OneLibraryCueWriter::dbPathFor(pioneerRoot.string()), /*readOnly=*/true);
        db.exec("PRAGMA key = '" + key + "';");
        SqlCipherStatement contentCount(db, "SELECT count(*) FROM content");
        contentCount.step();
        assert(contentCount.columnInt64(0) == 1);
        SqlCipherStatement cueCount(db, "SELECT count(*) FROM cue");
        cueCount.step();
        assert(cueCount.columnInt64(0) == 0);

        std::cout << "case 3 (write to unknown path fails cleanly, nothing corrupted) OK\n";
    }

    // Case 4: re-writing cues onto the same track replaces the whole set
    // (matching application::CueWriter's "pass the complete list" contract),
    // rather than appending to the previous write.
    {
        fs::path scratch = freshScratch();
        fs::path pioneerRoot = scratch / "PIONEER";
        createFixture(pioneerRoot.string());
        std::string filePath = (scratch / "Contents" / "Test Track.mp3").string();

        {
            OneLibraryCueWriter writer(pioneerRoot.string());
            writer.writeCuesForPath(filePath, sampleCues());
        }
        {
            OneLibraryCueWriter writer(pioneerRoot.string());
            std::vector<CuePoint> single = {CuePoint{CuePoint::Kind::Hot, 3, 2000.0, "#FFFFFF", "only one now"}};
            writer.writeCuesForPath(filePath, single);
        }

        std::string key = deriveOneLibraryKey();
        SqlCipherLibrary lib;
        SqlCipherDb db(lib, OneLibraryCueWriter::dbPathFor(pioneerRoot.string()), /*readOnly=*/true);
        db.exec("PRAGMA key = '" + key + "';");
        SqlCipherStatement countStmt(db, "SELECT count(*) FROM cue WHERE content_id = 1");
        countStmt.step();
        assert(countStmt.columnInt64(0) == 1);

        std::cout << "case 4 (re-write replaces the whole cue set, not appends) OK\n";
    }

    // Case 5: one writer instance, reused for several sequential
    // writeCuesForPath() calls across different tracks. The staleness
    // baseline must refresh after each successful write, or every call
    // after the first would spuriously refuse itself (see
    // onelibrary_cue_writer.cpp's refresh at the end of
    // writeCuesForPath()). Matches how local_cue_controller.cpp actually
    // uses this class (one writer, one write per restore candidate).
    {
        fs::path scratch = freshScratch();
        fs::path pioneerRoot = scratch / "PIONEER";
        createFixture(pioneerRoot.string());
        {
            std::string key = deriveOneLibraryKey();
            SqlCipherLibrary lib;
            SqlCipherDb db(lib, OneLibraryCueWriter::dbPathFor(pioneerRoot.string()), /*readOnly=*/false);
            db.exec("PRAGMA key = '" + key + "';");
            db.exec("INSERT INTO content (content_id, title, path) VALUES (2, 'Second Track', '/Contents/Second Track.mp3');");
        }

        OneLibraryCueWriter writer(pioneerRoot.string());
        writer.writeCuesForPath((scratch / "Contents" / "Test Track.mp3").string(), sampleCues());
        // Would previously throw here (stale baseline from before the
        // first write above) if the baseline weren't refreshed.
        writer.writeCuesForPath((scratch / "Contents" / "Second Track.mp3").string(), sampleCues());

        std::string key = deriveOneLibraryKey();
        SqlCipherLibrary lib;
        SqlCipherDb db(lib, OneLibraryCueWriter::dbPathFor(pioneerRoot.string()), /*readOnly=*/true);
        db.exec("PRAGMA key = '" + key + "';");
        SqlCipherStatement c1(db, "SELECT count(*) FROM cue WHERE content_id = 1");
        c1.step();
        assert(c1.columnInt64(0) == 3);
        SqlCipherStatement c2(db, "SELECT count(*) FROM cue WHERE content_id = 2");
        c2.step();
        assert(c2.columnInt64(0) == 3);

        std::cout << "case 5 (one writer reused across sequential writes to different tracks) OK\n";
    }

    // Case 6: removeTrackByPath() deletes the content row and every
    // dependent row (cue, hotCueBankList_cue, playlist_content). No FK/
    // cascade enforcement exists in this schema (see
    // docs/onelibrary-format.md), so this is exactly the risk a blind
    // single-table DELETE would miss.
    {
        fs::path scratch = freshScratch();
        fs::path pioneerRoot = scratch / "PIONEER";
        createFixture(pioneerRoot.string());
        std::string filePath = (scratch / "Contents" / "Test Track.mp3").string();

        {
            OneLibraryCueWriter writer(pioneerRoot.string());
            writer.writeCuesForPath(filePath, sampleCues());
        }
        {
            std::string key = deriveOneLibraryKey();
            SqlCipherLibrary lib;
            SqlCipherDb db(lib, OneLibraryCueWriter::dbPathFor(pioneerRoot.string()), /*readOnly=*/false);
            db.exec("PRAGMA key = '" + key + "';");
            db.exec("INSERT INTO playlist_content (content_id, playlist_id, sequenceNo) VALUES (1, 99, 0);");
        }

        {
            OneLibraryCueWriter writer(pioneerRoot.string());
            writer.removeTrackByPath(filePath);
        }

        std::string key = deriveOneLibraryKey();
        SqlCipherLibrary lib;
        SqlCipherDb db(lib, OneLibraryCueWriter::dbPathFor(pioneerRoot.string()), /*readOnly=*/true);
        db.exec("PRAGMA key = '" + key + "';");

        SqlCipherStatement contentCount(db, "SELECT count(*) FROM content WHERE content_id = 1");
        contentCount.step();
        assert(contentCount.columnInt64(0) == 0);
        SqlCipherStatement cueCount(db, "SELECT count(*) FROM cue WHERE content_id = 1");
        cueCount.step();
        assert(cueCount.columnInt64(0) == 0);
        SqlCipherStatement playlistCount(db, "SELECT count(*) FROM playlist_content WHERE content_id = 1");
        playlistCount.step();
        assert(playlistCount.columnInt64(0) == 0);
        // hotCueBankList_cue has no content_id column of its own. It's
        // only reachable via cue_id, which is exactly why the DELETE
        // must run before (not after) the cue rows it depends on are
        // gone. Confirm no bank rows reference cue_ids that no longer
        // exist in cue at all (a real orphan, since cue is now empty).
        SqlCipherStatement bankCount(db, "SELECT count(*) FROM hotCueBankList_cue");
        bankCount.step();
        assert(bankCount.columnInt64(0) == 0);

        std::cout << "case 6 (removeTrackByPath deletes content + every dependent row) OK\n";
    }

    // Case 7: removing a path with no matching content.path row fails
    // cleanly, same "nothing corrupted" contract as writeCuesForPath()'s
    // own unknown-path case.
    {
        fs::path scratch = freshScratch();
        fs::path pioneerRoot = scratch / "PIONEER";
        createFixture(pioneerRoot.string());
        OneLibraryCueWriter writer(pioneerRoot.string());

        bool threw = false;
        try {
            writer.removeTrackByPath((scratch / "Contents" / "Nonexistent.mp3").string());
        } catch (const std::exception &) {
            threw = true;
        }
        assert(threw);

        std::string key = deriveOneLibraryKey();
        SqlCipherLibrary lib;
        SqlCipherDb db(lib, OneLibraryCueWriter::dbPathFor(pioneerRoot.string()), /*readOnly=*/true);
        db.exec("PRAGMA key = '" + key + "';");
        SqlCipherStatement contentCount(db, "SELECT count(*) FROM content");
        contentCount.step();
        assert(contentCount.columnInt64(0) == 1);

        std::cout << "case 7 (removeTrackByPath on an unknown path fails cleanly) OK\n";
    }

    // Case 8: the staleness guard applies to removeTrackByPath() too -
    // refuses if the file changed since this writer was constructed.
    {
        fs::path scratch = freshScratch();
        fs::path pioneerRoot = scratch / "PIONEER";
        createFixture(pioneerRoot.string());
        OneLibraryCueWriter writer(pioneerRoot.string());

        {
            std::string key = deriveOneLibraryKey();
            SqlCipherLibrary lib;
            SqlCipherDb db(lib, OneLibraryCueWriter::dbPathFor(pioneerRoot.string()), /*readOnly=*/false);
            db.exec("PRAGMA key = '" + key + "';");
            db.exec("INSERT INTO content (content_id, title, path) VALUES (2, 'Other', '/Contents/Other.mp3');");
        }

        bool threw = false;
        try {
            writer.removeTrackByPath((scratch / "Contents" / "Test Track.mp3").string());
        } catch (const std::exception &) {
            threw = true;
        }
        assert(threw);
        std::cout << "case 8 (staleness guard refuses removeTrackByPath after external modification) OK\n";
    }

    std::cout << "All onelibrary_cue_writer_test cases passed.\n";
    return 0;
}
