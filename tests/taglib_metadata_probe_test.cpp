// Exercises TagLibMetadataProbe against MP3s built byte by byte here,
// rather than checked-in fixtures: the interesting cases are precisely
// the ones a normal encoder never produces on demand (a VBR stream with
// no Xing header), and a hand-built frame makes the expected duration
// arithmetic explicit instead of magic.
#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <taglib/fileref.h>
#include <taglib/tag.h>

#include "infrastructure/audio/taglib_metadata_probe.hpp"

using seabass::infrastructure::audio::TagLibMetadataProbe;
namespace fs = std::filesystem;

namespace
{

// MPEG-1 Layer III, 128 kbps, 44100 Hz, stereo, no padding, no CRC.
constexpr unsigned char FrameHeader[4] = {0xFF, 0xFB, 0x90, 0x00};
constexpr int FrameBytes = 417;  // 144 * 128000 / 44100, truncated
constexpr int SamplesPerFrame = 1152;
constexpr int SampleRate = 44100;

std::vector<unsigned char> silentFrame()
{
    std::vector<unsigned char> frame(FrameBytes, 0);
    std::memcpy(frame.data(), FrameHeader, sizeof(FrameHeader));
    return frame;
}

// A Xing header sits in the first frame's data area at an offset fixed
// by version + channel mode: MPEG-1 stereo is 32 bytes past the 4-byte
// frame header. Only the "frames" and "bytes" fields are filled in,
// which is what a length calculation needs.
std::vector<unsigned char> xingFrame(unsigned frames, unsigned bytes)
{
    std::vector<unsigned char> frame = silentFrame();
    size_t offset = 4 + 32;
    std::memcpy(frame.data() + offset, "Xing", 4);
    offset += 4;
    frame[offset + 3] = 0x03;  // flags: frames present | bytes present
    offset += 4;
    for (int i = 0; i < 4; ++i) {
        frame[offset + i] = static_cast<unsigned char>((frames >> (8 * (3 - i))) & 0xFF);
    }
    offset += 4;
    for (int i = 0; i < 4; ++i) {
        frame[offset + i] = static_cast<unsigned char>((bytes >> (8 * (3 - i))) & 0xFF);
    }
    return frame;
}

void writeFile(const fs::path &path, const std::vector<unsigned char> &data)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
}

// frameCount silent frames, the first optionally carrying a Xing header
// that honestly describes the file.
void writeMp3(const fs::path &path, int frameCount, bool withXing)
{
    std::vector<unsigned char> data;
    for (int i = 0; i < frameCount; ++i) {
        std::vector<unsigned char> frame =
            (i == 0 && withXing) ? xingFrame(static_cast<unsigned>(frameCount),
                                              static_cast<unsigned>(frameCount * FrameBytes))
                                 : silentFrame();
        data.insert(data.end(), frame.begin(), frame.end());
    }
    writeFile(path, data);
}

double expectedSeconds(int frameCount)
{
    return double(frameCount) * SamplesPerFrame / SampleRate;
}

}  // namespace

int main()
{
    fs::path root = fs::temp_directory_path() / "seabass_taglib_metadata_probe_test";
    fs::remove_all(root);
    fs::create_directories(root);

    TagLibMetadataProbe probe;

    // Case 1: a file with a Xing header reports a real length, and must
    // NOT be flagged estimated -- this is the case the unreferenced-file
    // cleanup is allowed to act on.
    {
        const int frames = 40;
        const fs::path path = root / "xing.mp3";
        writeMp3(path, frames, true);

        auto metadata = probe.read(path.string());
        assert(metadata.has_value());
        assert(!metadata->durationIsEstimated);
        assert(std::abs(metadata->durationSeconds - expectedSeconds(frames)) < 0.01);
        assert(metadata->bitrate == 128);
        assert(metadata->sampleRate == SampleRate);
        std::cout << "case 1 (Xing header -> exact duration, not estimated) OK\n";
    }

    // Case 2: the same stream without a Xing header. TagLib falls back to
    // size/bitrate, which happens to be close here because every frame is
    // the same size -- the point is not the number but the flag, since a
    // real VBR file would be seconds out and would then be excluded from
    // deletion. See FileMetadata::durationIsEstimated.
    {
        const int frames = 40;
        const fs::path path = root / "plain.mp3";
        writeMp3(path, frames, false);

        auto metadata = probe.read(path.string());
        assert(metadata.has_value());
        assert(metadata->durationIsEstimated);
        assert(metadata->durationSeconds > 0.0);
        assert(metadata->bitrate == 128);
        std::cout << "case 2 (no Xing header -> duration flagged estimated) OK\n";
    }

    // Case 3: tags come back. Written with TagLib itself, so this tests
    // our own plumbing (field mapping, UTF-8 conversion) rather than
    // re-testing TagLib's ID3 writer. The non-ASCII title is deliberate:
    // TagLib::String::to8Bit() defaults to Latin-1 and would mangle it.
    {
        const fs::path path = root / "tagged.mp3";
        writeMp3(path, 40, true);
        {
            TagLib::FileRef file(path.string().c_str());
            assert(!file.isNull());
            file.tag()->setTitle(TagLib::String("Una Hora M\xc3\xa1s", TagLib::String::UTF8));
            file.tag()->setArtist("The Rocketman");
            file.tag()->setAlbum("Prototype EP");
            assert(file.save());
        }

        auto metadata = probe.read(path.string());
        assert(metadata.has_value());
        assert(metadata->title == "Una Hora M\xc3\xa1s");
        assert(metadata->artist == "The Rocketman");
        assert(metadata->album == "Prototype EP");
        // Tagging must not have cost us the audio properties.
        assert(metadata->durationSeconds > 0.0);
        assert(metadata->bitrate == 128);
        std::cout << "case 3 (title/artist/album round-trip, UTF-8 intact) OK\n";
    }

    // Case 4: an untagged file is a successful read with empty strings,
    // not a failure -- callers still want its duration and bitrate.
    {
        const fs::path path = root / "untagged.mp3";
        writeMp3(path, 40, true);

        auto metadata = probe.read(path.string());
        assert(metadata.has_value());
        assert(metadata->title.empty());
        assert(metadata->artist.empty());
        assert(metadata->durationSeconds > 0.0);
        std::cout << "case 4 (no tags -> empty strings, properties still read) OK\n";
    }

    // Case 5: the two ways a real stick disappoints us -- a file that is
    // not audio at all, and a path that is not there. Both are ordinary
    // answers (nullopt), never exceptions, because a catalog row pointing
    // at a deleted file is normal.
    {
        writeFile(root / "notaudio.mp3", std::vector<unsigned char>{'n', 'o', 'p', 'e'});
        assert(!probe.read((root / "notaudio.mp3").string()).has_value());
        assert(!probe.read((root / "does-not-exist.mp3").string()).has_value());
        std::cout << "case 5 (garbage file and missing file -> nullopt) OK\n";
    }

    fs::remove_all(root);
    std::cout << "all taglib_metadata_probe_test cases passed\n";
    return 0;
}
