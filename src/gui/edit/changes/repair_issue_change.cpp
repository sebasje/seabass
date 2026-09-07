#include "gui/edit/changes/repair_issue_change.hpp"

#include <filesystem>
#include <memory>
#include <string>

#include "gui/edit/changes/change_helpers.hpp"
#include "gui/edit/format_write_session.hpp"
#include "gui/edit/save_context.hpp"
#include "infrastructure/engine/libdjinterop_engine_cleanup_writer.hpp"
#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/rekordbox/pdb_lookup.hpp"
#include "infrastructure/rekordbox/rekordbox_cleanup_writer.hpp"
#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"

namespace seabass::gui
{

namespace fs = std::filesystem;

namespace
{

// The writers of one save's repairs for one format (SaveContext::shared):
// the format's cue writer and cleanup writer on the FormatWriteSession's
// write root (a scratch copy for a big batch), plus, for rekordbox, the
// best-effort OneLibrary mirror. rekordbox's cue merges go to small
// per-track .ANLZ files (backed up per item by the change); only its row
// removals touch export.pdb.
struct RepairWriterContext
{
    RepairWriterContext(const QString &format, const QString &path, int itemCountHint, SaveContext &ctx)
        : session(format.toStdString(), path.toStdString(), itemCountHint, "consistency-repair", ctx)
    {
        std::string root = path.toStdString();
        if (format == "rekordbox") {
            rekordboxCues = std::make_unique<infrastructure::rekordbox::RekordboxCueWriter>(
                root, sharedAnlzPathIndex(ctx, path));
            rekordboxCleanup = std::make_unique<infrastructure::rekordbox::RekordboxCleanupWriter>(session.writeRoot());
            hasOneLibrary = infrastructure::onelibrary::OneLibraryCueWriter::existsFor(root);
        } else if (format == "engine") {
            engineCues = std::make_unique<infrastructure::engine::LibdjinteropEngineCueWriter>(session.writeRoot());
            engineCleanup =
                std::make_unique<infrastructure::engine::LibdjinteropEngineCleanupWriter>(session.writeRoot());
        } else {
            oneLibrary = std::make_unique<infrastructure::onelibrary::OneLibraryCueWriter>(
                session.writeRoot(), fs::path(root).parent_path().string());
        }
    }

