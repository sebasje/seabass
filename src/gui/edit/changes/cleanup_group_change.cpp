#include "gui/edit/changes/cleanup_group_change.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "application/ports/cue_writer.hpp"
#include "application/ports/library_cleanup_writer.hpp"
#include "gui/edit/changes/change_helpers.hpp"
#include "gui/edit/format_write_session.hpp"
#include "gui/edit/save_context.hpp"
#include "gui/onelibrary_cue_writer_adapter.hpp"
#include "infrastructure/cleanup/pending_deletion_manifest.hpp"
#include "infrastructure/engine/libdjinterop_engine_cleanup_writer.hpp"
#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/rekordbox/pdb_lookup.hpp"
#include "infrastructure/rekordbox/pdb_row_writer.hpp"
#include "infrastructure/rekordbox/rekordbox_cleanup_writer.hpp"
#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"

namespace seabass::gui
{

namespace fs = std::filesystem;

namespace
{

// One format's writers for a clean-up. Which files a cleanup overwrites is
// answered by filesWrittenFor() in change_helpers -- one definition, so
// what the save declares and what it backs up cannot disagree.
struct CleanupFormatContext
{
    std::unique_ptr<application::CueWriter> cueWriter;
    std::unique_ptr<application::LibraryCleanupWriter> cleanupWriter;
    // Non-empty only for rekordbox, used to best-effort also write
    // cues into OneLibrary/exportLibrary.db if it exists alongside
    // export.pdb on this stick.
    std::string pioneerRoot;
};

// oneLibrarySourceIdToPath is only consulted when format == "onelibrary"
// (the adapters' own comments explain why sourceId alone isn't enough for
// this format).
//
// writeRoot: where the format's *shared database* writers (cleanupWriter
// always; cueWriter too for engine/onelibrary, whose cue writes hit that
// same shared file) should actually read/write, when the save staged it
// to fast local scratch first (FormatWriteSession) -- defaults to `path`
// itself, i.e. write straight to the stick. rekordbox's cueWriter is
// deliberately NOT redirected: it writes small per-track .ANLZ files,
// not the shared export.pdb, so staging buys it nothing (same reasoning
// as SyncPlanChange's rekordbox branch).
CleanupFormatContext makeContext(const QString &format, const QString &path,
                                 const std::unordered_map<std::string, std::string> &oneLibrarySourceIdToPath = {},
                                 std::optional<std::string> writeRoot = std::nullopt)
{
    CleanupFormatContext ctx;
    if (format == "rekordbox") {
        std::string pioneerRoot = path.toStdString();
        ctx.cueWriter = std::make_unique<infrastructure::rekordbox::RekordboxCueWriter>(pioneerRoot);
        ctx.cleanupWriter =
            std::make_unique<infrastructure::rekordbox::RekordboxCleanupWriter>(writeRoot.value_or(pioneerRoot));
        ctx.pioneerRoot = pioneerRoot;
    } else if (format == "engine") {
        std::string engineLibraryPath = path.toStdString();
        std::string effectivePath = writeRoot.value_or(engineLibraryPath);
        ctx.cueWriter = std::make_unique<infrastructure::engine::LibdjinteropEngineCueWriter>(effectivePath);
        ctx.cleanupWriter = std::make_unique<infrastructure::engine::LibdjinteropEngineCleanupWriter>(effectivePath);
        std::string engineDbFile = (fs::path(engineLibraryPath) / "Database2" / "m.db").string();
    } else {
        // onelibrary. `path` here is the PIONEER root, same as the
        // rekordbox branch -- OneLibrary lives alongside export.pdb.
        // realStickRoot is passed explicitly (not left to the adapters'
        // own pioneerRoot-parent default) because writeRoot may be a
        // scratch copy, whose parent is just a temp directory, not the
        // stick -- content.path lookups need the *real* stick root
        // regardless of where exportLibrary.db itself is being read from
        // right now.
        std::string pioneerRoot = path.toStdString();
        std::string effectivePath = writeRoot.value_or(pioneerRoot);
        std::string realStickRoot = fs::path(pioneerRoot).parent_path().string();
        ctx.cueWriter =
            std::make_unique<OneLibraryCueWriterAdapter>(effectivePath, oneLibrarySourceIdToPath, realStickRoot);
        ctx.cleanupWriter = std::make_unique<OneLibraryCleanupWriterAdapter>(effectivePath, oneLibrarySourceIdToPath,
                                                                             realStickRoot);
    }
    return ctx;
}

// The writers of one save's clean-ups for one format (SaveContext::
// shared): the database backed up once, a scratch copy for a big batch
// (FormatWriteSession), the format's writers on that write root, and the
// pending-deletion manifest every removed copy is appended to. OneLibrary's
// adapters are bound to the tracks they write, so that format gets one
// context per group.
struct CleanupWriterContext
{
    CleanupWriterContext(const QString &format, const QString &path, int itemCountHint, SaveContext &ctx,
                         const std::unordered_map<std::string, std::string> &oneLibrarySourceIdToPath)
        : session(format.toStdString(), path.toStdString(), itemCountHint, "duplicate-file-cleanup", ctx),
          manifest((fs::path(path.toStdString()).parent_path() / ".seabass-pending-deletions.jsonl").string())
    {
        std::optional<std::string> writeRoot;
        if (session.usesScratch()) {
            writeRoot = session.writeRoot();
        }
        context = makeContext(format, path, oneLibrarySourceIdToPath, writeRoot);
        effectiveRoot = session.writeRoot();
        realStickRootForOneLib = fs::path(path.toStdString()).parent_path().string();
        // Named in every pending-deletion entry, so the review page can
        // point at the backup that still holds the removed row.
        dbBackupId = ctx.backupIdOf(session.databaseFile());
    }

