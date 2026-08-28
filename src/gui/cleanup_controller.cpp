#include "cleanup_controller.hpp"

#include <QStringList>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>

#include "application/ports/backup_store.hpp"
#include "application/ports/cue_writer.hpp"
#include "application/ports/library_cleanup_writer.hpp"
#include "application/ports/operation_log.hpp"
#include "application/use_cases/scan_library.hpp"
#include "domain/duplicate_cue_consolidation.hpp"
#include "gui/qt_progress_reporter.hpp"
#include "gui/write_guard.hpp"
#include "infrastructure/backup/filesystem_backup_store.hpp"
#include "infrastructure/backup/stick_write_lock.hpp"
#include "infrastructure/cleanup/pending_deletion_manifest.hpp"
#include "infrastructure/cleanup/pending_deletion_resolver.hpp"
#include "infrastructure/engine/libdjinterop_engine_cleanup_writer.hpp"
#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/engine/libdjinterop_engine_reader.hpp"
#include "infrastructure/logging/file_operation_log.hpp"
#if defined(_WIN32)
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#endif
#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"
#include "infrastructure/rekordbox/pdb_lookup.hpp"
#include "infrastructure/rekordbox/rekordbox_cleanup_writer.hpp"
#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"

namespace djconvert::gui
{

namespace fs = std::filesystem;

namespace
{

// Mirrors cli/main.cpp's humanSize() exactly -- see duplicates_controller
// .cpp's identical copy for why this is duplicated per composition root
// rather than shared.
QString humanSize(std::uint64_t bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units)) {
        value /= 1024.0;
        unit++;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f %s", value, units[unit]);
    return QString::fromUtf8(buf);
}

std::uint64_t wastedBytes(const domain::DuplicateCleanupPlan &plan)
{
    std::uint64_t total = 0;
    for (const auto &t : plan.toRemove) {
        total += t.fileSizeBytes;
    }
    return total;
}

QVariantMap trackSummary(const domain::Track &t)
{
    QVariantMap m;
    m["sourceId"] = QString::fromStdString(t.sourceId);
    m["title"] = QString::fromStdString(t.title);
    m["artist"] = QString::fromStdString(t.artist);
    m["filePath"] = QString::fromStdString(t.filePath);
    m["bitrate"] = t.bitrate;
    m["durationMs"] = t.durationSeconds * 1000.0;
    m["sizeBytes"] = static_cast<qulonglong>(t.fileSizeBytes);
    m["sizeHuman"] = humanSize(t.fileSizeBytes);
    return m;
}

}  // namespace

CleanupPlanListModel::CleanupPlanListModel(QObject *parent) : QAbstractListModel(parent) {}

int CleanupPlanListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_plans.size());
}

QVariant CleanupPlanListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= m_plans.size()) {
        return {};
    }
    const auto &plan = m_plans[static_cast<size_t>(index.row())];
    switch (role) {
    case SurvivorRole:
        return trackSummary(plan.survivor);
    case ToRemoveRole: {
        QVariantList result;
        for (const auto &t : plan.toRemove) {
            result << trackSummary(t);
        }
        return result;
    }
    case DiffersRole:
        return plan.differs;
    case WastedBytesHumanRole:
        return humanSize(wastedBytes(plan));
    case NewCueCountRole:
        return static_cast<int>(plan.mergedCuesForSurvivor.size()) - static_cast<int>(plan.survivor.cues.size());
    case IncludedRole:
        return m_included[static_cast<size_t>(index.row())];
    default:
        return {};
    }
}

bool CleanupPlanListModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= m_plans.size()) {
        return false;
    }
    if (role != IncludedRole) {
        return false;
    }
    m_included[static_cast<size_t>(index.row())] = value.toBool();
    emit dataChanged(index, index, {IncludedRole});
    return true;
}

QHash<int, QByteArray> CleanupPlanListModel::roleNames() const
{
    return {
        {SurvivorRole, "survivor"},
        {ToRemoveRole, "toRemove"},
        {DiffersRole, "differs"},
        {WastedBytesHumanRole, "wastedBytesHuman"},
        {NewCueCountRole, "newCueCount"},
        {IncludedRole, "included"},
    };
}

