// RestoreMetadataChange: the write half of Restore Metadata, driven
// against a real (SQLCipher-encrypted) OneLibrary database rather than a
// mock, because the thing worth pinning here is what actually lands in
// the file.
//
// Case 1 is a regression guard with a data-loss bug behind it. A cue
// writer takes the vector it is handed as the complete set for the
// track, so an empty one deletes every cue rather than leaving them
// alone -- and a proposal staged for its rating alone carries no cues,
// as does one whose cues conflict under the skip-all default. Writing
// unconditionally therefore wiped exactly the live work that default
// exists to protect.
//
// The rest pin the two things the format itself decides and no reader
// reads back: that OneLibrary stores a rating in stars (0 to 5) and not
// on Engine's 0-to-100 scale, and that the comment column is djComment.

#include <QString>

#include <cassert>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "application/ports/cancellation_token.hpp"
#include "application/ports/progress_reporter.hpp"
#include "domain/metadata_restore.hpp"
#include "domain/track.hpp"
#include "gui/edit/changes/restore_metadata_change.hpp"
#include "gui/edit/save_context.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/onelibrary/onelibrary_key.hpp"
#include "infrastructure/onelibrary/sqlcipher_dyn.hpp"

#include "scratch_path.hpp"

using namespace seabass::gui;
using namespace seabass::domain;
using namespace seabass::infrastructure::onelibrary;
using seabass::application::CancellationToken;
namespace fs = std::filesystem;

