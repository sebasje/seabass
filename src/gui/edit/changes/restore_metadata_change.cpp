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
#include "infrastructure/rekordbox/pdb_row_writer.hpp"
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
    RestoreWriterContext(const QString &format, const QString &path, SaveContext &ctx)
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
            // Empty: each change registers its own track through
            // notePath() as it applies, so one adapter serves the save.
            writer = std::make_unique<OneLibraryCueWriterAdapter>(
                root, std::unordered_map<std::string, std::string>{});
        }
    }

    std::unique_ptr<application::CueWriter> writer;
    std::unique_ptr<infrastructure::onelibrary::OneLibraryCueWriter> mirror;
    // rekordbox only, and only when a rating is actually being written:
    // this one rewrites export.pdb, which no cue write touches at all.
    std::unique_ptr<infrastructure::rekordbox::PdbRowWriter> pdbRows;
    std::string pdbPath;
    bool pdbDirty = false;
};

// Writes the rating and the comment, per format, and says in the log
// what each format could not take rather than leaving a gap.
//
// The per-format table is measured, not assumed -- see
// docs/metadata-backup-plan.md and tests/pdb_rating_write_test.cpp:
// Engine and OneLibrary take both fields; export.pdb takes the rating
// (one byte, already there) and cannot take a comment it does not
// already have room for.
void applyAnnotation(SaveContext &ctx, RestoreWriterContext &writer, const QString &format, const QString &path,
                      const std::string &sourceId, const domain::MetadataRestoreProposal &proposal)
{
    const std::optional<int> stars = proposal.ratingOffered ? proposal.rating : std::nullopt;
    const std::optional<std::string> comment =
        proposal.commentOffered ? std::optional<std::string>(proposal.comment) : std::nullopt;
    if (!stars && !comment) {
        return;
    }

    if (format == "engine") {
        auto *engine = dynamic_cast<infrastructure::engine::LibdjinteropEngineCueWriter *>(writer.writer.get());
        if (!engine) {
            ctx.log().record(std::string(LogTag) + ": no Engine writer for \"" + proposal.stickTrack.title +
                             "\"; its rating and comment were not written");
            return;
        }
        // writeAnnotation throws when the track has gone from the
        // database since the scan. The cues for this track are already
        // written by now, so failing the whole save over a rating would
        // undo more than it saves: log it and let the rest through, the
        // way the OneLibrary paths below do.
        try {
            engine->writeAnnotation(sourceId, stars, comment);
        } catch (const std::exception &e) {
            ctx.log().record(std::string(LogTag) + ": Engine annotation write failed for \"" +
                             proposal.stickTrack.title + "\": " + e.what());
        }
        return;
    }

    if (format == "rekordbox") {
        // The rating is a one-byte field in export.pdb, which no cue
        // write touches -- so this needs its own writer, opened once per
        // save and committed by an onFinish hook after the loop.
        if (stars) {
            if (!writer.pdbRows) {
                writer.pdbPath = (fs::path(path.toStdString()) / "rekordbox" / "export.pdb").string();
                ctx.backupOnce(writer.pdbPath, LogTag);
                writer.pdbRows = std::make_unique<infrastructure::rekordbox::PdbRowWriter>(writer.pdbPath);
                RestoreWriterContext *held = &writer;
                SaveContext *savedCtx = &ctx;
                ctx.onFinish([held, savedCtx](bool ok) {
                    (void)ok;
                    if (!held->pdbDirty || !held->pdbRows) {
                        return;
                    }
                    // Committed even after a cancel or a failure, and
                    // for the same reason FormatWriteSession commits its
                    // scratch copy then: the ratings already set are
                    // counted in the save's applied changes and the page
                    // takes them off its list, so leaving them in memory
                    // would report a write that never reached the stick.
                    // Nothing is committed that was not applied --
                    // pdbDirty is only set by a rating that took.
                    // commit() returns false rather than throwing when
                    // the file went stale under us, when the edited
                    // buffer does not reparse, or when the atomic write
                    // fails. Swallowing that would tell the DJ N ratings
                    // went back while export.pdb still holds the old
                    // ones; throwing here is how a finish hook reports a
                    // failed commit (see FormatWriteSession::commit).
                    if (!held->pdbRows->commit()) {
                        savedCtx->log().record(std::string(LogTag) +
                                               ": FAILED to commit the restored rating(s) into export.pdb");
                        throw std::runtime_error(
                            "metadata-restore: the restored rating(s) could not be written into export.pdb");
                    }
                });
            }
            // Throws for a rating outside 0..5, returns false when no
            // track row carries that id any more. Neither should cost
            // the save the cues it has already written.
            try {
                if (writer.pdbRows->setTrackRating(static_cast<uint32_t>(std::stoul(sourceId)), *stars)) {
                    writer.pdbDirty = true;
                } else {
                    ctx.log().record(std::string(LogTag) + ": no DeviceLibrary row with id=" + sourceId +
                                     " (\"" + proposal.stickTrack.title + "\"); its rating was not written");
                }
            } catch (const std::exception &e) {
                ctx.log().record(std::string(LogTag) + ": DeviceLibrary rating write failed for \"" +
                                 proposal.stickTrack.title + "\": " + e.what());
            }
        }
        // The mirror is the other half of the same library, and it is
        // the half that can take a comment of any length.
        bool mirrorTookIt = false;
        if (writer.mirror && !proposal.stickTrack.filePath.empty()) {
            try {
                writer.mirror->writeAnnotationForPath(proposal.stickTrack.filePath, stars, comment);
                mirrorTookIt = true;
            } catch (const std::exception &e) {
                ctx.log().record(std::string(LogTag) + ": OneLibrary annotation write failed: " + e.what());
            }
        }
        if (comment && !mirrorTookIt) {
            // Measured: export.pdb keeps a comment in a byte span fixed
            // at export time, and 1160 of the fixture's 1161 tracks have
            // a span of zero. Writing a truncated prefix of the DJ's own
            // sentence would be worse than not writing it, so this says
            // so instead. The page says the same thing before the save.
            //
            // Only when the mirror did not take it: on a stick carrying
            // both catalogs the comment did go back, and saying it could
            // not would contradict both the page and the truth.
            ctx.log().record(std::string(LogTag) + ": DeviceLibrary cannot store a comment for \"" + proposal.stickTrack.title +
                             "\" -- its row has no room to grow one");
        }
        return;
    }

    // format == "onelibrary": the adapter is this format's cue writer,
    // and it holds the one OneLibrary connection this save opens. Going
    // through it rather than constructing a second writer is what keeps
    // the whole save at one key derivation instead of one per track.
    auto *adapter = dynamic_cast<OneLibraryCueWriterAdapter *>(writer.writer.get());
    if (!adapter) {
        ctx.log().record(std::string(LogTag) + ": no OneLibrary writer for \"" + proposal.stickTrack.title +
                         "\"; its rating and comment were not written");
        return;
    }
    try {
        adapter->writer().writeAnnotationForPath(proposal.stickTrack.filePath, stars, comment);
    } catch (const std::exception &e) {
        ctx.log().record(std::string(LogTag) + ": OneLibrary annotation write failed: " + e.what());
    }
}

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
    // A proposal can be staged for its rating or its comment alone --
    // the cues may conflict and be skipped, or there may be none stored.
    // Saying "Put 0 stored cue(s) back" would describe a write this
    // change no longer makes.
    if (!m_proposal.cuesOffered) {
        QStringList fields;
        if (m_proposal.ratingOffered) {
            fields << QStringLiteral("rating");
        }
        if (m_proposal.commentOffered) {
            fields << QStringLiteral("comment");
        }
        return QStringLiteral("Put the stored %1 back on \"%2\"").arg(fields.join(QStringLiteral(" and ")), title);
    }
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
//
// Both flags follow what this particular proposal will actually write,
// because WriteScope is declared in both directions: a proposal staged
// for its rating alone writes no analysis file, and one that restores a
// rating does rewrite export.pdb -- which the up-front backup has to
// cover rather than leaving to applyAnnotation's backupOnce mid-loop.
std::vector<BackupTarget> RestoreMetadataChange::filesToBackup(SaveContext &ctx) const
{
    std::vector<BackupTarget> targets;
    const domain::TrackId track{m_format.toStdString(), m_sourceId.toStdString()};
    const WriteScope scope{.cueData = m_proposal.cuesOffered,
                           .catalogRows = m_proposal.ratingOffered,
                           .oneLibraryMirror = true};
    for (const auto &file : filesWrittenFor(scope, track, m_path, ctx)) {
        targets.push_back({file, LogTag});
    }
    return targets;
}