void CleanupPlanListModel::setPlans(std::vector<domain::DuplicateCleanupPlan> plans)
{
    beginResetModel();
    m_plans = std::move(plans);
    m_included.assign(m_plans.size(), true);
    for (size_t i = 0; i < m_plans.size(); ++i) {
        // Groups where quality and length disagree default to excluded
        // -- see DuplicateCleanupPlan::differs' own doc comment.
        m_included[i] = !m_plans[i].differs;
    }
    endResetModel();
}

bool CleanupPlanListModel::included(size_t index) const
{
    return index < m_included.size() && m_included[index];
}

void CleanupPlanListModel::setAllIncluded(bool included)
{
    if (m_included.empty()) {
        return;
    }
    m_included.assign(m_included.size(), included);
    emit dataChanged(index(0), index(static_cast<int>(m_included.size()) - 1), {IncludedRole});
}

int CleanupPlanListModel::includedCount() const
{
    return static_cast<int>(std::count(m_included.begin(), m_included.end(), true));
}

PendingDeletionListModel::PendingDeletionListModel(QObject *parent) : QAbstractListModel(parent) {}

int PendingDeletionListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_entries.size());
}

QVariant PendingDeletionListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= m_entries.size()) {
        return {};
    }
    const auto &entry = m_entries[static_cast<size_t>(index.row())];
    switch (role) {
    case FormatRole:
        return QString::fromStdString(entry.format);
    case TitleRole:
        return QString::fromStdString(entry.title);
    case ArtistRole:
        return QString::fromStdString(entry.artist);
    case FilePathRole:
        return QString::fromStdString(entry.filePath);
    case BackupIdRole:
        return QString::fromStdString(entry.backupId);
    case TimestampRole:
        return QString::fromStdString(entry.timestampUtc);
    case IncludedRole:
        return m_included[static_cast<size_t>(index.row())];
    default:
        return {};
    }
}

bool PendingDeletionListModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= m_entries.size()) {
        return false;
    }
    if (role != IncludedRole) {
        return false;
    }
    m_included[static_cast<size_t>(index.row())] = value.toBool();
    emit dataChanged(index, index, {IncludedRole});
    return true;
}

QHash<int, QByteArray> PendingDeletionListModel::roleNames() const
{
    return {
        {FormatRole, "format"},
        {TitleRole, "title"},
        {ArtistRole, "artist"},
        {FilePathRole, "filePath"},
        {BackupIdRole, "backupId"},
        {TimestampRole, "timestampUtc"},
        {IncludedRole, "included"},
    };
}

void PendingDeletionListModel::setEntries(std::vector<infrastructure::cleanup::PendingDeletion> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    m_included.assign(m_entries.size(), true);
    endResetModel();
}

std::vector<infrastructure::cleanup::PendingDeletion> PendingDeletionListModel::includedEntries() const
{
    std::vector<infrastructure::cleanup::PendingDeletion> result;
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (m_included[i]) {
            result.push_back(m_entries[i]);
        }
    }
    return result;
}

int PendingDeletionListModel::includedCount() const
{
    return static_cast<int>(std::count(m_included.begin(), m_included.end(), true));
}

void PendingDeletionListModel::setAllIncluded(bool included)
{
    if (m_included.empty()) {
        return;
    }
    m_included.assign(m_included.size(), included);
    emit dataChanged(index(0), index(static_cast<int>(m_included.size()) - 1), {IncludedRole});
}