namespace
{

// The same minimal-but-real fixture onelibrary_cue_writer_test.cpp
// builds -- see that file for why this schema shape -- plus the two
// annotation columns, which a real stick's content table also carries.
void createFixture(const std::string &pioneerRoot)
{
    fs::create_directories(fs::path(pioneerRoot) / "rekordbox");
    const std::string dbPath = OneLibraryCueWriter::dbPathFor(pioneerRoot);

    const std::string key = deriveOneLibraryKey();
    SqlCipherLibrary lib;
    SqlCipherDb db(lib, dbPath, /*readOnly=*/false);
    db.exec("PRAGMA key = '" + key + "';");
    db.exec(
        "CREATE TABLE content(content_id integer primary key, title varchar, path varchar, "
        "rating integer, djComment varchar);");
    db.exec(
        "CREATE TABLE cue(cue_id integer primary key, content_id integer, kind integer, "
        "colorTableIndex integer, cueComment varchar, isActiveLoop integer, inUsec integer, "
        "outUsec integer);");
    db.exec("CREATE TABLE hotCueBankList_cue(hotCueBankList_id integer, cue_id integer, sequenceNo integer);");
    db.exec("CREATE TABLE playlist_content(content_id integer, playlist_id integer, sequenceNo integer);");
    db.exec("INSERT INTO content (content_id, title, path) VALUES (1, 'Test Track', '/Contents/Test Track.mp3');");
}

// Everything read back goes through an independent second connection, so
// a case proves what is in the file rather than what a writer returned.
int cueCount(const std::string &pioneerRoot)
{
    SqlCipherLibrary lib;
    SqlCipherDb db(lib, OneLibraryCueWriter::dbPathFor(pioneerRoot), /*readOnly=*/true);
    db.exec("PRAGMA key = '" + deriveOneLibraryKey() + "';");
    SqlCipherStatement stmt(db, "SELECT COUNT(*) FROM cue WHERE content_id = 1");
    assert(stmt.step());
    return static_cast<int>(stmt.columnInt64(0));
}

std::optional<int> storedRating(const std::string &pioneerRoot)
{
    SqlCipherLibrary lib;
    SqlCipherDb db(lib, OneLibraryCueWriter::dbPathFor(pioneerRoot), /*readOnly=*/true);
    db.exec("PRAGMA key = '" + deriveOneLibraryKey() + "';");
    SqlCipherStatement stmt(db, "SELECT rating FROM content WHERE content_id = 1");
    assert(stmt.step());
    if (stmt.columnIsNull(0)) {
        return std::nullopt;
    }
    return static_cast<int>(stmt.columnInt64(0));
}

std::string storedComment(const std::string &pioneerRoot)
{
    SqlCipherLibrary lib;
    SqlCipherDb db(lib, OneLibraryCueWriter::dbPathFor(pioneerRoot), /*readOnly=*/true);
    db.exec("PRAGMA key = '" + deriveOneLibraryKey() + "';");
    SqlCipherStatement stmt(db, "SELECT djComment FROM content WHERE content_id = 1");
    assert(stmt.step());
    return stmt.columnText(0);
}

std::vector<CuePoint> liveCues()
{
    CuePoint hot1{CuePoint::Kind::Hot, 1, 1000.0, "#FF0000", "the DJ's own"};
    CuePoint hot2{CuePoint::Kind::Hot, 2, 2000.0, "#00FF00", "set last night"};
    return {hot1, hot2};
}

std::vector<CuePoint> storedCues()
{
    CuePoint hot1{CuePoint::Kind::Hot, 1, 9000.0, "#0000FF", "from the backup"};
    return {hot1};
}

struct Fixture
{
    fs::path scratch;
    fs::path pioneerRoot;
    std::string filePath;
};

Fixture freshFixture(const std::string &name)
{
    Fixture fixture;
    fixture.scratch = seabass::testing::scratchRoot() / ("seabass_restore_metadata_change_" + name);
    std::error_code ec;
    fs::remove_all(fixture.scratch, ec);
    fs::create_directories(fixture.scratch);
    fixture.pioneerRoot = fixture.scratch / "PIONEER";
    createFixture(fixture.pioneerRoot.string());
    fixture.filePath = (fixture.scratch / "Contents" / "Test Track.mp3").string();
    return fixture;
}

// The proposal a plan would carry for this one track, with only the
// fields a case is about turned on.
MetadataRestoreProposal proposalFor(const Fixture &fixture)
{
    MetadataRestoreProposal proposal;
    proposal.storedId = "stored-1";
    proposal.stickTrack.sourceId = "1";
    proposal.stickTrack.filePath = fixture.filePath;
    proposal.stickTrack.title = "Test Track";
    return proposal;
}

void applyChange(const Fixture &fixture, const MetadataRestoreProposal &proposal)
{
    auto &noProgress = seabass::application::NullProgressReporter::instance();
    CancellationToken token;
    const QString root = QString::fromStdString(fixture.pioneerRoot.string());
    SaveContext ctx(token, noProgress, {}, root, {});
    RestoreMetadataChange change("onelibrary", root, "1", proposal);
    const ChangeOutcome outcome = change.apply(ctx);
    assert(outcome.ok);
    assert(!ctx.runFinishHooks(true));
}

}  // namespace