    FormatWriteSession session;
    std::unique_ptr<infrastructure::rekordbox::RekordboxCueWriter> rekordboxCues;
    std::unique_ptr<infrastructure::rekordbox::RekordboxCleanupWriter> rekordboxCleanup;
    std::unique_ptr<infrastructure::engine::LibdjinteropEngineCueWriter> engineCues;
    std::unique_ptr<infrastructure::engine::LibdjinteropEngineCleanupWriter> engineCleanup;
    std::unique_ptr<infrastructure::onelibrary::OneLibraryCueWriter> oneLibrary;
    bool hasOneLibrary = false;
};

}  // namespace

RepairIssueChange::RepairIssueChange(QString path, domain::LibraryConsistencyIssue issue, int itemCountHint)
    : m_path(std::move(path)), m_issue(std::move(issue)), m_itemCountHint(itemCountHint)
{
}

QString RepairIssueChange::id() const
{
    return "repair:" + issueKeyFor(m_issue);
}

QString RepairIssueChange::owner() const
{
    return QStringLiteral("library-health");
}

QString RepairIssueChange::description() const
{
    QString survivor = m_issue.survivor ? QString::fromStdString(m_issue.survivor->title) : QString("?");
    QString what = m_issue.survivorCues.empty()
        ? QString()
        : QStringLiteral("merge %1 cue(s) onto it, ").arg(m_issue.survivorCues.size());
    return QStringLiteral("Repair \"%1\" (%2): %3remove %4 broken row(s)")
        .arg(survivor, issueFormat(m_issue), what)
        .arg(m_issue.brokenGroup.size());
}

QString RepairIssueChange::unit() const
{
    return QStringLiteral("rows");
}

QStringList RepairIssueChange::formatsTouched() const
{
    return {issueFormat(m_issue)};
}

ChangeOutcome RepairIssueChange::apply(SaveContext &ctx)
{
    if (!m_issue.survivor) {
        return ChangeOutcome::failure("This row has no survivor to repair onto.");
    }
    const QString format = issueFormat(m_issue);
    const auto &survivor = *m_issue.survivor;
    std::string root = m_path.toStdString();
    RepairWriterContext &w = ctx.shared<RepairWriterContext>(
        "repair:" + format.toStdString(),
        [&]() { return std::make_unique<RepairWriterContext>(format, m_path, m_itemCountHint, ctx); });

    if (format == "rekordbox") {
        if (!m_issue.survivorCues.empty()) {
            const auto *pathIndex = sharedAnlzPathIndex(ctx, m_path);
            const uint32_t survivorId = static_cast<uint32_t>(std::stoul(survivor.sourceId));
            auto analyzePath = pathIndex ? pathIndex->pathFor(survivorId)
                                         : infrastructure::rekordbox::findAnlzPathForTrackId(root, survivorId);
            if (analyzePath) {
                ctx.backupOnce(infrastructure::rekordbox::extAnlzPath(root, *analyzePath), "consistency-repair");
            }
            w.rekordboxCues->writeHotCues(survivor.sourceId, m_issue.survivorCues);
            ctx.log().record("consistency: merged cues onto survivor id=" + survivor.sourceId);
            // Best-effort mirror, same convention as Clean Up's own
            // survivor-cue mirror block.
            if (w.hasOneLibrary && !survivor.filePath.empty()) {
                try {
                    sharedOneLibraryWriter(ctx, root).writeCuesForPath(survivor.filePath, m_issue.survivorCues);
                } catch (const std::exception &e) {
                    ctx.log().record(std::string("consistency: OneLibrary cue mirror failed: ") + e.what());
                }
            }
        }
        for (const auto &broken : m_issue.brokenGroup) {
            w.rekordboxCleanup->removeTrackReplacingWith(broken.sourceId, survivor.sourceId);
            w.session.noteItemApplied();
            ctx.log().record("consistency: removed broken row id=" + broken.sourceId + " (\"" + broken.title
                             + "\"), replaced by survivor id=" + survivor.sourceId);
            if (w.hasOneLibrary && !broken.filePath.empty() && !survivor.filePath.empty()) {
                try {
                    // Reassigns playlist membership onto the survivor
                    // instead of dropping it -- see OneLibraryCueWriter::
                    // removeTrackByPathReplacingWith()'s own comment.
                    sharedOneLibraryWriter(ctx, root).removeTrackByPathReplacingWith(broken.filePath,
                                                                                     survivor.filePath);
                } catch (const std::exception &e) {
                    ctx.log().record(std::string("consistency: OneLibrary row removal failed: ") + e.what());
                }
            }
        }
    } else if (format == "engine") {
        if (!m_issue.survivorCues.empty()) {
            w.engineCues->writeHotCues(survivor.sourceId, m_issue.survivorCues);
            w.session.noteItemApplied();
            ctx.log().record("consistency: merged cues onto survivor id=" + survivor.sourceId);
        }
        for (const auto &broken : m_issue.brokenGroup) {
            w.engineCleanup->removeTrackReplacingWith(broken.sourceId, survivor.sourceId);
            w.session.noteItemApplied();
            ctx.log().record("consistency: removed broken row id=" + broken.sourceId + " (\"" + broken.title
                             + "\"), replaced by survivor id=" + survivor.sourceId);
        }
    } else if (format == "onelibrary") {
        if (!m_issue.survivorCues.empty()) {
            w.oneLibrary->writeCuesForPath(survivor.filePath, m_issue.survivorCues);
            w.session.noteItemApplied();
            ctx.log().record("consistency: merged cues onto survivor \"" + survivor.title + "\"");
        }
        for (const auto &broken : m_issue.brokenGroup) {
            w.oneLibrary->removeTrackByPathReplacingWith(broken.filePath, survivor.filePath);
            w.session.noteItemApplied();
            ctx.log().record("consistency: removed broken row \"" + broken.title + "\"");
        }
    } else {
        return ChangeOutcome::failure("Unknown library format: " + format);
    }
    return ChangeOutcome::success();
}

}  // namespace seabass::gui
