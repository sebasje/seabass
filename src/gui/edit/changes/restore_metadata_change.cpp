#include "gui/edit/changes/restore_metadata_change.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

#include "application/ports/cue_writer.hpp"
#include "gui/edit/changes/change_helpers.hpp"
#include "gui/edit/save_context.hpp"
#include "gui/onelibrary_cue_writer_adapter.hpp"
#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/rekordbox/pdb_lookup.hpp"
#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"

namespace seabass::gui
{

namespace fs = std::filesystem;

namespace
{

constexpr const char *LogTag = "metadata-restore";

// One format's writer for a whole save: the format's cue writer plus,
// for rekordbox, the best-effort OneLibrary mirror. Created by the first
// change that needs it and shared by the rest (SaveContext::shared), so
// a restore of four hundred tracks opens each database once rather than
// four hundred times.
struct RestoreWriterContext
{
    RestoreWriterContext(const QString &format, const QString &path, SaveContext &ctx,
                          std::unordered_map<std::string, std::string> oneLibraryPaths)
    {
        const std::string root = path.toStdString();
        if (format == "rekordbox") {
            writer = std::make_unique<infrastructure::rekordbox::RekordboxCueWriter>(
                root, sharedAnlzPathIndex(ctx, path));
            // The two rekordbox formats are one library, so a cue
            // written to one and not the other leaves them disagreeing.
            // Best-effort: see OneLibraryCueWriter's class comment.
            if (infrastructure::onelibrary::OneLibraryCueWriter::existsFor(root)) {
                ctx.backupOnce(infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(root), LogTag);
                try {
                    mirror = std::make_unique<infrastructure::onelibrary::OneLibraryCueWriter>(root);
                } catch (const std::exception &e) {
                    ctx.log().record(std::string(LogTag) + ": could not open OneLibrary: " + e.what());
                }
            }
        } else if (format == "engine") {
            ctx.backupOnce((fs::path(root) / "Database2" / "m.db").string(), LogTag);
            writer = std::make_unique<infrastructure::engine::LibdjinteropEngineCueWriter>(root);
        } else {
            ctx.backupOnce(infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(root), LogTag);
            writer = std::make_unique<OneLibraryCueWriterAdapter>(root, std::move(oneLibraryPaths));
        }
    }

    std::unique_ptr<application::CueWriter> writer;
    std::unique_ptr<infrastructure::onelibrary::OneLibraryCueWriter> mirror;
};

}  // namespace

RestoreMetadataChange::RestoreMetadataChange(QString format, QString path, QString sourceId,
                                              domain::MetadataRestoreProposal proposal)
    : m_format(std::move(format)), m_path(std::move(path)), m_sourceId(std::move(sourceId)),
      m_proposal(std::move(proposal))
{
}

QString RestoreMetadataChange::id() const
{
    return "metadata-restore:" + m_format + ":" + m_sourceId;
}

QString RestoreMetadataChange::description() const
{
    const QString title = QString::fromStdString(m_proposal.stickTrack.title.empty()
                                                      ? m_proposal.stickTrack.filename
                                                      : m_proposal.stickTrack.title);
    const int cues = static_cast<int>(m_proposal.cues.size());
    if (m_proposal.cuesFillAGap) {
        return QStringLiteral("Put %1 stored cue(s) back on \"%2\"").arg(cues).arg(title);
    }
    return QStringLiteral("Replace the cues on \"%1\" with the %2 stored one(s)").arg(title).arg(cues);
}

QString RestoreMetadataChange::verb() const
{
    return QStringLiteral("restored");
}

QString RestoreMetadataChange::unit() const
{
    return QStringLiteral("tracks");
}

QStringList RestoreMetadataChange::formatsTouched() const
{
    return {m_format};
}

// The format decides which catalog is written; rekordbox additionally
// writes this track's own analysis file, whose path needs the shared
// index apply() would build anyway. Same shape as MergeCuesChange.
std::vector<BackupTarget> RestoreMetadataChange::filesToBackup(SaveContext &ctx) const
{
    std::vector<BackupTarget> targets;
    const domain::TrackId track{m_format.toStdString(), m_sourceId.toStdString()};
    for (const auto &file :
         filesWrittenFor(WriteScope{.catalogRows = false, .oneLibraryMirror = true}, track, m_path, ctx)) {
        targets.push_back({file, LogTag});
    }
    return targets;
}

ChangeOutcome RestoreMetadataChange::apply(SaveContext &ctx)
{
    const domain::Track &track = m_proposal.stickTrack;
    const std::string sourceId = m_sourceId.toStdString();

    std::string key = "metadata-restore:" + m_format.toStdString();
    std::unordered_map<std::string, std::string> oneLibraryPaths;
    if (m_format == "onelibrary") {
        oneLibraryPaths[sourceId] = track.filePath;
        key += ":" + sourceId;
    }
    RestoreWriterContext &writer = ctx.shared<RestoreWriterContext>(
        key, [&]() { return std::make_unique<RestoreWriterContext>(m_format, m_path, ctx, oneLibraryPaths); });

    if (m_format == "rekordbox") {
        // rekordbox keeps cues per track, in ANLZ files, so the file to
        // back up is this track's own.
        const std::string root = m_path.toStdString();
        const auto *pathIndex = sharedAnlzPathIndex(ctx, m_path);
        const auto trackId = static_cast<uint32_t>(std::stoul(sourceId));
        auto analyzePath = pathIndex ? pathIndex->pathFor(trackId)
                                     : infrastructure::rekordbox::findAnlzPathForTrackId(root, trackId);
        if (analyzePath) {
            ctx.backupOnce(infrastructure::rekordbox::extAnlzPath(root, *analyzePath), LogTag);
        }
    }

    writer.writer->writeHotCues(sourceId, m_proposal.cues);
    ctx.log().record(std::string(LogTag) + ": wrote " + std::to_string(m_proposal.cues.size()) +
                     " stored cue(s) onto " + m_format.toStdString() + " track id=" + sourceId + " (\"" +
                     track.title + "\")");

    if (writer.mirror && !track.filePath.empty()) {
        try {
            writer.mirror->writeCuesForPath(track.filePath, m_proposal.cues);
            ctx.log().record(std::string(LogTag) + ": also wrote them into OneLibrary (id=" + sourceId + ")");
        } catch (const std::exception &e) {
            ctx.log().record(std::string(LogTag) + ": OneLibrary cue write failed for \"" + track.title +
                             "\": " + e.what());
        }
    }
    return ChangeOutcome::success();
}

}  // namespace seabass::gui
