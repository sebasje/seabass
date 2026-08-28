#include "infrastructure/rekordbox/pdb_row_writer.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>

#include "infrastructure/durable_file_write.hpp"
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
constexpr size_t HeaderLenPageOffset = 4;
constexpr size_t HeaderNumTablesOffset = 8;
//   page: [4]gap + u4(page_index) + u4(type/type_ext) + page_ref(u4 next_page)
//         + u4(sequence) -> sequence at byte 16, relative to page start.
constexpr size_t PageSequenceOffset = 16;
// playlist_entry_row: u4(entry_index) + u4(track_id) + u4(playlist_id) --
// track_id starts right after entry_index.
constexpr size_t PlaylistEntryTrackIdOffset = 4;

// Loose but real sanity bounds -- real rekordbox exports use len_page
// 4096; this just rejects an obviously-wrong-format file (wrong file
// entirely, truncated header, garbage) before any offset math trusts
// it, not a strict format validator.
constexpr uint32_t MinPlausibleLenPage = 256;
constexpr uint32_t MaxPlausibleLenPage = 1u << 20;  // 1 MiB
constexpr uint32_t MaxPlausibleNumTables = 64;       // real files have ~20

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

// Rejects a file too small/implausible to even hold a real root header,
// or whose len_page/num_tables are outside any sane real-world range --
// catches "this isn't really an export.pdb" (or a truncated/corrupt
// one) before any byte offset computed from those values is trusted.
void validateLooksLikeRealPdb(const std::string &buffer)
{
    constexpr size_t MinRootHeaderSize = 28;  // through the end of `gap`, before any table entries
    if (buffer.size() < MinRootHeaderSize) {
        throw std::runtime_error("not a valid export.pdb: file too small to hold a root header");
    }
    uint32_t lenPage = readU32LE(buffer, HeaderLenPageOffset);
    if (lenPage < MinPlausibleLenPage || lenPage > MaxPlausibleLenPage) {
        throw std::runtime_error("not a valid export.pdb: implausible len_page in header");
    }
    uint32_t numTables = readU32LE(buffer, HeaderNumTablesOffset);
    if (numTables == 0 || numTables > MaxPlausibleNumTables) {
        throw std::runtime_error("not a valid export.pdb: implausible num_tables in header");
    }
    if (buffer.size() < static_cast<size_t>(lenPage)) {
        throw std::runtime_error("not a valid export.pdb: file smaller than its own declared page size");
    }
}

// Re-parses the fully edited buffer with the real generated parser --
// a structural sanity check that a bug in this class produced something
// still readable, run before the result is ever written to disk. Only
// walks the two tables this class can edit; a full library scan isn't
// needed to catch a broken row/page/header.
bool reparsesCleanly(const std::string &buffer)
{
    try {
        std::istringstream iss(buffer);
        kaitai::kstream ks(&iss);
        Pdb pdb(false, &ks);
        for (const auto &table : *pdb.tables()) {
            if (table->type() != Pdb::PAGE_TYPE_TRACKS && table->type() != Pdb::PAGE_TYPE_PLAYLIST_ENTRIES) {
                continue;
            }
            forEachDataPage(*table, [&](Pdb::page_t *page) {
                for (const auto &group : *page->row_groups()) {
                    for (const auto &row : *group->rows()) {
                        if (row->present()) {
                            (void)row->body();  // force the row to actually parse
                        }
                    }
                }
            });
        }
        return true;
    } catch (const std::exception &) {
        return false;
    }
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

// One playlist_entry row matching a given track id, plus which
// playlist it's in -- what reassignPlaylistMemberships() needs to
// decide repoint-vs-remove per row. Unlike findRow(), collects every
// match in one page-walk rather than stopping at the first.
struct PlaylistEntryMatch
{
    uint32_t playlistId = 0;
    FoundRow row;
};

std::vector<PlaylistEntryMatch> findAllPlaylistEntriesForTrack(const std::string &buffer, uint32_t trackId)
{
    std::vector<PlaylistEntryMatch> matches;
    std::istringstream iss(buffer);
    kaitai::kstream ks(&iss);
    Pdb pdb(false, &ks);

    for (const auto &table : *pdb.tables()) {
        if (table->type() != Pdb::PAGE_TYPE_PLAYLIST_ENTRIES) {
            continue;
        }
        forEachDataPage(*table, [&](Pdb::page_t *page) {
            for (const auto &group : *page->row_groups()) {
                for (const auto &row : *group->rows()) {
                    if (!row->present()) {
                        continue;
                    }
                    auto *e = dynamic_cast<Pdb::playlist_entry_row_t *>(row->body());
                    if (!e || e->track_id() != trackId) {
                        continue;
                    }
                    PlaylistEntryMatch m;
                    m.playlistId = e->playlist_id();
                    m.row.pageIndex = page->page_index();
                    m.row.presentFlagsOffset =
                        static_cast<size_t>(pdb.len_page()) * page->page_index() + static_cast<size_t>(group->base()) - 4;
                    m.row.rowIndexBit = row->row_index();
                    m.row.rowBodyOffset =
                        static_cast<size_t>(pdb.len_page()) * page->page_index() + static_cast<size_t>(row->row_base());
                    matches.push_back(m);
                }
            }
        });
    }
    return matches;
}

}  // namespace

