#pragma once

#include <string>
#include <vector>

namespace seabass::infrastructure
{

// Checks that an anonymized export is what its own manifest promises.
//
// The manifest tells a contributor their original file paths are removed
// entirely. That promise was not kept for a long time: every analysis file
// still embedded the real path, and the Engine filename, album, genre and
// label columns were never scrubbed at all. Those specific holes are now
// fixed, and this exists so the promise is *checked* rather than trusted,
// on every export, before anyone is asked to send one.
//
// It runs on the staging directory, before the zip is written, so a
// failure can refuse to produce the file at all rather than describing a
// leak that already exists on disk.
//
// What it will not do is prove anonymity in general. It checks structure:
// that only the intended files are present, and that every value it
// samples has the shape of a placeholder rather than of real text. A
// scrubber that wrote a convincing-looking constant would pass. It is a
// tripwire for the failure this project has actually had -- a field or a
// file nobody remembered to scrub -- not a proof.
struct AnonymizationVerification
{
    bool ok = false;
    // One line per problem, each naming the file or field and what was
    // found, in the order they were found. A non-empty list means
    // something real leaked.
    std::vector<std::string> problems;
    // Things that stopped a check from running rather than failing it --
    // most often a catalog the readers cannot parse, which a hand-built
    // minimal test fixture genuinely is. Kept separate from problems
    // because "we could not look" and "we looked and found real data" are
    // different facts and only the second must block an export. That a
    // real export reads back correctly is asserted by the corpus runner,
    // which scans both catalogs and checks their counts.
    std::vector<std::string> warnings;
    int analysisFilesChecked = 0;
    int rekordboxTracksSampled = 0;
    int engineTracksSampled = 0;
    int oneLibraryTracksSampled = 0;

    // Everything, ready to print or to put in an error message.
    std::string describe() const;
};

// `exportRoot` is the directory holding MANIFEST.txt, rekordbox/ and
// engine/ -- the staging directory AnonymizeLibrary builds before zipping.
// Sampling is capped so this stays fast enough to run on every export;
// analysis files are checked in full, because they are where the leak was.
AnonymizationVerification verifyAnonymizedExport(const std::string &exportRoot, int trackSampleSize = 200);

// True when `value` could be a placeholder of `kind` produced by
// anonymizationPlaceholder(), including one the rekordbox writer truncated
// to fit the real field's byte length. Exposed for the verifier's own test.
bool looksLikeHashPlaceholder(const std::string &value, const std::string &kind);

// True when `value` could be an anonymizationFilenamePlaceholder(), i.e. a
// short hex hash plus an extension, possibly truncated.
bool looksLikeFilenamePlaceholder(const std::string &value);

// True when `value` could be an indexed placeholder like "Artist 007",
// possibly truncated.
bool looksLikeIndexedPlaceholder(const std::string &value, const std::string &kind);

}  // namespace seabass::infrastructure
