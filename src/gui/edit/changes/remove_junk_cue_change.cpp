#include "gui/edit/changes/remove_junk_cue_change.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "gui/edit/changes/change_helpers.hpp"
#include "gui/edit/save_context.hpp"
#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/rekordbox/pdb_lookup.hpp"
#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"

namespace seabass::gui
{

namespace fs = std::filesystem;

namespace
{

std::vector<domain::CuePoint> cuesWithoutJunk(const domain::Track &track)
{
    std::vector<domain::CuePoint> remainingCues;
    for (const auto &c : track.cues) {
        if (!(c.kind == domain::CuePoint::Kind::Memory && c.positionMs == 0.0)) {
            remainingCues.push_back(c);
        }
    }
    return remainingCues;
}

// The writer of one save's stray-cue removals for one format.
struct JunkCueWriterContext
{
    JunkCueWriterContext(const QString &format, const QString &path, SaveContext &ctx)
    {
        std::string root = path.toStdString();
        if (format == "rekordbox") {
            // export.pdb is deliberately NOT backed up here: cue data lives
            // entirely in the per-track ANLZ files and this path never
            // writes export.pdb (see RekordboxCueWriter's own header). The
            // OneLibrary mirror below IS written, so it is what needs the
            // backup -- without it Undo restored the analysis file and left
            // the mirror holding the removed cue.
            rekordbox = std::make_unique<infrastructure::rekordbox::RekordboxCueWriter>(
                root, sharedAnlzPathIndex(ctx, path));
            hasOneLibrary = infrastructure::onelibrary::OneLibraryCueWriter::existsFor(root);
            if (hasOneLibrary) {
                ctx.backupOnce(infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(root), "junk-cue-cleanup");
            }
        } else if (format == "engine") {
            ctx.backupOnce((fs::path(root) / "Database2" / "m.db").string(), "junk-cue-cleanup");
            engine = std::make_unique<infrastructure::engine::LibdjinteropEngineCueWriter>(root);
        } else {
            ctx.backupOnce(infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(root), "junk-cue-cleanup");
            oneLibrary = std::make_unique<infrastructure::onelibrary::OneLibraryCueWriter>(root);
        }
    }

    std::unique_ptr<infrastructure::rekordbox::RekordboxCueWriter> rekordbox;
    std::unique_ptr<infrastructure::engine::LibdjinteropEngineCueWriter> engine;
    std::unique_ptr<infrastructure::onelibrary::OneLibraryCueWriter> oneLibrary;
    bool hasOneLibrary = false;
};

}  // namespace

RemoveJunkCueChange::RemoveJunkCueChange(QString path, domain::Track track)
    : m_path(std::move(path)), m_track(std::move(track))
{
}

QString RemoveJunkCueChange::id() const
{
    return "junk:" + junkKeyFor(m_track);
}

QString RemoveJunkCueChange::owner() const
{
    return QStringLiteral("library-health");
}

QString RemoveJunkCueChange::description() const
{
    return QStringLiteral("Remove the 0:00 memory cue from \"%1\"").arg(QString::fromStdString(m_track.title));
}

QString RemoveJunkCueChange::unit() const
{
    return QStringLiteral("cues");
}

QStringList RemoveJunkCueChange::formatsTouched() const
{
    return {QString::fromStdString(m_track.format)};
}

ChangeOutcome RemoveJunkCueChange::apply(SaveContext &ctx)
{
    const QString format = QString::fromStdString(m_track.format);
    std::string root = m_path.toStdString();
    JunkCueWriterContext &w = ctx.shared<JunkCueWriterContext>(
        "junk:" + m_track.format, [&]() { return std::make_unique<JunkCueWriterContext>(format, m_path, ctx); });
    auto remainingCues = cuesWithoutJunk(m_track);

    if (format == "rekordbox") {
        const auto *pathIndex = sharedAnlzPathIndex(ctx, m_path);
        const uint32_t trackId = static_cast<uint32_t>(std::stoul(m_track.sourceId));
        auto analyzePath = pathIndex ? pathIndex->pathFor(trackId)
                                     : infrastructure::rekordbox::findAnlzPathForTrackId(root, trackId);
        if (analyzePath) {
            ctx.backupOnce(infrastructure::rekordbox::extAnlzPath(root, *analyzePath), "junk-cue-cleanup");
        }
        w.rekordbox->writeHotCues(m_track.sourceId, remainingCues);
        if (w.hasOneLibrary && !m_track.filePath.empty()) {
            try {
                infrastructure::onelibrary::OneLibraryCueWriter oneLibWriter(root);
                oneLibWriter.writeCuesForPath(m_track.filePath, remainingCues);
            } catch (const std::exception &e) {
                ctx.log().record(std::string("junk-cue: OneLibrary cue mirror failed: ") + e.what());
            }
        }
    } else if (format == "engine") {
        w.engine->writeHotCues(m_track.sourceId, remainingCues);
    } else if (format == "onelibrary") {
        w.oneLibrary->writeCuesForPath(m_track.filePath, remainingCues);
    } else {
        return ChangeOutcome::failure("Unknown library format: " + format);
    }
    ctx.log().record("junk-cue: removed 0:00 memory cue from \"" + m_track.title + "\" (id=" + m_track.sourceId + ")");
    return ChangeOutcome::success();
}

}  // namespace seabass::gui