int main()
{
    // Case 1: the regression. A proposal that offers a rating and no
    // cues must leave the cues on the stick exactly as they were. This
    // is the shape a track gets under the skip-all default when its cues
    // conflict with the store's -- cuesOffered false, cues empty -- and
    // the shape that used to hand an empty vector to the writer and
    // delete two hot cues the DJ set last night.
    {
        Fixture fixture = freshFixture("keeps_cues");
        {
            OneLibraryCueWriter writer(fixture.pioneerRoot.string());
            writer.writeCuesForPath(fixture.filePath, liveCues());
        }
        assert(cueCount(fixture.pioneerRoot.string()) == 2);

        MetadataRestoreProposal proposal = proposalFor(fixture);
        proposal.cuesConflict = true;  // the store had cues; they differ
        proposal.ratingOffered = true;
        proposal.rating = 4;
        assert(!proposal.cuesOffered);
        assert(proposal.cues.empty());

        applyChange(fixture, proposal);

        assert(cueCount(fixture.pioneerRoot.string()) == 2);
        assert(storedRating(fixture.pioneerRoot.string()) == 4);
        std::cout << "case 1: a rating-only restore leaves the track's own cues alone\n";
    }

    // Case 2: the same change does write cues when the plan offers them,
    // so case 1 is a guard and not an accidental no-op.
    {
        Fixture fixture = freshFixture("writes_cues");
        {
            OneLibraryCueWriter writer(fixture.pioneerRoot.string());
            writer.writeCuesForPath(fixture.filePath, liveCues());
        }
        assert(cueCount(fixture.pioneerRoot.string()) == 2);

        MetadataRestoreProposal proposal = proposalFor(fixture);
        proposal.cuesOffered = true;
        proposal.cues = storedCues();

        applyChange(fixture, proposal);

        assert(cueCount(fixture.pioneerRoot.string()) == 1);
        std::cout << "case 2: an offered cue set replaces what was there\n";
    }

    // Case 3: the two scales the format decides and no OneLibrary reader
    // reads back. A rating is stars, 0 to 5 -- four stars is a 4 in the
    // column and not Engine's 80 -- and the comment column is djComment,
    // which the schema spells that way and nothing else does.
    {
        Fixture fixture = freshFixture("annotation");
        MetadataRestoreProposal proposal = proposalFor(fixture);
        proposal.ratingOffered = true;
        proposal.rating = 4;
        proposal.commentOffered = true;
        proposal.comment = "mixes into the Larry Heard";

        applyChange(fixture, proposal);

        assert(storedRating(fixture.pioneerRoot.string()) == 4);
        assert(storedComment(fixture.pioneerRoot.string()) == "mixes into the Larry Heard");
        std::cout << "case 3: rating in stars, comment in djComment\n";
    }

    // Case 4: an absent field is left alone rather than cleared, so a
    // restore of one field cannot blank the other.
    {
        Fixture fixture = freshFixture("leaves_absent_alone");
        {
            MetadataRestoreProposal both = proposalFor(fixture);
            both.ratingOffered = true;
            both.rating = 5;
            both.commentOffered = true;
            both.comment = "keep me";
            applyChange(fixture, both);
        }

        MetadataRestoreProposal ratingOnly = proposalFor(fixture);
        ratingOnly.ratingOffered = true;
        ratingOnly.rating = 2;
        applyChange(fixture, ratingOnly);

        assert(storedRating(fixture.pioneerRoot.string()) == 2);
        assert(storedComment(fixture.pioneerRoot.string()) == "keep me");
        std::cout << "case 4: restoring one field leaves the other alone\n";
    }

    // Case 5: what the change calls itself. A proposal with no cues used
    // to describe itself as "Put 0 stored cue(s) back", which named a
    // write it does not make -- the summary the DJ reads before pressing
    // Save has to say what will actually happen.
    {
        Fixture fixture = freshFixture("description");
        MetadataRestoreProposal proposal = proposalFor(fixture);
        proposal.ratingOffered = true;
        proposal.rating = 3;
        proposal.commentOffered = true;
        proposal.comment = "a comment";
        const QString root = QString::fromStdString(fixture.pioneerRoot.string());
        RestoreMetadataChange change("onelibrary", root, "1", proposal);
        const QString description = change.description();
        assert(!description.contains("cue"));
        assert(description.contains("rating and comment"));
        assert(description.contains("Test Track"));

        MetadataRestoreProposal withCues = proposalFor(fixture);
        withCues.cuesOffered = true;
        withCues.cuesFillAGap = true;
        withCues.cues = storedCues();
        RestoreMetadataChange cueChange("onelibrary", root, "1", withCues);
        assert(cueChange.description().contains("1 stored cue(s)"));
        std::cout << "case 5: the description names the fields it will write\n";
    }

    std::cout << "restore_metadata_change_test: all cases passed\n";
    return 0;
}
