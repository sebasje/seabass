#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "application/ports/cue_writer.hpp"
#include "application/ports/library_cleanup_writer.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"

namespace seabass::gui
{

// application::CueWriter identifies a track by its format's own
// sourceId; infrastructure::onelibrary::OneLibraryCueWriter deliberately
// doesn't (see its own class comment: content_id is a separate id space
// from export.pdb/m.db's own track ids, file path is the one identifier
// every format actually shares). This adapts the latter to the former,
// using a sourceId->filePath map built from the tracks actually being
// written -- see DuplicatesController's own use of this for why no
// track REMOVAL is involved here (this only ever backs the non-
// destructive "copy cues" flow; Clean Up Duplicates' destructive removal
// doesn't support OneLibrary as a primary target yet, since
// OneLibraryCueWriter::removeTrackByPath() deletes playlist-membership
// rows outright rather than reassigning them to a survivor the way
// application::LibraryCleanupWriter::removeTrackReplacingWith()
// requires -- see cleanup_controller.cpp's own comment).
class OneLibraryCueWriterAdapter : public application::CueWriter
{
public:
    // realStickRoot: only needed when pioneerRoot points at a relocated
    // copy of exportLibrary.db (a local scratch build, see
    // OneLibraryCueWriter's own constructor comment) rather than the
    // real stick -- forwarded through unchanged so file-path-to-
    // content.path resolution still uses the real stick's layout.
    OneLibraryCueWriterAdapter(std::string pioneerRoot, std::unordered_map<std::string, std::string> sourceIdToPath,
                                std::optional<std::string> realStickRoot = std::nullopt)
        : m_pioneerRoot(std::move(pioneerRoot)),
          m_sourceIdToPath(std::move(sourceIdToPath)),
          m_realStickRoot(std::move(realStickRoot))
    {
    }

    void writeHotCues(const std::string &trackSourceId, const std::vector<domain::CuePoint> &cues) override
    {
        auto it = m_sourceIdToPath.find(trackSourceId);
        if (it == m_sourceIdToPath.end()) {
            throw std::runtime_error("onelibrary: no known file path for source id=" + trackSourceId);
        }
        infrastructure::onelibrary::OneLibraryCueWriter writer(m_pioneerRoot, m_realStickRoot);
        writer.writeCuesForPath(it->second, cues);
    }

private:
    std::string m_pioneerRoot;
    std::unordered_map<std::string, std::string> m_sourceIdToPath;
    std::optional<std::string> m_realStickRoot;
};

// Same sourceId->filePath adaptation as OneLibraryCueWriterAdapter above,
// for application::LibraryCleanupWriter instead of CueWriter -- backs
// Clean Up Duplicates when OneLibrary is the primary scanned format.
// Only usable now because OneLibraryCueWriter::
// removeTrackByPathReplacingWith() exists (it didn't when OneLibrary
// support was first added here -- see that method's own comment on the
// real playlist-membership-loss bug its absence caused).
class OneLibraryCleanupWriterAdapter : public application::LibraryCleanupWriter
{
public:
    // realStickRoot: see OneLibraryCueWriterAdapter's own comment above.
    OneLibraryCleanupWriterAdapter(std::string pioneerRoot,
                                    std::unordered_map<std::string, std::string> sourceIdToPath,
                                    std::optional<std::string> realStickRoot = std::nullopt)
        : m_pioneerRoot(std::move(pioneerRoot)),
          m_sourceIdToPath(std::move(sourceIdToPath)),
          m_realStickRoot(std::move(realStickRoot))
    {
    }

    void removeTrackReplacingWith(const std::string &doomedTrackId, const std::string &survivorTrackId) override
    {
        auto doomedIt = m_sourceIdToPath.find(doomedTrackId);
        if (doomedIt == m_sourceIdToPath.end()) {
            throw std::runtime_error("onelibrary: no known file path for source id=" + doomedTrackId);
        }
        auto survivorIt = m_sourceIdToPath.find(survivorTrackId);
        if (survivorIt == m_sourceIdToPath.end()) {
            throw std::runtime_error("onelibrary: no known file path for source id=" + survivorTrackId);
        }
        infrastructure::onelibrary::OneLibraryCueWriter writer(m_pioneerRoot, m_realStickRoot);
        writer.removeTrackByPathReplacingWith(doomedIt->second, survivorIt->second);
    }

private:
    std::string m_pioneerRoot;
    std::unordered_map<std::string, std::string> m_sourceIdToPath;
    std::optional<std::string> m_realStickRoot;
};

}  // namespace seabass::gui