    FormatWriteSession session;
    infrastructure::cleanup::PendingDeletionManifest manifest;
    CleanupFormatContext context;
    std::string effectiveRoot;
    std::string realStickRootForOneLib;
    std::string dbBackupId;
};

}  // namespace

CleanupGroupChange::CleanupGroupChange(QString format, QString path, domain::DuplicateCleanupPlan plan,
                                        int itemCountHint)
    : m_format(std::move(format)), m_path(std::move(path)), m_plan(std::move(plan)), m_itemCountHint(itemCountHint)
{
}

QString CleanupGroupChange::id() const
{
    return "cleanup:" + m_format + ":" + QString::fromStdString(m_plan.survivor.sourceId);
}

QString CleanupGroupChange::description() const
{
    int newCues =
        static_cast<int>(m_plan.mergedCuesForSurvivor.size()) - static_cast<int>(m_plan.survivor.cues.size());
    return QStringLiteral("Clean up \"%1\": keep %2, remove %3 cop%4%5")
        .arg(QString::fromStdString(m_plan.survivor.title), QString::fromStdString(m_plan.survivor.filename))
        .arg(m_plan.toRemove.size())
        .arg(m_plan.toRemove.size() == 1 ? "y" : "ies")
        .arg(newCues > 0 ? QStringLiteral(" (%1 cue(s) preserved)").arg(newCues) : QString());
}

QString CleanupGroupChange::unit() const
{
    return QStringLiteral("groups");
}

QStringList CleanupGroupChange::formatsTouched() const
{
    return {m_format};
}

namespace
{

// A stray file has no catalog row to remove and no sourceId a writer
// would recognise -- its sourceId is a file path -- so it never reaches a
// cleanup writer. It gets a manifest line routing it to "Delete Orphaned
// Files" and nothing else. unreferencedFilesHeldBack gets no line at all:
// the planner refused those, and this is not the place to second-guess it.
void recordStrayFilesForDeletion(infrastructure::cleanup::PendingDeletionManifest &manifest,
                                 const domain::DuplicateCleanupPlan &plan, const std::string &format,
                                 application::OperationLog &log)
{
    for (const auto &stray : plan.unreferencedFilesToDelete) {
        infrastructure::cleanup::PendingDeletion pending;
        pending.format = format;
        pending.filePath = stray.filePath;
        pending.title = stray.title;
        pending.artist = stray.artist;
        manifest.append(pending);
        log.record("cleanup: no catalog references \"" + stray.filePath
                   + "\"; recorded for deletion, kept copy is id=" + plan.survivor.sourceId);
    }
}

// True when applying this plan would write nothing to any catalog: no row
// to remove, no cue to merge onto the survivor, no field to fill in.
//
// Not only the all-stray group. The commonest shape on a real stick is one
// catalogued row plus one stray copy of it: the catalogued row survives,
// so the plan's only removal is a file, and a stray carries no cues, bpm,
// key or artwork to propagate. Such a plan is a line in the
// pending-deletion manifest and nothing else -- opening a write session
// for it would back up the database, possibly copy the whole file to
// scratch and back, and change not one byte of it.
bool writesToCatalog(const domain::DuplicateCleanupPlan &plan)
{
    if (plan.mergedCuesForSurvivor.size() > plan.survivor.cues.size()) {
        return true;
    }
    if (plan.bpmForSurvivor || plan.keyForSurvivor || plan.artworkPathForSurvivor) {
        return true;
    }
    return std::any_of(plan.toRemove.begin(), plan.toRemove.end(),
                       [](const domain::Track &t) { return !t.isUnreferenced; });
}

}  // namespace

// A cleanup merges cues onto the survivor, then removes the doomed rows and
// repoints playlists -- catalog rows and the OneLibrary copy of them.
//
// The doomed tracks' own analysis files are deliberately absent: a cleanup
// removes their catalog rows, it does not write their cue files. Only the
// survivor's is written.
//
// A plan that writes to no catalog backs nothing up. It is a line in the
// pending-deletion manifest and nothing else, so declaring files for it
// would have Undo restore files this save never touched.
std::vector<BackupTarget> CleanupGroupChange::filesToBackup(SaveContext &ctx) const
{
    if (!writesToCatalog(m_plan)) {
        return {};
    }
    const WriteScope scope{.cueData = true, .catalogRows = true, .oneLibraryMirror = true};
    std::vector<BackupTarget> targets;
    for (const auto &file :
         filesWrittenFor(scope, {m_format.toStdString(), m_plan.survivor.sourceId}, m_path, ctx)) {
        targets.push_back({file, "duplicate-file-cleanup"});
    }
    return targets;
}

ChangeOutcome CleanupGroupChange::apply(SaveContext &ctx)
{
    const auto &plan = m_plan;

    // A plan that writes to no catalog is one or more manifest lines and
    // nothing else, so it deliberately opens no write session: the
    // manifest is append-per-call precisely so it needs none.
    if (!writesToCatalog(plan)) {
        infrastructure::cleanup::PendingDeletionManifest manifest(
            (fs::path(m_path.toStdString()).parent_path() / ".seabass-pending-deletions.jsonl").string());
        recordStrayFilesForDeletion(manifest, plan, m_format.toStdString(), ctx.log());
        return ChangeOutcome::success();
    }

    std::string key = "cleanup:" + m_format.toStdString();
    std::unordered_map<std::string, std::string> oneLibrarySourceIdToPath;
    if (m_format == "onelibrary") {
        oneLibrarySourceIdToPath[plan.survivor.sourceId] = plan.survivor.filePath;
        for (const auto &doomed : plan.toRemove) {
            oneLibrarySourceIdToPath[doomed.sourceId] = doomed.filePath;
        }
        key += ":" + plan.survivor.sourceId;
    }
    CleanupWriterContext &w = ctx.shared<CleanupWriterContext>(key, [&]() {
        return std::make_unique<CleanupWriterContext>(m_format, m_path, m_itemCountHint, ctx,
                                                      oneLibrarySourceIdToPath);
    });
    CleanupFormatContext &fc = w.context;
    application::OperationLog &log = ctx.log();
    const QString &format = m_format;

    // Fallback for anything filesToBackup() did not declare, resolved the
    // same way so it cannot disagree with it -- and so it costs no second
    // export.pdb parse.
    for (const auto &f :
         filesWrittenFor({.cueData = true, .catalogRows = true, .oneLibraryMirror = true},
                         {m_format.toStdString(), plan.survivor.sourceId}, m_path, ctx)) {
        ctx.backupOnce(f, "duplicate-file-cleanup");
    }

    // sourceId -> filePath among this group's own tracks -- only
    // OneLibrary's propagateMissingFieldsForPath() below needs this
    // (it identifies tracks by path, not sourceId, same reason
    // OneLibraryCueWriter's class comment gives).
    auto findTrackFilePath = [&plan](const std::string &sourceId) -> std::string {
        for (const auto &t : plan.group.tracks) {
            if (t.sourceId == sourceId) {
                return t.filePath;
            }
        }
        return {};
    };

    if (plan.mergedCuesForSurvivor.size() > plan.survivor.cues.size()) {
        fc.cueWriter->writeHotCues(plan.survivor.sourceId, plan.mergedCuesForSurvivor);
        w.session.noteItemApplied();
        log.record("cleanup: wrote merged cues onto survivor track id=" + plan.survivor.sourceId);

        // Best-effort secondary write, alongside the primary write
        // above, never fatal to this operation. See OneLibraryCueWriter's
        // own class comment and docs/onelibrary-format.md.
        if (!fc.pioneerRoot.empty() && !plan.survivor.filePath.empty()
            && infrastructure::onelibrary::OneLibraryCueWriter::existsFor(fc.pioneerRoot)) {
            try {
                sharedOneLibraryWriter(ctx, fc.pioneerRoot)
                    .writeCuesForPath(plan.survivor.filePath, plan.mergedCuesForSurvivor);
                log.record("cleanup: also wrote merged cues onto survivor into OneLibrary (id="
                           + plan.survivor.sourceId + ")");
            } catch (const std::exception &e) {
                log.record("cleanup: OneLibrary cue write failed for \"" + plan.survivor.title + "\": " + e.what());
            }
        }
    }

    // Fills in the survivor's missing bpm/key/artwork from whichever
    // other copy in the group has each (see domain::DuplicateCleanupPlan's
    // own comment on why this is a per-field "fill a gap", not a merge).
    // Primary-format writes here are NOT best-effort: a failure fails
    // this change. Only the OneLibrary *mirror* of a rekordbox write,
    // below, is best-effort, same as the merged-cues mirror.
    if (plan.bpmForSurvivor || plan.keyForSurvivor || plan.artworkPathForSurvivor) {
        if (format == "rekordbox") {
            std::string pdbPath = w.effectiveRoot + "/rekordbox/export.pdb";
            infrastructure::rekordbox::PdbRowWriter fieldWriter(pdbPath);
            uint32_t survivorId = static_cast<uint32_t>(std::stoul(plan.survivor.sourceId));
            if (plan.keyForSurvivor) {
                fieldWriter.copyTrackFieldsIfMissing(static_cast<uint32_t>(std::stoul(plan.keyDonorSourceId)),
                                                     survivorId, true, false, false);
            }
            if (plan.bpmForSurvivor) {
                fieldWriter.copyTrackFieldsIfMissing(static_cast<uint32_t>(std::stoul(plan.bpmDonorSourceId)),
                                                     survivorId, false, true, false);
            }
            if (plan.artworkPathForSurvivor) {
                fieldWriter.copyTrackFieldsIfMissing(static_cast<uint32_t>(std::stoul(plan.artworkDonorSourceId)),
                                                     survivorId, false, false, true);
            }
            if (!fieldWriter.commit()) {
                return ChangeOutcome::failure("failed to write " + QString::fromStdString(pdbPath));
            }
            w.session.noteItemApplied();
            log.record("cleanup: propagated missing bpm/key/artwork onto survivor track id=" + plan.survivor.sourceId);
        } else if (format == "engine") {
            // Artwork is deliberately not offered here -- Engine track
            // artwork isn't writable through libdjinterop today, see
            // propagateMissingFields()'s own doc comment. cueWriter is
            // always this concrete type for format == "engine".
            auto *engineCueWriter =
                static_cast<infrastructure::engine::LibdjinteropEngineCueWriter *>(fc.cueWriter.get());
            engineCueWriter->propagateMissingFields(plan.survivor.sourceId, plan.bpmForSurvivor, plan.keyForSurvivor);
            w.session.noteItemApplied();
            log.record("cleanup: propagated missing bpm/key onto survivor track id=" + plan.survivor.sourceId);
        } else if (format == "onelibrary") {
            auto &fieldWriter = sharedOneLibraryWriter(ctx, w.effectiveRoot, w.realStickRootForOneLib);
            if (plan.keyForSurvivor) {
                std::string donorPath = findTrackFilePath(plan.keyDonorSourceId);
                if (!donorPath.empty() && !plan.survivor.filePath.empty()) {
                    fieldWriter.propagateMissingFieldsForPath(donorPath, plan.survivor.filePath, false, true, false);
                }
            }
            if (plan.bpmForSurvivor) {
                std::string donorPath = findTrackFilePath(plan.bpmDonorSourceId);
                if (!donorPath.empty() && !plan.survivor.filePath.empty()) {
                    fieldWriter.propagateMissingFieldsForPath(donorPath, plan.survivor.filePath, true, false, false);
                }
            }
            if (plan.artworkPathForSurvivor) {
                std::string donorPath = findTrackFilePath(plan.artworkDonorSourceId);
                if (!donorPath.empty() && !plan.survivor.filePath.empty()) {
                    fieldWriter.propagateMissingFieldsForPath(donorPath, plan.survivor.filePath, false, false, true);
                }
            }
            w.session.noteItemApplied();
            log.record("cleanup: propagated missing bpm/key/artwork onto survivor track id=" + plan.survivor.sourceId);
        }

        // Best-effort mirror onto OneLibrary too -- only reachable when
        // this format is rekordbox (format == "onelibrary" already wrote
        // OneLibrary directly above, as the primary write).
        if (format == "rekordbox" && !fc.pioneerRoot.empty() && !plan.survivor.filePath.empty()
            && infrastructure::onelibrary::OneLibraryCueWriter::existsFor(fc.pioneerRoot)) {
            try {
                auto &oneLibFieldWriter = sharedOneLibraryWriter(ctx, fc.pioneerRoot);
                if (plan.keyForSurvivor) {
                    std::string donorPath = findTrackFilePath(plan.keyDonorSourceId);
                    if (!donorPath.empty()) {
                        oneLibFieldWriter.propagateMissingFieldsForPath(donorPath, plan.survivor.filePath, false, true,
                                                                        false);
                    }
                }
                if (plan.bpmForSurvivor) {
                    std::string donorPath = findTrackFilePath(plan.bpmDonorSourceId);
                    if (!donorPath.empty()) {
                        oneLibFieldWriter.propagateMissingFieldsForPath(donorPath, plan.survivor.filePath, true, false,
                                                                        false);
                    }
                }
                if (plan.artworkPathForSurvivor) {
                    std::string donorPath = findTrackFilePath(plan.artworkDonorSourceId);
                    if (!donorPath.empty()) {
                        oneLibFieldWriter.propagateMissingFieldsForPath(donorPath, plan.survivor.filePath, false, false,
                                                                        true);
                    }
                }
                log.record("cleanup: also propagated missing bpm/key/artwork into OneLibrary (id="
                           + plan.survivor.sourceId + ")");
            } catch (const std::exception &e) {
                log.record("cleanup: OneLibrary field propagation failed for \"" + plan.survivor.title
                           + "\": " + e.what());
            }
        }
    }

    for (const auto &doomed : plan.toRemove) {
        if (doomed.isUnreferenced) {
            // No catalog row to remove and no sourceId a writer would
            // recognise -- its sourceId is a file path. Handled after this
            // loop, by recordStrayFilesForDeletion().
            continue;
        }
        fc.cleanupWriter->removeTrackReplacingWith(doomed.sourceId, plan.survivor.sourceId);
        w.session.noteItemApplied();
        log.record("cleanup: removed duplicate track id=" + doomed.sourceId + " (\"" + doomed.title
                   + "\"), replaced by survivor id=" + plan.survivor.sourceId);

        // Best-effort OneLibrary mirror. Without this, the doomed
        // track's own OneLibrary row is left pointing at a file this
        // change is about to schedule for deletion, becoming an orphan
        // (this is exactly how real orphaned rows were found on
        // production data, see docs/onelibrary-format.md).
        if (!fc.pioneerRoot.empty() && !doomed.filePath.empty() && !plan.survivor.filePath.empty()
            && infrastructure::onelibrary::OneLibraryCueWriter::existsFor(fc.pioneerRoot)) {
            try {
                // Reassigns the doomed row's OneLibrary playlist
                // memberships onto the survivor instead of dropping
                // them -- see removeTrackByPathReplacingWith()'s comment.
                sharedOneLibraryWriter(ctx, fc.pioneerRoot)
                    .removeTrackByPathReplacingWith(doomed.filePath, plan.survivor.filePath);
                log.record("cleanup: also removed OneLibrary row for id=" + doomed.sourceId);
            } catch (const std::exception &e) {
                log.record("cleanup: OneLibrary row removal failed for \"" + doomed.title + "\": " + e.what());
            }
        }

        // On-stick state, appended per doomed copy: whatever this save
        // gets through has its manifest line, cancelled or not.
        infrastructure::cleanup::PendingDeletion pending;
        pending.format = format.toStdString();
        pending.filePath = doomed.filePath;
        pending.title = doomed.title;
        pending.artist = doomed.artist;
        pending.backupId = w.dbBackupId;
        w.manifest.append(pending);
    }

    recordStrayFilesForDeletion(w.manifest, plan, format.toStdString(), log);
    return ChangeOutcome::success();
}

}  // namespace seabass::gui
