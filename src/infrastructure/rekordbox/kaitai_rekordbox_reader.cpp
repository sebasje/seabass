#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"

#include <algorithm>
#include <fstream>
#include <unordered_map>

#include "infrastructure/rekordbox/generated/rekordbox_anlz.h"
#include "infrastructure/rekordbox/generated/rekordbox_pdb.h"
#include "infrastructure/rekordbox/pdb_lookup.hpp"

namespace djconvert::infrastructure::rekordbox
{

using Pdb = rekordbox_pdb_t;
using Anlz = rekordbox_anlz_t;
namespace domain = djconvert::domain;

namespace
{

// Extended cue color, when present (color_code/red/green/blue), as
// "#RRGGBB"; falls back to the legacy color_id as a bare number, since
// nxs2-era hot cues store their color there rather than in color_id.
//
// Takes a non-const reference because the generated _is_null_*()/accessor
// methods are not declared const (they lazily compute and cache the field
// on first access).
std::string cueColor(Anlz::cue_extended_entry_t &cue)
{
    if (!cue._is_null_color_red() && !cue._is_null_color_green() && !cue._is_null_color_blue()) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", cue.color_red(), cue.color_green(), cue.color_blue());
        return buf;
    }
    return std::to_string(static_cast<int>(cue.color_id()));
}

std::vector<domain::CuePoint> readCues(const std::string &anlzPath)
{
    std::vector<domain::CuePoint> cues;
    std::ifstream ifs(anlzPath, std::ifstream::binary);
    if (!ifs.is_open()) {
        return cues;
    }

    kaitai::kstream ks(&ifs);
    Anlz anlz(&ks);
    for (const auto &section : *anlz.sections()) {
        if (section->fourcc() != Anlz::SECTION_TAGS_CUES_2) {
            continue;
        }
        auto *tag = dynamic_cast<Anlz::cue_extended_tag_t *>(section->body());
        if (!tag) {
            continue;
        }

        bool isHot = tag->type() == Anlz::CUE_LIST_TYPE_HOT_CUES;
        for (const auto &cue : *tag->cues()) {
            domain::CuePoint cp;
            cp.kind = (isHot && cue->hot_cue() != 0) ? domain::CuePoint::Kind::Hot
                                                      : domain::CuePoint::Kind::Memory;
            cp.hotCueNumber = static_cast<int>(cue->hot_cue());
            cp.positionMs = static_cast<double>(cue->time());
            cp.color = cueColor(*cue);
            if (!cue->_is_null_comment()) {
                cp.comment = cue->comment();
            }
            cues.push_back(std::move(cp));
        }
    }
    return cues;
}

struct PlaylistTreeInfo
{
    std::string name;
    uint32_t parentId = 0;
    bool isFolder = false;
};

// Builds the full path (e.g. "Techno/Peak Time") for a playlist id by
// walking parent_id links up to the root. Guards against a malformed/
// cyclic parent chain rather than looping forever.
std::string playlistPath(uint32_t id, const std::unordered_map<uint32_t, PlaylistTreeInfo> &tree)
{
    std::vector<std::string> parts;
    uint32_t current = id;
    for (int guard = 0; current != 0 && guard < 64; ++guard) {
        auto it = tree.find(current);
        if (it == tree.end()) {
            break;
        }
        parts.push_back(it->second.name);
        current = it->second.parentId;
    }
    std::reverse(parts.begin(), parts.end());

    std::string path;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            path += "/";
        }
        path += parts[i];
    }
    return path;
}

}  // namespace

KaitaiRekordboxReader::KaitaiRekordboxReader(std::string pioneerRoot)
    : m_pioneerRoot(std::move(pioneerRoot))
{
}