PdbRowWriter::PdbRowWriter(std::string pdbPath) : m_pdbPath(std::move(pdbPath)), m_buffer(readWholeFile(m_pdbPath))
{
    validateLooksLikeRealPdb(m_buffer);
    m_originalFileSize = fs::file_size(m_pdbPath);
    m_originalMtime = fs::last_write_time(m_pdbPath);
}

bool PdbRowWriter::trackExists(uint32_t trackId) const
{
    return findRow(m_buffer, Pdb::PAGE_TYPE_TRACKS, [&](kaitai::kstruct *body) {
               auto *t = dynamic_cast<Pdb::track_row_t *>(body);
               return t != nullptr && t->id() == trackId;
           }).has_value();
}

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

size_t PdbRowWriter::reassignPlaylistMemberships(uint32_t oldTrackId, uint32_t newTrackId)
{
    auto oldEntries = findAllPlaylistEntriesForTrack(m_buffer, oldTrackId);
    if (oldEntries.empty()) {
        return 0;
    }

    std::set<uint32_t> newTrackAlreadyIn;
    for (const auto &m : findAllPlaylistEntriesForTrack(m_buffer, newTrackId)) {
        newTrackAlreadyIn.insert(m.playlistId);
    }

    size_t affected = 0;
    for (const auto &m : oldEntries) {
        if (newTrackAlreadyIn.count(m.playlistId)) {
            // newTrackId is already in this playlist -- just drop the
            // oldTrackId entry rather than create a duplicate.
            uint16_t flags = readU16LE(m_buffer, m.row.presentFlagsOffset);
            flags &= static_cast<uint16_t>(~(static_cast<uint16_t>(1) << m.row.rowIndexBit));
            writeU16LE(m_buffer, m.row.presentFlagsOffset, flags);
        } else {
            writeU32LE(m_buffer, m.row.rowBodyOffset + PlaylistEntryTrackIdOffset, newTrackId);
            // If oldTrackId somehow had more than one entry in the same
            // playlist, don't repoint the second one too -- one is
            // enough to preserve membership, the rest would be dupes.
            newTrackAlreadyIn.insert(m.playlistId);
        }
        m_editedPageIndices.insert(m.row.pageIndex);
        ++affected;
    }
    return affected;
}

bool PdbRowWriter::commit()
{
    if (m_editedPageIndices.empty()) {
        return false;
    }

    // Staleness check: if the real file changed since we read it (size
    // or mtime), something else touched it in the meantime -- our
    // in-memory copy is no longer a safe base to overwrite it with.
    std::error_code statEc;
    auto currentSize = fs::file_size(m_pdbPath, statEc);
    auto currentMtime = fs::last_write_time(m_pdbPath, statEc);
    if (statEc || currentSize != m_originalFileSize || currentMtime != m_originalMtime) {
        return false;
    }

    uint32_t lenPage = readU32LE(m_buffer, HeaderLenPageOffset);
    uint32_t currentSequence = readU32LE(m_buffer, HeaderSequenceOffset);
    for (uint32_t pageIndex : m_editedPageIndices) {
        size_t pageSequenceOffset = static_cast<size_t>(lenPage) * pageIndex + PageSequenceOffset;
        writeU32LE(m_buffer, pageSequenceOffset, currentSequence);
    }
    writeU32LE(m_buffer, HeaderSequenceOffset, currentSequence + 1);

    // A bug in this class producing a broken file must never reach
    // disk -- confirm the edited result is still structurally readable
    // before writing it anywhere.
    if (!reparsesCleanly(m_buffer)) {
        return false;
    }

    if (!writeFileDurablyAtomic(m_pdbPath, m_buffer)) {
        return false;
    }

    m_editedPageIndices.clear();
    return true;
}

}  // namespace djconvert::infrastructure::rekordbox
