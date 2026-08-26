#include "infrastructure/rekordbox/pdb_lookup.hpp"

#include <fstream>
#include <stdexcept>

namespace djconvert::infrastructure::rekordbox
{

using Pdb = rekordbox_pdb_t;

std::string sqlText(Pdb::device_sql_string_t *s)
{
    if (!s) {
        return "";
    }
    auto *body = s->body();
    if (auto *a = dynamic_cast<Pdb::device_sql_short_ascii_t *>(body)) {
        return a->text();
    }
    if (auto *a = dynamic_cast<Pdb::device_sql_long_ascii_t *>(body)) {
        return a->text();
    }
    if (auto *a = dynamic_cast<Pdb::device_sql_long_utf16le_t *>(body)) {
        return a->text();
    }
    return "";
}

std::string extAnlzPath(const std::string &pioneerRoot, const std::string &analyzePath)
{
    std::string rel = analyzePath;
    size_t pos = rel.find("/PIONEER/");
    if (pos != std::string::npos) {
        rel = rel.substr(pos + std::string("/PIONEER/").size());
    }
    size_t dot = rel.rfind(".DAT");
    if (dot != std::string::npos) {
        rel = rel.substr(0, dot) + ".EXT";
    }
    return pioneerRoot + "/" + rel;
}

std::optional<std::string> findAnlzPathForTrackId(const std::string &pioneerRoot, uint32_t trackId)
{
    std::string pdbPath = pioneerRoot + "/rekordbox/export.pdb";
    std::ifstream ifs(pdbPath, std::ifstream::binary);
    if (!ifs.is_open()) {
        throw std::runtime_error("could not open " + pdbPath);
    }

    kaitai::kstream ks(&ifs);
    Pdb pdb(false, &ks);

    std::optional<std::string> result;
    for (const auto &table : *pdb.tables()) {
        if (table->type() != Pdb::PAGE_TYPE_TRACKS || result) {
            continue;
        }
        forEachDataPage(*table, [&](Pdb::page_t *page) {
            if (result) {
                return;
            }
            for (const auto &group : *page->row_groups()) {
                for (const auto &row : *group->rows()) {
                    if (!row->present()) {
                        continue;
                    }
                    auto *rowTrack = dynamic_cast<Pdb::track_row_t *>(row->body());
                    if (!rowTrack || rowTrack->id() != trackId) {
                        continue;
                    }
                    std::string analyzePath = sqlText(rowTrack->analyze_path());
                    if (!analyzePath.empty()) {
                        result = analyzePath;
                    }
                    return;
                }
            }
        });
    }
    return result;
}

}  // namespace djconvert::infrastructure::rekordbox
