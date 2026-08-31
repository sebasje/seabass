#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "application/ports/progress_reporter.hpp"

namespace seabass::application
{

struct AnonymizationOptions
{
    // Unset (default) keeps every real track; set to cap the output at
    // this many, dropping the rest -- see BRAINSTORM.md's "For
    // Developers, Maintainers and Testing" section and the plan this
    // implements for why this is an always-available user choice
    // rather than an on-by-default sampling step.
    std::optional<size_t> maxTracks;

    // Free text captured verbatim into MANIFEST.txt (and shown back to
    // the caller before anything is sent anywhere) -- what hardware the
    // submitter uses, and anything they'd like tested. Not validated or
    // interpreted; the caller is responsible for warning that this text
    // may be published alongside the rest of the export.
    std::string hardware;
    std::string notes;
};

struct AnonymizationSummary
{
    bool rekordboxAttempted = false;
    int rekordboxTracksKept = 0;
    int rekordboxTracksDropped = 0;
    int rekordboxArtistsRenamed = 0;
    int rekordboxPlaylistsRenamed = 0;
    std::string rekordboxError;  // empty on success

    bool engineAttempted = false;
    int engineTracksKept = 0;
    int engineTracksDropped = 0;
    int enginePlaylistsRenamed = 0;
    std::string engineError;  // empty on success

    std::string manifestPath;
    std::uintmax_t outputSizeBytes = 0;
    std::uintmax_t estimatedZippedBytes = 0;

    // True if at least one catalog was attempted and none of the
    // attempted ones failed.
    bool succeeded() const;
};

// Produces a de-identified, structurally-real copy of one or both real
// libraries at outputDir/rekordbox and outputDir/engine (whichever of
// rekordboxRoot/engineRoot is set), plus outputDir/MANIFEST.txt
// documenting exactly what's included -- see
// infrastructure::rekordbox::anonymizeRekordboxLibrary and
// infrastructure::engine::anonymizeEngineLibrary for what each edit
// actually does to its catalog. Shared by both the maintainer fixture-
// regeneration workflow and the user-facing "export anonymized library"
// CLI command -- see seabass-cli's own `anonymize` subcommand.
class AnonymizeLibrary
{
public:
    AnonymizationSummary execute(const std::optional<std::string> &rekordboxRoot,
                                  const std::optional<std::string> &engineRoot, const std::string &outputDir,
                                  const AnonymizationOptions &options,
                                  ProgressReporter &reporter = NullProgressReporter::instance());
};

}  // namespace seabass::application