namespace
{

// Mirrors duplicates_controller.cpp's FormatContext, extended with a
// LibraryCleanupWriter and the extra (not tied to any one track) files
// that need backing up once per session -- rekordbox's export.pdb,
// touched regardless of which track triggered the write, unlike
// Engine's m.db which filesToBackUpFor already covers per-track (same
// file every time, deduped the same way as everywhere else).
struct FormatContext
{
    std::unique_ptr<application::CueWriter> cueWriter;
    std::unique_ptr<application::LibraryCleanupWriter> cleanupWriter;
    std::unique_ptr<application::BackupStore> backupStore;
    std::unique_ptr<application::OperationLog> log;
    std::function<std::vector<std::string>(const std::string &)> filesToBackUpFor;
    std::vector<std::string> extraFilesToBackUp;
    std::string pendingDeletionManifestPath;
    // Non-empty only for rekordbox -- used to best-effort also write
    // cues into OneLibrary/exportLibrary.db (see runApplyTask()) if it
    // exists alongside export.pdb on this stick.
    std::string pioneerRoot;
};

FormatContext makeContext(const QString &format, const QString &path)
{
    fs::path stickRoot = fs::path(path.toStdString()).parent_path();

    FormatContext ctx;
    ctx.backupStore = std::make_unique<infrastructure::backup::FilesystemBackupStore>(
        (stickRoot / ".djconvert-backups").string());
    ctx.log = std::make_unique<infrastructure::logging::FileOperationLog>((stickRoot / ".djconvert.log").string());
    ctx.pendingDeletionManifestPath = (stickRoot / ".djconvert-pending-deletions.jsonl").string();

    if (format == "rekordbox") {
        std::string pioneerRoot = path.toStdString();
        ctx.cueWriter = std::make_unique<infrastructure::rekordbox::RekordboxCueWriter>(pioneerRoot);
        ctx.cleanupWriter = std::make_unique<infrastructure::rekordbox::RekordboxCleanupWriter>(pioneerRoot);
        ctx.filesToBackUpFor = [pioneerRoot](const std::string &trackSourceId) -> std::vector<std::string> {
            auto analyzePath = infrastructure::rekordbox::findAnlzPathForTrackId(
                pioneerRoot, static_cast<uint32_t>(std::stoul(trackSourceId)));
            if (!analyzePath) {
                return {};
            }
            return {infrastructure::rekordbox::extAnlzPath(pioneerRoot, *analyzePath)};
        };
        ctx.extraFilesToBackUp = {pioneerRoot + "/rekordbox/export.pdb"};
        ctx.pioneerRoot = pioneerRoot;
#if defined(_WIN32)
        if (infrastructure::onelibrary::OneLibraryCueWriter::existsFor(pioneerRoot)) {
            ctx.extraFilesToBackUp.push_back(infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(pioneerRoot));
        }
#endif
    } else {
        std::string engineLibraryPath = path.toStdString();
        ctx.cueWriter = std::make_unique<infrastructure::engine::LibdjinteropEngineCueWriter>(engineLibraryPath);
        ctx.cleanupWriter = std::make_unique<infrastructure::engine::LibdjinteropEngineCleanupWriter>(engineLibraryPath);
        std::string engineDbFile = (fs::path(engineLibraryPath) / "Database2" / "m.db").string();
        ctx.filesToBackUpFor = [engineDbFile](const std::string &) -> std::vector<std::string> { return {engineDbFile}; };
    }
    return ctx;
}

// Runs entirely on a background thread (see CleanupController::apply()).
CleanupWriteResult runApplyTask(QString format, QString path, std::vector<domain::DuplicateCleanupPlan> includedPlans,
                                 std::shared_ptr<QtProgressReporter> reporter)
{
    CleanupWriteResult result;
    QString refusal = refuseIfRekordboxRunning();
    if (!refusal.isEmpty()) {
        result.errorMessage = refusal;
        return result;
    }
    try {
        std::string backupDir = (fs::path(path.toStdString()).parent_path() / ".djconvert-backups").string();
        infrastructure::backup::StickWriteLock lock(backupDir + "/.write.lock");

        auto ctx = makeContext(format, path);
        infrastructure::cleanup::PendingDeletionManifest manifest(ctx.pendingDeletionManifestPath);

        std::set<std::string> backedUpFiles;
        std::string dbBackupId;
        auto backupIfNeeded = [&](const std::string &f) {
            if (f.empty() || backedUpFiles.contains(f)) {
                return;
            }
            auto record = ctx.backupStore->backup({f}, "duplicate-file-cleanup");
            ctx.log->record("backed up before duplicate file cleanup -> " + record.path);
            result.backups.push_back({QString::fromStdString(fs::path(record.path).parent_path().string()),
                                       QString::fromStdString(record.id)});
            backedUpFiles.insert(f);
            if (dbBackupId.empty()) {
                dbBackupId = record.id;
            }
        };

        for (const auto &f : ctx.extraFilesToBackUp) {
            backupIfNeeded(f);
        }

        size_t totalWork = 0;
        for (const auto &plan : includedPlans) {
            totalWork += plan.toRemove.size();
        }
        reporter->start("Cleaning up duplicates", totalWork);
        size_t done = 0;

        int groupsProcessed = 0;
        int filesRemoved = 0;
        int cuesPreserved = 0;
        QStringList oneLibraryWarnings;

        for (const auto &plan : includedPlans) {
            for (const auto &f : ctx.filesToBackUpFor(plan.survivor.sourceId)) {
                backupIfNeeded(f);
            }

            if (plan.mergedCuesForSurvivor.size() > plan.survivor.cues.size()) {
                ctx.cueWriter->writeHotCues(plan.survivor.sourceId, plan.mergedCuesForSurvivor);
                cuesPreserved += static_cast<int>(plan.mergedCuesForSurvivor.size() - plan.survivor.cues.size());
                ctx.log->record("cleanup: wrote merged cues onto survivor track id=" + plan.survivor.sourceId);

#if defined(_WIN32)
                // Best-effort secondary write, alongside the primary
                // export.pdb write above -- never fatal to this
                // operation, and never rolls back the export.pdb write
                // that already succeeded. See OneLibraryCueWriter's own
                // class comment and docs/onelibrary-format.md.
                if (!ctx.pioneerRoot.empty() && !plan.survivor.filePath.empty() &&
                    infrastructure::onelibrary::OneLibraryCueWriter::existsFor(ctx.pioneerRoot)) {
                    try {
                        infrastructure::onelibrary::OneLibraryCueWriter oneLibWriter(ctx.pioneerRoot);
                        oneLibWriter.writeCuesForPath(plan.survivor.filePath, plan.mergedCuesForSurvivor);
                        ctx.log->record("cleanup: also wrote merged cues onto survivor into OneLibrary (id=" +
                                         plan.survivor.sourceId + ")");
                    } catch (const std::exception &e) {
                        QString warning = QString("OneLibrary cue write failed for \"%1\": %2")
                                               .arg(QString::fromStdString(plan.survivor.title))
                                               .arg(QString::fromStdString(e.what()));
                        oneLibraryWarnings << warning;
                        ctx.log->record("cleanup: " + warning.toStdString());
                    }
                }
#endif
            }

            for (const auto &doomed : plan.toRemove) {
                ctx.cleanupWriter->removeTrackReplacingWith(doomed.sourceId, plan.survivor.sourceId);
                ctx.log->record("cleanup: removed duplicate track id=" + doomed.sourceId + " (\"" + doomed.title +
                                 "\"), replaced by survivor id=" + plan.survivor.sourceId);

                infrastructure::cleanup::PendingDeletion pending;
                pending.format = format.toStdString();
                pending.filePath = doomed.filePath;
                pending.title = doomed.title;
                pending.artist = doomed.artist;
                pending.backupId = dbBackupId;
                manifest.append(pending);

                filesRemoved++;
                done++;
                reporter->tick(done);
            }
            groupsProcessed++;
        }
        reporter->finish();

        result.statusMessage = QString("Cleaned up %1 duplicate group(s): removed %2 duplicate track(s) from the "
                                        "library, preserved %3 cue(s) onto the surviving copy. The %2 removed "
                                        "audio file(s) are listed for deletion but haven't been deleted yet.")
                                    .arg(groupsProcessed)
                                    .arg(filesRemoved)
                                    .arg(cuesPreserved);
        if (!oneLibraryWarnings.isEmpty()) {
            result.statusMessage += QString(" (%1 OneLibrary cue write(s) failed -- the primary library write "
                                             "above still succeeded and was not affected: %2)")
                                         .arg(oneLibraryWarnings.size())
                                         .arg(oneLibraryWarnings.join("; "));
        }
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

CleanupWriteResult runUndoTask(std::vector<UndoableBackup> backups, std::shared_ptr<QtProgressReporter> reporter)
{
    CleanupWriteResult result;
    QString refusal = refuseIfRekordboxRunning();
    if (!refusal.isEmpty()) {
        result.errorMessage = refusal;
        return result;
    }
    int restored = 0;
    try {
        std::vector<std::string> dirs;
        for (const auto &backup : backups) {
            dirs.push_back(backup.backupDir.toStdString());
        }
        std::sort(dirs.begin(), dirs.end());
        dirs.erase(std::unique(dirs.begin(), dirs.end()), dirs.end());
        std::vector<std::unique_ptr<infrastructure::backup::StickWriteLock>> locks;
        for (const auto &dir : dirs) {
            locks.push_back(std::make_unique<infrastructure::backup::StickWriteLock>(dir + "/.write.lock"));
        }

        reporter->start("Undoing", backups.size());
        for (auto it = backups.rbegin(); it != backups.rend(); ++it) {
            infrastructure::backup::FilesystemBackupStore store(it->backupDir.toStdString());
            if (store.restore(it->id.toStdString())) {
                restored++;
            }
            reporter->tick(static_cast<size_t>(restored));
        }
        reporter->finish();
        result.statusMessage = QString("Undone -- restored %1 file(s) to their state before the last cleanup. "
                                        "Removed library entries and the pending-deletions list are not reverted "
                                        "by this -- they reflect what was written to the backups, not what's on "
                                        "disk after restoring.")
                                    .arg(restored);
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

// Runs entirely on a background thread (see CleanupController::
// deleteSelectedPendingFiles()). Re-scans the library fresh -- never
// trusts the manifest alone, see resolvePendingDeletions()'s own doc
// comment -- deletes every `selected` entry resolvePendingDeletions()
// confirms is genuinely orphaned, and clears exactly those from the
// pending-deletions manifest. Entries still referenced are left
// untouched on disk and in the manifest either way.
PendingDeletionApplyResult runDeletePendingTask(QString format, QString path,
                                                 std::vector<infrastructure::cleanup::PendingDeletion> selected,
                                                 std::shared_ptr<QtProgressReporter> reporter)
{
    PendingDeletionApplyResult result;
    QString refusal = refuseIfRekordboxRunning();
    if (!refusal.isEmpty()) {
        result.errorMessage = refusal;
        return result;
    }
    try {
        fs::path stickRoot = fs::path(path.toStdString()).parent_path();
        infrastructure::backup::StickWriteLock lock((stickRoot / ".djconvert-backups" / ".write.lock").string());
        infrastructure::cleanup::PendingDeletionManifest manifest(
            (stickRoot / ".djconvert-pending-deletions.jsonl").string());
        infrastructure::logging::FileOperationLog log((stickRoot / ".djconvert.log").string());

        std::vector<domain::Track> tracks;
        if (format == "rekordbox") {
            infrastructure::rekordbox::KaitaiRekordboxReader reader(path.toStdString());
            reader.setProgressReporter(*reporter);
            tracks = application::ScanLibrary(reader).execute();
        } else {
            infrastructure::engine::LibdjinteropEngineReader reader(path.toStdString());
            reader.setProgressReporter(*reporter);
            tracks = application::ScanLibrary(reader).execute();
        }

        auto resolution = infrastructure::cleanup::resolvePendingDeletions(selected, tracks);

        std::set<std::string> processed;
        int deleted = 0;
        int failed = 0;
        for (const auto &entry : resolution.safeToDelete) {
            std::error_code ec;
            bool existed = fs::exists(entry.filePath, ec);
            if (!existed) {
                // Already gone from disk (e.g. removed by hand since) --
                // still clear it from the manifest, nothing left to do.
                processed.insert(entry.filePath);
                deleted++;
                log.record("cleanup: pending deletion already absent from disk, clearing from manifest -> " +
                            entry.filePath);
                continue;
            }
            if (fs::remove(entry.filePath, ec)) {
                processed.insert(entry.filePath);
                deleted++;
                log.record("cleanup: deleted orphaned duplicate file (backup " + entry.backupId + ") -> " +
                            entry.filePath);
            } else {
                failed++;
                log.record("cleanup: failed to delete orphaned duplicate file -> " + entry.filePath + " (" +
                            ec.message() + ")");
            }
        }
        manifest.removeProcessed(processed);

        QStringList parts;
        parts << QString("deleted %1 file(s) from disk").arg(deleted);
        if (!resolution.stillReferenced.empty()) {
            parts << QString("%1 file(s) still referenced by a current track -- left alone, not deleted")
                         .arg(static_cast<int>(resolution.stillReferenced.size()));
        }
        if (failed > 0) {
            parts << QString("%1 failed to delete").arg(failed);
        }
        result.statusMessage = parts.join("; ");
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

CleanupTaskResult runRescanTask(QString format, QString path, std::shared_ptr<QtProgressReporter> reporter)
{
    CleanupTaskResult result;
    try {
        std::vector<domain::Track> tracks;
        if (format == "rekordbox") {
            infrastructure::rekordbox::KaitaiRekordboxReader reader(path.toStdString());
            reader.setProgressReporter(*reporter);
            tracks = application::ScanLibrary(reader).execute();
        } else {
            infrastructure::engine::LibdjinteropEngineReader reader(path.toStdString());
            reader.setProgressReporter(*reporter);
            tracks = application::ScanLibrary(reader).execute();
        }

        std::vector<domain::DuplicateCleanupPlan> plans;
        for (const auto &group : domain::DuplicateTrackFinder::find(tracks)) {
            auto plan = domain::DuplicateCleanupPlanner::plan(group);
            if (!plan.toRemove.empty()) {
                plans.push_back(std::move(plan));
            }
        }
        result.plans = std::move(plans);
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

}  // namespace

CleanupController::CleanupController(QObject *parent) : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<CleanupTaskResult>::finished, this, &CleanupController::onRescanFinished);
    connect(&m_writeWatcher, &QFutureWatcher<CleanupWriteResult>::finished, this, &CleanupController::onWriteFinished);
    connect(&m_pendingWriteWatcher, &QFutureWatcher<PendingDeletionApplyResult>::finished, this,
            &CleanupController::onDeletePendingFinished);
}

QString CleanupController::totalWastedBytesHuman() const
{
    std::uint64_t total = 0;
    for (const auto &plan : m_model.plans()) {
        total += wastedBytes(plan);
    }
    return humanSize(total);
}

void CleanupController::scan(const QString &format, const QString &path)
{
    m_format = format;
    m_path = path;
    rescan();
}

void CleanupController::rescan()
{
    if (m_busy) {
        return;
    }
    setErrorMessage({});
    setScanProgress(0, 0);
    setBusy(true);

    m_watcher.setFuture(QtConcurrent::run(runRescanTask, m_format, m_path, makeReporter()));
}

void CleanupController::setIncluded(int index, bool included)
{
    m_model.setData(m_model.index(index), included, CleanupPlanListModel::IncludedRole);
    emit includedChanged();
}

void CleanupController::setAllIncluded(bool included)
{
    m_model.setAllIncluded(included);
    emit includedChanged();
}

std::shared_ptr<QtProgressReporter> CleanupController::makeReporter()
{
    auto reporter = std::make_shared<QtProgressReporter>();
    connect(reporter.get(), &QtProgressReporter::started, this,
            [this](const QString &, int total) { setScanProgress(0, total); });
    connect(reporter.get(), &QtProgressReporter::progressed, this,
            [this](int current) { setScanProgress(current, m_scanTotal); });
    return reporter;
}

void CleanupController::onRescanFinished()
{
    CleanupTaskResult result = m_watcher.result();

    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
        setBusy(false);
        return;
    }

    m_model.setPlans(std::move(result.plans));
    setBusy(false);
    emit plansChanged();
    emit includedChanged();
    refreshPendingDeletions();
}

void CleanupController::apply()
{
    if (m_busy) {
        return;
    }
    std::vector<domain::DuplicateCleanupPlan> includedPlans;
    const auto &plans = m_model.plans();
    for (size_t i = 0; i < plans.size(); ++i) {
        if (m_model.included(i)) {
            includedPlans.push_back(plans[i]);
        }
    }
    if (includedPlans.empty()) {
        return;
    }

    setErrorMessage({});
    setStatusMessage({});
    setScanProgress(0, 0);
    setBusy(true);
    setWriting(true);
    m_lastBackups.clear();
    emit canUndoChanged();

    m_writeWatcher.setFuture(QtConcurrent::run(runApplyTask, m_format, m_path, std::move(includedPlans), makeReporter()));
}

void CleanupController::onWriteFinished()
{
    CleanupWriteResult result = m_writeWatcher.result();
    m_lastBackups = std::move(result.backups);
    emit canUndoChanged();

    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
    } else {
        setStatusMessage(result.statusMessage);
    }
    setBusy(false);
    setWriting(false);

    rescan();
}

void CleanupController::undoLastOperation()
{
    if (m_busy || m_lastBackups.empty()) {
        return;
    }
    setErrorMessage({});
    setStatusMessage({});
    setScanProgress(0, 0);
    setBusy(true);
    setWriting(true);

    m_writeWatcher.setFuture(QtConcurrent::run(runUndoTask, m_lastBackups, makeReporter()));
}

void CleanupController::refreshPendingDeletions()
{
    if (m_path.isEmpty()) {
        return;
    }
    fs::path stickRoot = fs::path(m_path.toStdString()).parent_path();
    infrastructure::cleanup::PendingDeletionManifest manifest(
        (stickRoot / ".djconvert-pending-deletions.jsonl").string());

    // rekordbox and Engine each accumulate their own separate pending
    // entries (see PendingDeletion::format) -- this page only ever shows
    // the one currently selected via the format toggle, same as
    // everything else on it.
    std::vector<infrastructure::cleanup::PendingDeletion> filtered;
    for (auto &entry : manifest.list()) {
        if (entry.format == m_format.toStdString()) {
            filtered.push_back(std::move(entry));
        }
    }
    m_pendingModel.setEntries(std::move(filtered));
    emit pendingDeletionsChanged();
}

void CleanupController::setPendingDeletionIncluded(int index, bool included)
{
    m_pendingModel.setData(m_pendingModel.index(index), included, PendingDeletionListModel::IncludedRole);
    emit pendingDeletionsChanged();
}

void CleanupController::setAllPendingDeletionIncluded(bool included)
{
    m_pendingModel.setAllIncluded(included);
    emit pendingDeletionsChanged();
}

void CleanupController::deleteSelectedPendingFiles()
{
    if (m_busy) {
        return;
    }
    auto selected = m_pendingModel.includedEntries();
    if (selected.empty()) {
        return;
    }

    setErrorMessage({});
    setStatusMessage({});
    setScanProgress(0, 0);
    setBusy(true);
    setWriting(true);

    m_pendingWriteWatcher.setFuture(
        QtConcurrent::run(runDeletePendingTask, m_format, m_path, std::move(selected), makeReporter()));
}

void CleanupController::onDeletePendingFinished()
{
    PendingDeletionApplyResult result = m_pendingWriteWatcher.result();

    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
    } else {
        setStatusMessage(result.statusMessage);
    }
    setBusy(false);
    setWriting(false);
    refreshPendingDeletions();
}

void CleanupController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void CleanupController::setWriting(bool writing)
{
    if (m_writing == writing) {
        return;
    }
    m_writing = writing;
    emit writingChanged();
}

void CleanupController::setScanProgress(int current, int total)
{
    if (m_scanCurrent == current && m_scanTotal == total) {
        return;
    }
    m_scanCurrent = current;
    m_scanTotal = total;
    emit scanProgressChanged();
}

void CleanupController::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

void CleanupController::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) {
        return;
    }
    m_statusMessage = message;
    emit statusMessageChanged();
}

}  // namespace djconvert::gui
