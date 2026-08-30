#include "add_cue_controller.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <filesystem>
#include <memory>

#include "application/ports/backup_store.hpp"
#include "application/ports/cue_writer.hpp"
#include "application/use_cases/scan_library.hpp"
#include "gui/write_guard.hpp"
#include "infrastructure/backup/filesystem_backup_store.hpp"
#include "infrastructure/backup/stick_write_lock.hpp"
#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/engine/libdjinterop_engine_reader.hpp"
#include "infrastructure/logging/file_operation_log.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/onelibrary/onelibrary_reader.hpp"
#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"
#include "infrastructure/rekordbox/pdb_lookup.hpp"
#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"

namespace seabass::gui
{

namespace fs = std::filesystem;

namespace
{

// Runs entirely on a background thread (see AddCueController::addCue()).
AddCueResult runAddCueTask(QString format, QString path, QString sourceId, double positionMs, QString kind,
                            int hotCueNumber, QString color, QString comment,
                            std::shared_ptr<QtProgressReporter> reporter)
{
    AddCueResult result;
    QString refusal = refuseIfRekordboxRunning();
    if (!refusal.isEmpty()) {
        result.errorMessage = refusal;
        return result;
    }
    try {
        fs::path stickRoot = fs::path(path.toStdString()).parent_path();
        infrastructure::backup::StickWriteLock lock((stickRoot / ".seabass-backups" / ".write.lock").string());
        infrastructure::backup::FilesystemBackupStore backupStore((stickRoot / ".seabass-backups").string());
        infrastructure::logging::FileOperationLog log((stickRoot / ".seabass.log").string());

        // Never trust whatever cue list the calling page had cached --
        // re-scan fresh so the augmented list below always starts from
        // this track's real current state.
        std::vector<domain::Track> tracks;
        if (format == "rekordbox") {
            infrastructure::rekordbox::KaitaiRekordboxReader reader(path.toStdString());
            reader.setProgressReporter(*reporter);
            tracks = application::ScanLibrary(reader).execute();
        } else if (format == "engine") {
            infrastructure::engine::LibdjinteropEngineReader reader(path.toStdString());
            reader.setProgressReporter(*reporter);
            tracks = application::ScanLibrary(reader).execute();
        } else if (format == "onelibrary") {
            infrastructure::onelibrary::OneLibraryReader reader(path.toStdString());
            reader.setProgressReporter(*reporter);
            tracks = application::ScanLibrary(reader).execute();
        } else {
            result.errorMessage = "Unknown library format: " + format;
            return result;
        }

        std::string id = sourceId.toStdString();
        const domain::Track *track = nullptr;
        for (const auto &t : tracks) {
            if (t.sourceId == id) {
                track = &t;
                break;
            }
        }
        if (!track) {
            result.errorMessage = "This track no longer exists in the library -- rescan and try again.";
            return result;
        }

        domain::CuePoint newCue;
        newCue.kind = kind == "hot" ? domain::CuePoint::Kind::Hot : domain::CuePoint::Kind::Memory;
        newCue.hotCueNumber = newCue.kind == domain::CuePoint::Kind::Hot ? hotCueNumber : 0;
        newCue.positionMs = positionMs;
        newCue.color = color.toStdString();
        newCue.comment = comment.toStdString();

        std::vector<domain::CuePoint> cues = track->cues;
        if (newCue.kind == domain::CuePoint::Kind::Hot) {
            // A hardware hot-cue pad can only ever hold one cue at a time
            // -- replace whatever's already in this slot rather than
            // adding a second entry claiming the same number, which every
            // writer here already assumes can't happen.
            cues.erase(std::remove_if(cues.begin(), cues.end(),
                                       [&](const domain::CuePoint &c) {
                                           return c.kind == domain::CuePoint::Kind::Hot &&
                                                  c.hotCueNumber == newCue.hotCueNumber;
                                       }),
                       cues.end());
        }
        cues.push_back(newCue);

        std::string pioneerRoot = path.toStdString();
        std::string backupFile;
        std::string dbBackupId;

        if (format == "onelibrary") {
            // OneLibraryCueWriter is deliberately not an
            // application::CueWriter (it keys by file path, not sourceId
            // -- see its class comment), so this is a separate primary
            // write path rather than another branch of the writer
            // dispatch below.
            if (track->filePath.empty()) {
                result.errorMessage = "This track has no known file path in OneLibrary -- can't write a cue.";
                return result;
            }
            backupFile = infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(pioneerRoot);
            if (!backupFile.empty()) {
                auto record = backupStore.backup({backupFile}, "add-cue");
                log.record("add-cue: backed up before adding cue -> " + record.path);
                dbBackupId = record.id;
            }
            infrastructure::onelibrary::OneLibraryCueWriter writer(pioneerRoot);
            writer.writeCuesForPath(track->filePath, cues);
            log.record("add-cue: added " + kind.toStdString() + " cue at " +
                       std::to_string(static_cast<int>(positionMs)) + "ms to OneLibrary track id=" + id + " (\"" +
                       track->title + "\")" + (dbBackupId.empty() ? "" : ", backup " + dbBackupId));
        } else {
            std::unique_ptr<application::CueWriter> writer;
            if (format == "rekordbox") {
                writer = std::make_unique<infrastructure::rekordbox::RekordboxCueWriter>(pioneerRoot);
                auto analyzePath = infrastructure::rekordbox::findAnlzPathForTrackId(
                    pioneerRoot, static_cast<uint32_t>(std::stoul(id)));
                if (analyzePath) {
                    backupFile = infrastructure::rekordbox::extAnlzPath(pioneerRoot, *analyzePath);
                }
            } else {
                writer = std::make_unique<infrastructure::engine::LibdjinteropEngineCueWriter>(pioneerRoot);
                backupFile = (fs::path(pioneerRoot) / "Database2" / "m.db").string();
            }

            if (!backupFile.empty()) {
                auto record = backupStore.backup({backupFile}, "add-cue");
                log.record("add-cue: backed up before adding cue -> " + record.path);
                dbBackupId = record.id;
            }

            writer->writeHotCues(id, cues);
            log.record("add-cue: added " + kind.toStdString() + " cue at " +
                       std::to_string(static_cast<int>(positionMs)) + "ms to track id=" + id + " (\"" +
                       track->title + "\")" + (dbBackupId.empty() ? "" : ", backup " + dbBackupId));

            // Best-effort OneLibrary mirror -- same secondary write every
            // other rekordbox cue path here already does, never fatal to
            // the primary write above.
            if (format == "rekordbox" && !track->filePath.empty() &&
                infrastructure::onelibrary::OneLibraryCueWriter::existsFor(pioneerRoot)) {
                try {
                    infrastructure::onelibrary::OneLibraryCueWriter oneLibWriter(pioneerRoot);
                    oneLibWriter.writeCuesForPath(track->filePath, cues);
                    log.record("add-cue: also wrote into OneLibrary");
                } catch (const std::exception &e) {
                    log.record(std::string("add-cue: OneLibrary write failed: ") + e.what());
                }
            }
        }

        QString warning;
        if (format == "engine" && newCue.kind == domain::CuePoint::Kind::Memory) {
            int otherMemoryCues = static_cast<int>(
                std::count_if(track->cues.begin(), track->cues.end(),
                               [](const domain::CuePoint &c) { return c.kind == domain::CuePoint::Kind::Memory; }));
            if (otherMemoryCues > 0) {
                warning = " Engine keeps only one memory cue (the earliest by position) -- this one may not have "
                          "been saved if an existing memory cue on this track is earlier.";
            }
        }

        result.statusMessage = QString("Added %1 cue at %2ms to \"%3\".%4")
                                    .arg(kind == "hot" ? "hot" : "memory")
                                    .arg(static_cast<int>(positionMs))
                                    .arg(QString::fromStdString(track->title))
                                    .arg(warning);
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

}  // namespace

AddCueController::AddCueController(QObject *parent) : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<AddCueResult>::finished, this, &AddCueController::onTaskFinished);
}

std::shared_ptr<QtProgressReporter> AddCueController::makeReporter()
{
    auto reporter = std::make_shared<QtProgressReporter>();
    connect(reporter.get(), &QtProgressReporter::started, this,
            [this](const QString &, int total) { setScanProgress(0, total); });
    connect(reporter.get(), &QtProgressReporter::progressed, this,
            [this](int current) { setScanProgress(current, m_scanTotal); });
    return reporter;
}

void AddCueController::addCue(const QString &format, const QString &path, const QString &sourceId, double positionMs,
                               const QString &kind, int hotCueNumber, const QString &color, const QString &comment)
{
    if (m_busy) {
        return;
    }
    setErrorMessage({});
    setStatusMessage({});
    setScanProgress(0, 0);
    setBusy(true);
    setWriting(true);

    m_watcher.setFuture(QtConcurrent::run(runAddCueTask, format, path, sourceId, positionMs, kind, hotCueNumber,
                                           color, comment, makeReporter()));
}

void AddCueController::onTaskFinished()
{
    AddCueResult result = m_watcher.result();
    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
    } else {
        setStatusMessage(result.statusMessage);
    }
    setBusy(false);
    setWriting(false);
}

void AddCueController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void AddCueController::setWriting(bool writing)
{
    if (m_writing == writing) {
        return;
    }
    m_writing = writing;
    emit writingChanged();
}

void AddCueController::setScanProgress(int current, int total)
{
    if (m_scanCurrent == current && m_scanTotal == total) {
        return;
    }
    m_scanCurrent = current;
    m_scanTotal = total;
    emit scanProgressChanged();
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
