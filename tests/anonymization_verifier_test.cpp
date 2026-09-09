// The verifier is the last thing between a DJ's real library metadata and
// a zip they upload to a stranger. Until now the only thing exercising it
// was the `verify_anonymized_export` tool pointed at the committed
// fixture -- which is already clean, so the tool had only ever run its
// happy path. Nothing had ever shown it refusing anything.
//
// A gate that has never been seen to close is not known to close. So each
// case here plants a specific leak in a copy of the fixture and asserts
// the verifier catches that leak, plus one case asserting the clean copy
// still passes.
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "infrastructure/anonymization_verifier.hpp"
#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"
#include "infrastructure/rekordbox/pdb_row_writer.hpp"

namespace fs = std::filesystem;
using namespace seabass;

namespace
{

bool mentions(const std::vector<std::string> &problems, const std::string &needle)
{
    for (const auto &p : problems) {
        if (p.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void dump(const char *what, const infrastructure::AnonymizationVerification &v)
{
    std::cerr << what << ": " << v.problems.size() << " problem(s)\n";
    for (const auto &p : v.problems) {
        std::cerr << "    " << p << "\n";
    }
}

}  // namespace

int main(int argc, char **argv)
{
    const fs::path fixture = argc > 1 ? fs::path(argv[1]) : fs::path("tests/fixtures/anonymized_library");
    if (!fs::is_directory(fixture)) {
        std::cerr << "fixture not found at " << fixture << " -- run from the repository root\n";
        return 1;
    }

    const fs::path root = fs::temp_directory_path() / "seabass_anonymization_verifier_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    const fs::path copy = root / "export";
    fs::copy(fixture, copy, fs::copy_options::recursive, ec);
    assert(!ec);

    const fs::path pdb = copy / "rekordbox" / "rekordbox" / "export.pdb";
    const fs::path pristinePdb = root / "export.pdb.pristine";
    fs::copy_file(pdb, pristinePdb, fs::copy_options::overwrite_existing, ec);
    assert(!ec);
    auto restorePdb = [&] {
        std::error_code e;
        fs::copy_file(pristinePdb, pdb, fs::copy_options::overwrite_existing, e);
        assert(!e);
    };

    // The clean copy must pass, or every "it refused" below proves
    // nothing -- a verifier that refuses everything would satisfy them all.
    {
        auto v = infrastructure::verifyAnonymizedExport(copy.string());
        if (!v.problems.empty()) {
            dump("clean copy", v);
        }
        assert(v.problems.empty());
        assert(v.rekordboxTracksSampled > 0);
        std::cout << "case 1 (the clean fixture passes, so refusals below mean something) OK\n";
    }

    // Layout: anything the manifest does not account for is a leak,
    // because it was never anonymized by anything.
    {
        const fs::path stray = copy / "my-real-library.txt";
        std::ofstream(stray) << "real content";
        auto v = infrastructure::verifyAnonymizedExport(copy.string());
        assert(!v.problems.empty());
        assert(mentions(v.problems, "my-real-library.txt"));
        fs::remove(stray);
        std::cout << "case 2 (an unexpected file at the top level is refused) OK\n";
    }

    {
        const fs::path stray = copy / "rekordbox" / "exportExt.pdb";
        std::ofstream(stray) << "My Tag vocabulary, never anonymized";
        auto v = infrastructure::verifyAnonymizedExport(copy.string());
        assert(!v.problems.empty());
        assert(mentions(v.problems, "exportExt.pdb"));
        fs::remove(stray);
        std::cout << "case 3 (an unexpected entry in the rekordbox tree is refused) OK\n";
    }

    // Content: a real title and a real filename on a track the verifier
    // samples. Written through the app's own writer so the row is a real
    // row, not a hand-built one.
    {
        infrastructure::rekordbox::KaitaiRekordboxReader reader((copy / "rekordbox").string());
        const auto tracks = reader.readAll();
        assert(!tracks.empty());
        const std::uint32_t id = static_cast<std::uint32_t>(std::stoul(tracks.front().sourceId));

        infrastructure::rekordbox::PdbRowWriter writer(pdb.string());
        infrastructure::rekordbox::PdbRowWriter::TrackTextOverride text;
        text.title = "Blue Monday";
        assert(writer.overwriteTrackText(id, text));
        assert(writer.commit());

        auto v = infrastructure::verifyAnonymizedExport(copy.string());
        if (v.problems.empty()) {
            dump("planted title", v);
        }
        assert(mentions(v.problems, "still has a real title"));
        restorePdb();
        std::cout << "case 4 (a real title on a sampled track is refused) OK\n";
    }

    {
        infrastructure::rekordbox::KaitaiRekordboxReader reader((copy / "rekordbox").string());
        const auto tracks = reader.readAll();
        const std::uint32_t id = static_cast<std::uint32_t>(std::stoul(tracks.front().sourceId));

        infrastructure::rekordbox::PdbRowWriter writer(pdb.string());
        infrastructure::rekordbox::PdbRowWriter::TrackTextOverride text;
        text.filename = "realsong.mp3";
        assert(writer.overwriteTrackText(id, text));
        assert(writer.commit());

        auto v = infrastructure::verifyAnonymizedExport(copy.string());
        assert(mentions(v.problems, "still has a real filename") || mentions(v.problems, "still has a real file path"));
        restorePdb();
        std::cout << "case 5 (a real filename on a sampled track is refused) OK\n";
    }

    // The case that matters most: the same leak, on the LAST track.
    //
    // The verifier used to check 200 tracks per catalog by default, which
    // on this 1161-track fixture left 83% of the rows unexamined -- a
    // real title planted at the end passed the gate reporting zero
    // problems. Both halves are asserted here: the default now catches
    // it, and a deliberately small sample still misses it, which is the
    // evidence for why the default changed.
    {
        infrastructure::rekordbox::KaitaiRekordboxReader reader((copy / "rekordbox").string());
        const auto tracks = reader.readAll();
        assert(tracks.size() > 400);
        const std::uint32_t lateId =
            static_cast<std::uint32_t>(std::stoul(tracks[tracks.size() - 1].sourceId));

        infrastructure::rekordbox::PdbRowWriter writer(pdb.string());
        infrastructure::rekordbox::PdbRowWriter::TrackTextOverride text;
        text.title = "Blue Monday";
        assert(writer.overwriteTrackText(lateId, text));
        assert(writer.commit());

        auto byDefault = infrastructure::verifyAnonymizedExport(copy.string());
        if (!mentions(byDefault.problems, "still has a real title")) {
            dump("leak on the last track, default settings", byDefault);
        }
        assert(mentions(byDefault.problems, "still has a real title"));

        auto sampled200 = infrastructure::verifyAnonymizedExport(copy.string(), 200);
        assert(!mentions(sampled200.problems, "still has a real title"));

        std::cout << "case 6 (a leak on the last track is caught by default, and missed at sample=200) OK\n";
        restorePdb();
    }

    // Every track really is examined, not merely more of them.
    {
        infrastructure::rekordbox::KaitaiRekordboxReader reader((copy / "rekordbox").string());
        const auto tracks = reader.readAll();
        auto v = infrastructure::verifyAnonymizedExport(copy.string());
        assert(v.problems.empty());
        assert(v.rekordboxTracksSampled == static_cast<int>(tracks.size()));
        std::cout << "case 7 (the default sweeps every track in the catalog: "
                  << v.rekordboxTracksSampled << ") OK\n";
    }

    fs::remove_all(root, ec);
    std::cout << "all cases passed\n";
    return 0;
}