std::vector<domain::Track> KaitaiRekordboxReader::readAll()
{
    std::vector<domain::Track> tracks;

    std::string pdbPath = m_pioneerRoot + "/rekordbox/export.pdb";
    std::ifstream ifs(pdbPath, std::ifstream::binary);
    if (!ifs.is_open()) {
        throw std::runtime_error("could not open " + pdbPath);
    }

    kaitai::kstream ks(&ifs);
    Pdb pdb(false, &ks);

    // Playlists: build id -> {name, parent, isFolder} from PLAYLIST_TREE,
    // then track id -> playlist path(s) from PLAYLIST_ENTRIES. Both tables
    // are small, so this is done fully upfront rather than per-track.
    std::unordered_map<uint32_t, PlaylistTreeInfo> playlistTreeById;
    for (const auto &table : *pdb.tables()) {
        if (table->type() != Pdb::PAGE_TYPE_PLAYLIST_TREE) {
            continue;
        }
        forEachDataPage(*table, [&](Pdb::page_t *page) {
            for (const auto &group : *page->row_groups()) {
                for (const auto &row : *group->rows()) {
                    if (!row->present()) {
                        continue;
                    }
                    auto *rowPlaylist = dynamic_cast<Pdb::playlist_tree_row_t *>(row->body());
                    if (!rowPlaylist) {
                        continue;
                    }
                    playlistTreeById[rowPlaylist->id()] = PlaylistTreeInfo{
                        sqlText(rowPlaylist->name()),
                        rowPlaylist->parent_id(),
                        rowPlaylist->raw_is_folder() != 0,
                    };
                }
            }
        });
    }

    std::unordered_map<uint32_t, std::vector<std::string>> playlistsByTrackId;
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
                    auto *rowEntry = dynamic_cast<Pdb::playlist_entry_row_t *>(row->body());
                    if (!rowEntry) {
                        continue;
                    }
                    auto it = playlistTreeById.find(rowEntry->playlist_id());
                    if (it == playlistTreeById.end() || it->second.isFolder) {
                        continue;
                    }
                    playlistsByTrackId[rowEntry->track_id()].push_back(playlistPath(rowEntry->playlist_id(), playlistTreeById));
                }
            }
        });
    }

    // Pre-pass: count rows across the tracks table's pages (cheap -- just
    // reads page headers, no per-row parsing) so progress can show a real
    // percentage rather than an indeterminate count.
    size_t totalRows = 0;
    for (const auto &table : *pdb.tables()) {
        if (table->type() != Pdb::PAGE_TYPE_TRACKS) {
            continue;
        }
        forEachDataPage(*table, [&totalRows](Pdb::page_t *page) { totalRows += page->num_rows(); });
    }

    m_progress->start("Scanning rekordbox tracks", totalRows);
    size_t processed = 0;

    for (const auto &table : *pdb.tables()) {
        if (table->type() != Pdb::PAGE_TYPE_TRACKS) {
            continue;
        }

        forEachDataPage(*table, [&](Pdb::page_t *page) {
            for (const auto &group : *page->row_groups()) {
                for (const auto &row : *group->rows()) {
                    if (!row->present()) {
                        continue;
                    }
                    auto *rowTrack = dynamic_cast<Pdb::track_row_t *>(row->body());
                    if (!rowTrack) {
                        continue;
                    }

                    domain::Track track;
                    track.sourceId = std::to_string(rowTrack->id());
                    track.title = sqlText(rowTrack->title());
                    track.filename = sqlText(rowTrack->filename());
                    track.durationSeconds = rowTrack->duration();
                    track.playCount = rowTrack->play_count();
                    auto playlistsIt = playlistsByTrackId.find(rowTrack->id());
                    if (playlistsIt != playlistsByTrackId.end()) {
                        track.playlists = playlistsIt->second;
                    }

                    std::string analyzePath = sqlText(rowTrack->analyze_path());
                    if (!analyzePath.empty()) {
                        track.cues = readCues(extAnlzPath(m_pioneerRoot, analyzePath));
                    }
                    tracks.push_back(std::move(track));

                    m_progress->tick(++processed);
                }
            }
        });
    }

    m_progress->finish();
    return tracks;
}

}  // namespace djconvert::infrastructure::rekordbox
