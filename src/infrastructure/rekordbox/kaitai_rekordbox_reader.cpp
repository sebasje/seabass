#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"

#include <fstream>

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
