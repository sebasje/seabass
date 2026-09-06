#include "add_cue_controller.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>

#include "application/ports/cue_writer.hpp"
#include "gui/edit/edit_session_registry.hpp"
#include "gui/edit/library_edit_session.hpp"
#include "gui/edit/pending_change.hpp"
#include "gui/edit/save_context.hpp"
#include "gui/library_catalog_cache.hpp"
#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/rekordbox/pdb_lookup.hpp"
#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"

namespace seabass::gui
{

namespace fs = std::filesystem;

namespace
{

QString formatPosition(double positionMs)
{
    int totalSeconds = static_cast<int>(positionMs / 1000.0);
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

// One cue to add -- the old runAddCueTask() body, minus the lock/backup-
// store/log plumbing the save loop provides. Runs on the worker; every
// input is a copy.
class AddCueChange : public PendingChange
{
public:
    AddCueChange(QString format, QString path, QString sourceId, double positionMs, QString kind, int hotCueNumber,
                 QString color, QString comment, bool isLoop, double loopEndMs, QString trackTitle)
        : m_format(std::move(format)),
          m_path(std::move(path)),
          m_sourceId(std::move(sourceId)),
          m_positionMs(positionMs),
          m_kind(std::move(kind)),
          m_hotCueNumber(hotCueNumber),
          m_color(std::move(color)),
          m_comment(std::move(comment)),
          m_isLoop(isLoop),
          m_loopEndMs(loopEndMs),
          m_trackTitle(std::move(trackTitle))
    {
    }

    QString id() const override
    {
        // A hot slot can hold one cue: staging the same slot twice replaces
        // the earlier one. Memory cues are distinct per position.
        QString slot = m_kind == "hot" ? QStringLiteral("hot%1").arg(m_hotCueNumber)
                                       : QStringLiteral("memory@%1").arg(static_cast<int>(m_positionMs));
        return "addcue:" + m_format + ":" + m_sourceId + ":" + slot;
    }

    QString description() const override
    {
        QString what = m_isLoop ? QStringLiteral("hot loop %1").arg(m_hotCueNumber)
            : m_kind == "hot"  ? QStringLiteral("hot cue %1").arg(m_hotCueNumber)
                               : QStringLiteral("memory cue");
        QString track = m_trackTitle.isEmpty() ? "track " + m_sourceId : "\"" + m_trackTitle + "\"";
        return QStringLiteral("Add %1 at %2 to %3").arg(what, formatPosition(m_positionMs), track);
    }

    QString unit() const override { return QStringLiteral("cues"); }
    QStringList formatsTouched() const override { return {m_format}; }

    QVariantMap summary() const
    {
        return {
            {"changeId", id()},         {"sourceId", m_sourceId}, {"kind", m_kind},
            {"hotCueNumber", m_hotCueNumber}, {"positionMs", m_positionMs}, {"isLoop", m_isLoop},
            {"loopEndMs", m_loopEndMs}, {"color", m_color},       {"comment", m_comment},
        };
    }

    ChangeOutcome apply(SaveContext &ctx) override
    {
        // Never trust whatever cue list the calling page had cached --
        // re-scan fresh (via the shared cache, which itself re-reads
        // whenever the catalog's mtime has moved) so the augmented list
        // below always starts from this track's real current state.
        std::vector<domain::Track> tracks = LibraryCatalogCache::instance().tracksFor(
            m_format.toStdString(), m_path.toStdString(), ctx.progress(), ctx.cancel());

        std::string id = m_sourceId.toStdString();
        const domain::Track *track = nullptr;
        for (const auto &t : tracks) {
            if (t.sourceId == id) {
                track = &t;
                break;
            }
        }
        if (!track) {
            return ChangeOutcome::failure("This track no longer exists in the library -- rescan and try again.");
        }

        domain::CuePoint newCue;
        newCue.kind = m_kind == "hot" ? domain::CuePoint::Kind::Hot : domain::CuePoint::Kind::Memory;
        newCue.hotCueNumber = newCue.kind == domain::CuePoint::Kind::Hot ? m_hotCueNumber : 0;
        newCue.positionMs = m_positionMs;
        newCue.isLoop = m_isLoop;
        newCue.loopEndMs = m_isLoop ? m_loopEndMs : 0.0;
        newCue.color = m_color.toStdString();
        newCue.comment = m_comment.toStdString();

        std::vector<domain::CuePoint> cues = track->cues;
        if (newCue.kind == domain::CuePoint::Kind::Hot) {
            // A hardware hot-cue pad can only ever hold one cue at a time
            // -- replace whatever's already in this slot rather than
            // adding a second entry claiming the same number, which every
            // writer here already assumes can't happen.
            cues.erase(std::remove_if(cues.begin(), cues.end(),
                                      [&](const domain::CuePoint &c) {
                                          return c.kind == domain::CuePoint::Kind::Hot
                                              && c.hotCueNumber == newCue.hotCueNumber;
                                      }),
                       cues.end());
        }
        cues.push_back(newCue);

        std::string pioneerRoot = m_path.toStdString();
        std::string kind = m_kind.toStdString();
        std::string positionText = std::to_string(static_cast<int>(m_positionMs));

        if (m_format == "onelibrary") {
            // OneLibraryCueWriter is deliberately not an
            // application::CueWriter (it keys by file path, not sourceId
            // -- see its class comment), so this is a separate primary
            // write path rather than another branch of the writer
            // dispatch below.
            if (track->filePath.empty()) {
                return ChangeOutcome::failure("This track has no known file path in OneLibrary -- can't write a cue.");
            }
            ctx.backupOnce(infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(pioneerRoot), "add-cue");
            infrastructure::onelibrary::OneLibraryCueWriter writer(pioneerRoot);
            writer.writeCuesForPath(track->filePath, cues);
            ctx.log().record("add-cue: added " + kind + " cue at " + positionText + "ms to OneLibrary track id=" + id
                             + " (\"" + track->title + "\")");
            return ChangeOutcome::success();
        }

        std::unique_ptr<application::CueWriter> writer;
        if (m_format == "rekordbox") {
            writer = std::make_unique<infrastructure::rekordbox::RekordboxCueWriter>(pioneerRoot);
            auto analyzePath =
                infrastructure::rekordbox::findAnlzPathForTrackId(pioneerRoot, static_cast<uint32_t>(std::stoul(id)));
            if (analyzePath) {
                ctx.backupOnce(infrastructure::rekordbox::extAnlzPath(pioneerRoot, *analyzePath), "add-cue");
            }
        } else {
            writer = std::make_unique<infrastructure::engine::LibdjinteropEngineCueWriter>(pioneerRoot);
            ctx.backupOnce((fs::path(pioneerRoot) / "Database2" / "m.db").string(), "add-cue");
        }

        writer->writeHotCues(id, cues);
        ctx.log().record("add-cue: added " + kind + (m_isLoop ? " loop" : " cue") + " at " + positionText
                         + "ms to track id=" + id + " (\"" + track->title + "\")");

        // Best-effort OneLibrary mirror -- same secondary write every
        // other rekordbox cue path here already does, never fatal to
        // the primary write above.
        if (m_format == "rekordbox" && !track->filePath.empty()
            && infrastructure::onelibrary::OneLibraryCueWriter::existsFor(pioneerRoot)) {
            try {
                ctx.backupOnce(infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(pioneerRoot), "add-cue");
                infrastructure::onelibrary::OneLibraryCueWriter oneLibWriter(pioneerRoot);
                oneLibWriter.writeCuesForPath(track->filePath, cues);
                ctx.log().record("add-cue: also wrote into OneLibrary");
            } catch (const std::exception &e) {
                ctx.log().record(std::string("add-cue: OneLibrary write failed: ") + e.what());
            }
        }

        if (m_format == "engine" && newCue.kind == domain::CuePoint::Kind::Memory) {
            int otherMemoryCues = static_cast<int>(
                std::count_if(track->cues.begin(), track->cues.end(),
                              [](const domain::CuePoint &c) { return c.kind == domain::CuePoint::Kind::Memory; }));
            if (otherMemoryCues > 0) {
                ctx.log().record("add-cue: note: Engine keeps only one memory cue (the earliest by position); "
                                 "this one may not be kept if an existing memory cue on this track is earlier");
            }
        }
        return ChangeOutcome::success();
    }

private:
    QString m_format;
    QString m_path;
    QString m_sourceId;
    double m_positionMs;
    QString m_kind;
    int m_hotCueNumber;
    QString m_color;
    QString m_comment;
    bool m_isLoop;
    double m_loopEndMs;
    QString m_trackTitle;
};

}  // namespace

AddCueController::AddCueController(QObject *parent) : QObject(parent) {}

bool AddCueController::writing() const
{
    return m_session && m_session->writing();
}

void AddCueController::attachSession(const QString &format, const QString &path)
{
    auto *registry = EditSessionRegistry::instance();
    LibraryEditSession *session = registry->sessionFor(registry->libraryIdForPath(path));
    if (session != m_session) {
        if (m_session) {
            disconnect(m_session, nullptr, this, nullptr);
        }
        m_session = session;
        if (m_session) {
            connect(m_session, &LibraryEditSession::stateChanged, this, &AddCueController::writingChanged);
            connect(m_session, &LibraryEditSession::changeApplied, this, [this](const QString &changeId) {
                if (m_pending.erase(changeId) > 0) {
                    ++m_pendingRevision;
                    emit pendingChanged();
                }
            });
            connect(m_session, &LibraryEditSession::saveFinished, this, [this](const QVariantMap &summary) {
                if (summary.value("written").toInt() > 0) {
                    emit cuesSaved();
                }
            });
            connect(m_session, &LibraryEditSession::changesDiscarded, this, [this]() {
                if (!m_pending.empty()) {
                    m_pending.clear();
                    ++m_pendingRevision;
                    emit pendingChanged();
                }
            });
        }
    }
    if (m_session) {
        if (format == "engine") {
            m_session->setLibraryPaths(QString(), path);
        } else {
            m_session->setLibraryPaths(path, QString());
        }
    }
}

void AddCueController::addCue(const QString &format, const QString &path, const QString &sourceId, double positionMs,
                               const QString &kind, int hotCueNumber, const QString &color, const QString &comment,
                               bool isLoop, double loopEndMs, const QString &trackTitle)
{
    setErrorMessage({});
    setStatusMessage({});
    if (format != "rekordbox" && format != "engine" && format != "onelibrary") {
        setErrorMessage("Unknown library format: " + format);
        return;
    }
    if (isLoop && format != "engine") {
        // See the class comment: rekordbox's ANLZ loop encoding is
        // unverified against real hardware, and OneLibrary's cue table
        // has never been confirmed to round-trip loops either --
        // refusing here means a DJ never gets a cue silently written as
        // a point when they asked to save a loop-out.
        setErrorMessage("Hot loops can only be added to Engine tracks right now.");
        return;
    }
    if (isLoop && kind != "hot") {
        // Engine's loops() array is indexed exactly like hot_cues() --
        // there's no Engine concept of an un-slotted "memory loop" to
        // write this into.
        setErrorMessage("Loops need a hot cue slot.");
        return;
    }

    attachSession(format, path);
    if (!m_session) {
        setErrorMessage("This stick's library could not be identified; nothing was changed.");
        return;
    }
    if (m_session->writing()) {
        setErrorMessage("A save is running -- add the cue once it has finished.");
        return;
    }

    auto change = std::make_unique<AddCueChange>(format, path, sourceId, positionMs, kind, hotCueNumber, color, comment,
                                                 isLoop, loopEndMs, trackTitle);
    QString changeId = change->id();
    QString description = change->description();
    QVariantMap summary = change->summary();
    if (!m_session->stage(std::move(change))) {
        return;  // the session reported the lock refusal; the page shows it
    }
    m_pending[changeId] = summary;
    ++m_pendingRevision;
    emit pendingChanged();
    setStatusMessage("Staged: " + description + ". Press Save to write it to the stick.");
}

QVariantList AddCueController::pendingCuesFor(const QString &sourceId) const
{
    QVariantList list;
    for (const auto &[id, cue] : m_pending) {
        if (cue.value("sourceId").toString() == sourceId) {
            list << cue;
        }
    }
    return list;
}

void AddCueController::unstage(const QString &changeId)
{
    if (m_pending.erase(changeId) == 0) {
        return;
    }
    if (m_session) {
        m_session->unstage(changeId);
    }
    ++m_pendingRevision;
    emit pendingChanged();
    setStatusMessage({});
}

void AddCueController::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

void AddCueController::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) {
        return;
    }
    m_statusMessage = message;
    emit statusMessageChanged();
}

}  // namespace seabass::gui
