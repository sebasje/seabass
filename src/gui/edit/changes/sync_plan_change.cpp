#include "gui/edit/changes/sync_plan_change.hpp"

#include <filesystem>
#include <memory>
#include <string>

#include "gui/edit/changes/change_helpers.hpp"
#include "gui/edit/format_write_session.hpp"
#include "gui/edit/save_context.hpp"
#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/rekordbox/pdb_lookup.hpp"
#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"

namespace seabass::gui
{

namespace fs = std::filesystem;
using domain::SyncPlan;

namespace
{

QString formatLabel(const std::string &format)
{
    if (format == "engine") return "Engine";
    if (format == "onelibrary") return "OneLibrary";
    return "Rekordbox";
}

// The writer for one target catalog in one save, created by the first
// SyncPlanChange that needs it and shared by the rest
// (SaveContext::shared): the catalog's database is backed up once, and a
// big batch against Engine/OneLibrary goes through FormatWriteSession's
// scratch copy. rekordbox cue writes go to small per-track .ANLZ files
// (backed up per item by the change), so its hint is 0: never scratch.
struct SyncFormatWriter
{
    SyncFormatWriter(const std::string &format, const std::string &catalogPath, int itemCountHint, SaveContext &ctx,
                     const infrastructure::rekordbox::AnlzPathIndex *pathIndex)
        : session(format, catalogPath, format == "rekordbox" ? 0 : itemCountHint, "sync", ctx)
    {
        if (format == "rekordbox") {
            rekordbox = std::make_unique<infrastructure::rekordbox::RekordboxCueWriter>(session.realRoot(),
                                                                                            pathIndex);
        } else if (format == "engine") {
            engine = std::make_unique<infrastructure::engine::LibdjinteropEngineCueWriter>(session.writeRoot());
        } else {
            // realStickRoot is passed explicitly because writeRoot() may
            // be the scratch copy, whose parent is just a temp directory
            // -- content.path lookups need the real stick's layout.
            oneLibrary = std::make_unique<infrastructure::onelibrary::OneLibraryCueWriter>(
                session.writeRoot(), fs::path(catalogPath).parent_path().string());
        }
    }

    FormatWriteSession session;
    std::unique_ptr<infrastructure::rekordbox::RekordboxCueWriter> rekordbox;
    std::unique_ptr<infrastructure::engine::LibdjinteropEngineCueWriter> engine;
    std::unique_ptr<infrastructure::onelibrary::OneLibraryCueWriter> oneLibrary;
};

}  // namespace

SyncPlanChange::SyncPlanChange(QString rekordboxPath, QString enginePath, SyncPlan plan, int itemCountHint)
    : m_rekordboxPath(std::move(rekordboxPath)),
      m_enginePath(std::move(enginePath)),
      m_plan(std::move(plan)),
      m_itemCountHint(itemCountHint)
{
}

const domain::Track &SyncPlanChange::target() const
{
    return m_plan.direction == SyncPlan::Direction::ToB ? m_plan.match.trackB : m_plan.match.trackA;
}

const domain::Track &SyncPlanChange::source() const
{
    return m_plan.direction == SyncPlan::Direction::ToB ? m_plan.match.trackA : m_plan.match.trackB;
}

QString SyncPlanChange::id() const
{
    return "sync:" + QString::fromStdString(target().format) + ":" + QString::fromStdString(target().sourceId);
}

QString SyncPlanChange::description() const
{
    return QStringLiteral("Copy %1 from %2 \"%3\" to %4")
        .arg(describeCues(m_plan.cuesToApply), formatLabel(source().format), QString::fromStdString(source().title),
             formatLabel(target().format));
}

QString SyncPlanChange::unit() const
{
    return QStringLiteral("tracks");
}

QStringList SyncPlanChange::formatsTouched() const
{
    return {QString::fromStdString(target().format)};
}

ChangeOutcome SyncPlanChange::apply(SaveContext &ctx)
{
    const domain::Track &tgt = target();
    const domain::Track &src = source();
    const std::string &targetFormat = tgt.format;
    // rekordbox and OneLibrary share the same PIONEER root/path.
    QString path = targetFormat == "engine" ? m_enginePath : m_rekordboxPath;
    if (path.isEmpty()) {
        return ChangeOutcome::failure("No " + formatLabel(targetFormat) + " catalog path is known for this stick.");
    }
    std::string catalogPath = path.toStdString();
    SyncFormatWriter &writer = ctx.shared<SyncFormatWriter>("sync:" + targetFormat, [&]() {
        return std::make_unique<SyncFormatWriter>(targetFormat, catalogPath, m_itemCountHint, ctx,
                                                 sharedAnlzPathIndex(ctx, path));
    });

    if (targetFormat == "rekordbox") {
        const auto *pathIndex = sharedAnlzPathIndex(ctx, path);
        const uint32_t trackId = static_cast<uint32_t>(std::stoul(tgt.sourceId));
        auto analyzePath = pathIndex ? pathIndex->pathFor(trackId)
                                     : infrastructure::rekordbox::findAnlzPathForTrackId(catalogPath, trackId);
        if (analyzePath) {
            ctx.backupOnce(infrastructure::rekordbox::extAnlzPath(catalogPath, *analyzePath), "sync");
        }
        writer.rekordbox->writeHotCues(tgt.sourceId, m_plan.cuesToApply);
    } else if (targetFormat == "engine") {
        writer.engine->writeHotCues(tgt.sourceId, m_plan.cuesToApply);
    } else {
        writer.oneLibrary->writeCuesForPath(tgt.filePath, m_plan.cuesToApply);
    }
    writer.session.noteItemApplied();
    ctx.log().record("sync: copied cues (" + describeCues(m_plan.cuesToApply).toStdString() + ") from " + src.format
                     + " track \"" + src.title + "\" (id=" + src.sourceId + ") to " + targetFormat + " track id="
                     + tgt.sourceId);
    return ChangeOutcome::success();
}

}  // namespace seabass::gui
