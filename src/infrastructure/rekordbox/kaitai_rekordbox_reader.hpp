#pragma once

#include <string>
#include <vector>

#include "application/ports/library_reader.hpp"
#include "domain/track.hpp"

namespace seabass::infrastructure::rekordbox
{

// The same "no RGB -> fall back to legacy color_id, but color_id == 0
// means no color at all" logic KaitaiRekordboxReader uses internally,
// pulled out as a free function of plain values (rather than the
// Kaitai-generated cue_extended_entry_t, which isn't practical to
// construct outside a real parsed file) so it has direct unit test
// coverage. See kaitai_rekordbox_reader.cpp's own comment on why
// color_id == 0 -> "" specifically matters: it's what makes an
// uncolored rekordbox cue compare equal to an uncolored Engine cue in
// domain::cueSetsEqual().
std::string rekordboxCueColor(bool hasRgb, unsigned char r, unsigned char g, unsigned char b, int colorId);

// Reads a rekordbox USB export (PIONEER/rekordbox/export.pdb +
// PIONEER/USBANLZ/**/ANLZ*.{DAT,EXT}) using the Kaitai-generated parser in
// infrastructure/rekordbox/generated/, built from crate-digger's specs
// (see specs/README.md). This is the only place that knows about the Kaitai
// runtime or the on-disk rekordbox format.
class KaitaiRekordboxReader : public application::LibraryReader
{
public:
    // pioneerRoot is the directory containing "rekordbox/" and "USBANLZ/"
    // (i.e. the "PIONEER" folder itself).
    explicit KaitaiRekordboxReader(std::string pioneerRoot);

    std::vector<domain::Track> readAll() override;

private:
    std::string m_pioneerRoot;
};

}  // namespace seabass::infrastructure::rekordbox
