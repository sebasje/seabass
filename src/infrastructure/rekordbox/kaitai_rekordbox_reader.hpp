#pragma once

#include <string>
#include <vector>

#include "application/ports/library_reader.hpp"
#include "domain/track.hpp"

namespace seabass::infrastructure::rekordbox
{

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
