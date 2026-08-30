#pragma once

#include <string>

#include "application/ports/cue_writer.hpp"

namespace seabass::infrastructure::rekordbox
{

// Writes hot cues into a rekordbox USB export by rewriting the target
// track's ANLZ .EXT file's PCO2 (hot cues) section -- see
// anlz_cue_codec.hpp for the format confidence notes and
// anlz_file.hpp for why this can be done safely without touching any
// section we don't understand. export.pdb itself is never modified: cue
// data lives entirely in the ANLZ files.
//
// This is the least-proven part of the whole project (see the plan's
// Risks section) -- validated so far by round-tripping real files
// byte-for-byte and cross-checking newly written cues with the
// independent, existing Kaitai-based reader, but NOT yet verified against
// real rekordbox software or real CDJ/XDJ hardware. Treat output as
// untrusted for a real gig until you've confirmed that yourself.
class RekordboxCueWriter : public application::CueWriter
{
public:
    explicit RekordboxCueWriter(std::string pioneerRoot);

    void writeHotCues(const std::string &trackSourceId, const std::vector<domain::CuePoint> &cues) override;

private:
    std::string m_pioneerRoot;
};

}  // namespace seabass::infrastructure::rekordbox