ChangeOutcome RestoreMetadataChange::apply(SaveContext &ctx)
{
    const domain::Track &track = m_proposal.stickTrack;
    const std::string sourceId = m_sourceId.toStdString();

    // One writer per format for the whole save, not one per track. The
    // key deliberately does not carry the sourceId: the OneLibrary
    // adapter learns this track's path through notePath() below, so
    // every track in the save shares one adapter and therefore one
    // SQLCipher connection and one key derivation. Keying it per track
    // cost two opens per track, ~115 ms of PBKDF2 each -- see
    // tests/restore_metadata_change_test.cpp case 6, which counts them.
    const std::string key = "metadata-restore:" + m_format.toStdString();
    RestoreWriterContext &writer = ctx.shared<RestoreWriterContext>(
        key, [&]() { return std::make_unique<RestoreWriterContext>(m_format, m_path, ctx); });
    if (auto *adapter = dynamic_cast<OneLibraryCueWriterAdapter *>(writer.writer.get())) {
        adapter->notePath(sourceId, track.filePath);
    }

    if (m_format == "rekordbox" && m_proposal.cuesOffered) {
        // rekordbox keeps cues per track, in ANLZ files, so the file to
        // back up is this track's own. Nothing to back up when no cue
        // write is coming: the rating goes into export.pdb, which
        // applyAnnotation backs up itself.
        const std::string root = m_path.toStdString();
        const auto *pathIndex = sharedAnlzPathIndex(ctx, m_path);
        const auto trackId = static_cast<uint32_t>(std::stoul(sourceId));
        auto analyzePath = pathIndex ? pathIndex->pathFor(trackId)
                                     : infrastructure::rekordbox::findAnlzPathForTrackId(root, trackId);
        if (analyzePath) {
            ctx.backupOnce(infrastructure::rekordbox::extAnlzPath(root, *analyzePath), LogTag);
        }
    }

    // Only when the plan actually offers cues. Every writer treats the
    // vector it is handed as the complete set for the track, so writing
    // an empty one does not mean "leave the cues alone" -- it means
    // "delete them". A proposal staged for its rating alone carries no
    // cues, and under the skip-all default a track whose cues conflict
    // carries none either: exactly the DJ's live work that the default
    // exists to protect.
    if (m_proposal.cuesOffered) {
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
    }

    applyAnnotation(ctx, writer, m_format, m_path, sourceId, m_proposal);
    return ChangeOutcome::success();
}

}  // namespace seabass::gui
