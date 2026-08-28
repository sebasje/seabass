#include "infrastructure/rekordbox/pdb_row_writer.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include "infrastructure/rekordbox/generated/rekordbox_pdb.h"
#include "infrastructure/rekordbox/little_endian.hpp"
#include "infrastructure/rekordbox/pdb_lookup.hpp"

namespace djconvert::infrastructure::rekordbox
{

using Pdb = rekordbox_pdb_t;
namespace fs = std::filesystem;

namespace
{

// Byte offsets within the root header / a page header, derived directly
// from specs/rekordbox_pdb.ksy's seq field layout (not from a generated
// accessor -- these two fields have no dedicated "position" instance,
// only a value accessor, unlike the row/group offsets below which are
// read straight off the trusted parser).
//   root: u4(unknown) + u4(len_page) + u4(num_tables) + u4(next_unused_page)
//         + u4(unknown) + u4(sequence) -> sequence at byte 20.
constexpr size_t HeaderSequenceOffset = 20;
//   page: [4]gap + u4(page_index) + u4(type/type_ext) + page_ref(u4 next_page)
//         + u4(sequence) -> sequence at byte 16, relative to page start.
constexpr size_t PageSequenceOffset = 16;
// playlist_entry_row: u4(entry_index) + u4(track_id) + u4(playlist_id) --
// track_id starts right after entry_index.
constexpr size_t PlaylistEntryTrackIdOffset = 4;

std::string readWholeFile(const std::string &path)
{
    std::ifstream ifs(path, std::ifstream::binary);
    if (!ifs.is_open()) {
        throw std::runtime_error("could not open " + path);
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

// What findRow() needs to hand back to a caller so it can overwrite
// either the row's presence bit or a fixed-size field inside its body --
// every offset is absolute within the file/buffer.
struct FoundRow
{
    uint32_t pageIndex = 0;
    size_t presentFlagsOffset = 0;
    uint16_t rowIndexBit = 0;
    size_t rowBodyOffset = 0;
};

// Locates the first present row of `wantedType` for which `matches`
// returns true, using the exact same trusted, already-tested parser and
// page-walking helper (forEachDataPage(), pdb_lookup.hpp) the read path
// uses -- against `buffer` (this session's in-memory, possibly
// already-edited copy) rather than a fresh file handle, so a caller
// always sees its own prior edits within the same session.
std::optional<FoundRow> findRow(const std::string &buffer, Pdb::page_type_t wantedType,
                                 const std::function<bool(kaitai::kstruct *)> &matches)
{
    std::optional<FoundRow> found;
    std::istringstream iss(buffer);
    kaitai::kstream ks(&iss);
    Pdb pdb(false, &ks);

    for (const auto &table : *pdb.tables()) {
        if (found || table->type() != wantedType) {
            continue;
        }
        forEachDataPage(*table, [&](Pdb::page_t *page) {
            if (found) {
                return;
            }
            for (const auto &group : *page->row_groups()) {
                if (found) {
                    return;
                }
                for (const auto &row : *group->rows()) {
                    if (!row->present() || !matches(row->body())) {
                        continue;
                    }
                    FoundRow f;
                    f.pageIndex = page->page_index();
                    f.presentFlagsOffset =
                        static_cast<size_t>(pdb.len_page()) * page->page_index() + static_cast<size_t>(group->base()) - 4;
                    f.rowIndexBit = row->row_index();
                    f.rowBodyOffset = static_cast<size_t>(pdb.len_page()) * page->page_index() + static_cast<size_t>(row->row_base());
                    found = f;
                    return;
                }
            }
        });
    }
    return found;
}

}  // namespace

PdbRowWriter::PdbRowWriter(std::string pdbPath) : m_pdbPath(std::move(pdbPath)), m_buffer(readWholeFile(m_pdbPath)) {}

bool PdbRowWriter::removeTrack(uint32_t trackId)
{
    auto found = findRow(m_buffer, Pdb::PAGE_TYPE_TRACKS, [&](kaitai::kstruct *body) {
        auto *t = dynamic_cast<Pdb::track_row_t *>(body);
        return t != nullptr && t->id() == trackId;
    });
    if (!found) {
        return false;
    }
    uint16_t flags = readU16LE(m_buffer, found->presentFlagsOffset);
    flags &= static_cast<uint16_t>(~(static_cast<uint16_t>(1) << found->rowIndexBit));
    writeU16LE(m_buffer, found->presentFlagsOffset, flags);
    m_editedPageIndices.insert(found->pageIndex);
    return true;
}

bool PdbRowWriter::removePlaylistEntry(uint32_t playlistId, uint32_t trackId)
{
    auto found = findRow(m_buffer, Pdb::PAGE_TYPE_PLAYLIST_ENTRIES, [&](kaitai::kstruct *body) {
        auto *e = dynamic_cast<Pdb::playlist_entry_row_t *>(body);
        return e != nullptr && e->playlist_id() == playlistId && e->track_id() == trackId;
    });
    if (!found) {
        return false;
    }
    uint16_t flags = readU16LE(m_buffer, found->presentFlagsOffset);
    flags &= static_cast<uint16_t>(~(static_cast<uint16_t>(1) << found->rowIndexBit));
    writeU16LE(m_buffer, found->presentFlagsOffset, flags);
    m_editedPageIndices.insert(found->pageIndex);
    return true;
}

bool PdbRowWriter::repointPlaylistEntry(uint32_t playlistId, uint32_t oldTrackId, uint32_t newTrackId)
{
    auto found = findRow(m_buffer, Pdb::PAGE_TYPE_PLAYLIST_ENTRIES, [&](kaitai::kstruct *body) {
        auto *e = dynamic_cast<Pdb::playlist_entry_row_t *>(body);
        return e != nullptr && e->playlist_id() == playlistId && e->track_id() == oldTrackId;
    });
    if (!found) {
        return false;
    }
    writeU32LE(m_buffer, found->rowBodyOffset + PlaylistEntryTrackIdOffset, newTrackId);
    m_editedPageIndices.insert(found->pageIndex);
    return true;
}

bool PdbRowWriter::commit()
{
    if (m_editedPageIndices.empty()) {
        return false;
    }

    uint32_t lenPage = readU32LE(m_buffer, 4);
    uint32_t currentSequence = readU32LE(m_buffer, HeaderSequenceOffset);
    for (uint32_t pageIndex : m_editedPageIndices) {
        size_t pageSequenceOffset = static_cast<size_t>(lenPage) * pageIndex + PageSequenceOffset;
        writeU32LE(m_buffer, pageSequenceOffset, currentSequence);
    }
    writeU32LE(m_buffer, HeaderSequenceOffset, currentSequence + 1);

    std::string tempPath = m_pdbPath + ".tmp-djconvert-cleanup";
    {
        std::ofstream ofs(tempPath, std::ofstream::binary | std::ofstream::trunc);
        if (!ofs.is_open()) {
            return false;
        }
        ofs.write(m_buffer.data(), static_cast<std::streamsize>(m_buffer.size()));
        if (!ofs.good()) {
            return false;
        }
    }

    std::error_code ec;
    fs::rename(tempPath, m_pdbPath, ec);
    if (ec) {
        fs::remove(tempPath, ec);
        return false;
    }

    m_editedPageIndices.clear();
    return true;
}

}  // namespace djconvert::infrastructure::rekordbox
