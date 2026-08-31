#include "cleanup_controller.hpp"

#include <QStringList>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <unordered_map>

#include "application/ports/backup_store.hpp"
#include "application/ports/cue_writer.hpp"
#include "application/ports/library_cleanup_writer.hpp"
#include "application/ports/operation_log.hpp"
#include "application/use_cases/scan_library.hpp"
#include "domain/duplicate_cue_consolidation.hpp"
#include "gui/onelibrary_cue_writer_adapter.hpp"
#include "gui/qt_progress_reporter.hpp"
#include "gui/write_guard.hpp"
#include "infrastructure/backup/filesystem_backup_store.hpp"
#include "infrastructure/backup/stick_write_lock.hpp"
#include "infrastructure/cleanup/pending_deletion_applier.hpp"
#include "infrastructure/cleanup/pending_deletion_manifest.hpp"
#include "infrastructure/cleanup/pending_deletion_resolver.hpp"
#include "infrastructure/engine/libdjinterop_engine_cleanup_writer.hpp"
#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/engine/libdjinterop_engine_reader.hpp"
#include "infrastructure/logging/file_operation_log.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/onelibrary/onelibrary_reader.hpp"
#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"
#include "infrastructure/rekordbox/pdb_lookup.hpp"
#include "infrastructure/rekordbox/pdb_row_writer.hpp"
#include "infrastructure/rekordbox/rekordbox_cleanup_writer.hpp"
#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"

namespace seabass::gui
{

namespace fs = std::filesystem;

namespace
{

// Mirrors cli/main.cpp's humanSize() exactly, see duplicates_controller
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

// side/artworkPath/cues added so this can feed TrackWaveformCard
// directly (same shape trackToMap() produces elsewhere in this app) --
// deliberately no "waveform" field, see TrackWaveformCard.qml's own
// comment: that's fetched on demand by QML, not precomputed here.
QVariantMap trackSummary(const domain::Track &t)
{
    QVariantMap m;
    m["side"] = QString::fromStdString(t.format);
    m["sourceId"] = QString::fromStdString(t.sourceId);
    m["title"] = QString::fromStdString(t.title);
    m["artist"] = QString::fromStdString(t.artist);
    m["filePath"] = QString::fromStdString(t.filePath);
    m["artworkPath"] = QString::fromStdString(t.artworkPath);
    m["bitrate"] = t.bitrate;
    m["durationMs"] = t.durationSeconds * 1000.0;
    m["sizeBytes"] = static_cast<qulonglong>(t.fileSizeBytes);
    m["sizeHuman"] = humanSize(t.fileSizeBytes);

    QVariantList cues;
    for (const auto &c : t.cues) {
        QVariantMap cueMap;
        cueMap["kind"] = c.kind == domain::CuePoint::Kind::Hot ? QStringLiteral("hot") : QStringLiteral("memory");
        cueMap["hotCueNumber"] = c.hotCueNumber;
        cueMap["positionMs"] = c.positionMs;
        cueMap["color"] = QString::fromStdString(c.color);
        cues << cueMap;
    }
    m["cues"] = cues;
    return m;
}

}  // namespace

CleanupPlanListModel::CleanupPlanListModel(QObject *parent) : QAbstractListModel(parent) {}

int CleanupPlanListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_visibleIndices.size());
}

QVariant CleanupPlanListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= m_visibleIndices.size()) {
        return {};
    }
    size_t realIndex = m_visibleIndices[static_cast<size_t>(index.row())];
    const auto &plan = m_plans[realIndex];
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
    case HasUnpreservableDataAtRiskRole:
        return plan.hasUnpreservableDataAtRisk;
    case WastedBytesHumanRole:
        return humanSize(wastedBytes(plan));
    case NewCueCountRole:
        return static_cast<int>(plan.mergedCuesForSurvivor.size()) - static_cast<int>(plan.survivor.cues.size());
    case IncludedRole:
        return m_included[realIndex];
    default:
        return {};
    }
}

bool CleanupPlanListModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= m_visibleIndices.size()) {
        return false;
    }
    if (role != IncludedRole) {
        return false;
    }
    m_included[m_visibleIndices[static_cast<size_t>(index.row())]] = value.toBool();
    emit dataChanged(index, index, {IncludedRole});
    return true;
}

QHash<int, QByteArray> CleanupPlanListModel::roleNames() const
{
    return {
        {SurvivorRole, "survivor"},
        {ToRemoveRole, "toRemove"},
        {DiffersRole, "differs"},
        {HasUnpreservableDataAtRiskRole, "hasUnpreservableDataAtRisk"},
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
        // Groups where quality and length disagree, or where a copy
        // carries real rating/comment/play-count/last-played data that
        // would be silently lost, default to excluded -- see
        // DuplicateCleanupPlan::differs and ::hasUnpreservableDataAtRisk's
        // own doc comments (deliberately separate flags/reasons).
        m_included[i] = !m_plans[i].differs && !m_plans[i].hasUnpreservableDataAtRisk;
    }
    m_visibleIndices.resize(m_plans.size());
    for (size_t i = 0; i < m_plans.size(); ++i) {
        m_visibleIndices[i] = i;
    }
    endResetModel();
}

bool CleanupPlanListModel::included(size_t index) const
{
    return index < m_included.size() && m_included[index];
}

void CleanupPlanListModel::setAllIncluded(bool included)
{
    if (m_visibleIndices.empty()) {
        return;
    }
    for (size_t realIndex : m_visibleIndices) {
        m_included[realIndex] = included;
    }
    emit dataChanged(index(0), index(static_cast<int>(m_visibleIndices.size()) - 1), {IncludedRole});
}

int CleanupPlanListModel::includedCount() const
{
    return static_cast<int>(std::count(m_included.begin(), m_included.end(), true));
}

namespace
{
bool matchesQuery(const domain::Track &t, const QString &query)
{
    return QString::fromStdString(t.title).contains(query, Qt::CaseInsensitive) ||
           QString::fromStdString(t.artist).contains(query, Qt::CaseInsensitive);
}
}  // namespace

void CleanupPlanListModel::setFilter(const QString &query)
{
    beginResetModel();
    m_visibleIndices.clear();
    for (size_t i = 0; i < m_plans.size(); ++i) {
        if (query.isEmpty() || matchesQuery(m_plans[i].survivor, query) ||
            std::any_of(m_plans[i].toRemove.begin(), m_plans[i].toRemove.end(),
                        [&query](const domain::Track &t) { return matchesQuery(t, query); })) {
            m_visibleIndices.push_back(i);
        }
    }
    endResetModel();
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
    case SizeHumanRole:
        return humanSize(entry.fileSizeBytes);
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
        {SizeHumanRole, "sizeHuman"},
    };
}

void PendingDeletionListModel::setEntries(std::vector<infrastructure::cleanup::PendingDeletion> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    m_included.assign(m_entries.size(), true);
    endResetModel();
}

std::uint64_t PendingDeletionListModel::totalBytes() const
{
    std::uint64_t total = 0;
    for (const auto &entry : m_entries) {
        total += entry.fileSizeBytes;
    }
    return total;
}

std::uint64_t PendingDeletionListModel::includedBytes() const
{
    std::uint64_t total = 0;
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (i < m_included.size() && m_included[i]) {
            total += m_entries[i].fileSizeBytes;
        }
    }
    return total;
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
// that need backing up once per session, rekordbox's export.pdb,
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
    // Non-empty only for rekordbox, used to best-effort also write
    // cues into OneLibrary/exportLibrary.db (see runApplyTask()) if it
    // exists alongside export.pdb on this stick.
    std::string pioneerRoot;
};

// oneLibrarySourceIdToPath is only consulted when format == "onelibrary"
// -- built by the caller from the tracks actually about to be written
// (OneLibraryCueWriterAdapter/OneLibraryCleanupWriterAdapter's own
// comments explain why sourceId alone isn't enough for this format).
FormatContext makeContext(const QString &format, const QString &path,
                           const std::unordered_map<std::string, std::string> &oneLibrarySourceIdToPath = {})
{
    fs::path stickRoot = fs::path(path.toStdString()).parent_path();

    FormatContext ctx;
    ctx.backupStore = std::make_unique<infrastructure::backup::FilesystemBackupStore>(
        (stickRoot / ".seabass-backups").string());
    ctx.log = std::make_unique<infrastructure::logging::FileOperationLog>((stickRoot / ".seabass.log").string());
    ctx.pendingDeletionManifestPath = (stickRoot / ".seabass-pending-deletions.jsonl").string();

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
        if (infrastructure::onelibrary::OneLibraryCueWriter::existsFor(pioneerRoot)) {
            ctx.extraFilesToBackUp.push_back(infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(pioneerRoot));
        }
    } else if (format == "engine") {
        std::string engineLibraryPath = path.toStdString();
        ctx.cueWriter = std::make_unique<infrastructure::engine::LibdjinteropEngineCueWriter>(engineLibraryPath);
        ctx.cleanupWriter = std::make_unique<infrastructure::engine::LibdjinteropEngineCleanupWriter>(engineLibraryPath);
        std::string engineDbFile = (fs::path(engineLibraryPath) / "Database2" / "m.db").string();
        ctx.filesToBackUpFor = [engineDbFile](const std::string &) -> std::vector<std::string> { return {engineDbFile}; };
    } else {
        // onelibrary. `path` here is the PIONEER root, same as the
        // rekordbox branch -- OneLibrary lives alongside export.pdb, see
        // DuplicatesController's own onelibrary branch for the same
        // convention.
        std::string pioneerRoot = path.toStdString();
        ctx.cueWriter =
            std::make_unique<OneLibraryCueWriterAdapter>(pioneerRoot, oneLibrarySourceIdToPath);
        ctx.cleanupWriter =
            std::make_unique<OneLibraryCleanupWriterAdapter>(pioneerRoot, oneLibrarySourceIdToPath);
        ctx.extraFilesToBackUp = {infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(pioneerRoot)};
        ctx.filesToBackUpFor = [](const std::string &) -> std::vector<std::string> { return {}; };
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
        std::string backupDir = (fs::path(path.toStdString()).parent_path() / ".seabass-backups").string();
        infrastructure::backup::StickWriteLock lock(backupDir + "/.write.lock");

        std::unordered_map<std::string, std::string> oneLibrarySourceIdToPath;
        if (format == "onelibrary") {
            for (const auto &plan : includedPlans) {
                oneLibrarySourceIdToPath[plan.survivor.sourceId] = plan.survivor.filePath;
                for (const auto &doomed : plan.toRemove) {
                    oneLibrarySourceIdToPath[doomed.sourceId] = doomed.filePath;
                }
            }
        }
        auto ctx = makeContext(format, path, oneLibrarySourceIdToPath);
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
        QStringList propagationWarnings;

        for (const auto &plan : includedPlans) {
            for (const auto &f : ctx.filesToBackUpFor(plan.survivor.sourceId)) {
                backupIfNeeded(f);
            }

            // sourceId -> filePath among this group's own tracks -- only
            // OneLibrary's propagateMissingFieldsForPath() below needs
            // this (it identifies tracks by path, not sourceId, same
            // reason OneLibraryCueWriter's class comment gives).
            auto findTrackFilePath = [&plan](const std::string &sourceId) -> std::string {
                for (const auto &t : plan.group.tracks) {
                    if (t.sourceId == sourceId) {
                        return t.filePath;
                    }
                }
                return {};
            };

            if (plan.mergedCuesForSurvivor.size() > plan.survivor.cues.size()) {
                ctx.cueWriter->writeHotCues(plan.survivor.sourceId, plan.mergedCuesForSurvivor);
                cuesPreserved += static_cast<int>(plan.mergedCuesForSurvivor.size() - plan.survivor.cues.size());
                ctx.log->record("cleanup: wrote merged cues onto survivor track id=" + plan.survivor.sourceId);

                // Best-effort secondary write, alongside the primary
                // export.pdb write above, never fatal to this
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
            }

            // Fills in the survivor's missing bpm/key/artwork from
            // whichever other copy in the group has each (see
            // domain::DuplicateCleanupPlan's own comment on why this is
            // a per-field "fill a gap", not a merge -- each field may
            // have come from a different donor track). Primary-format
            // writes here are treated exactly like the merged-cues write
            // above: NOT best-effort, a failure aborts the whole
            // operation (caught by this function's own outer try). Only
            // the OneLibrary *mirror* of a rekordbox write, below, is
            // best-effort, same as the merged-cues mirror.
            if (plan.bpmForSurvivor || plan.keyForSurvivor || plan.artworkPathForSurvivor) {
                if (format == "rekordbox") {
                    std::string pdbPath = ctx.pioneerRoot + "/rekordbox/export.pdb";
                    infrastructure::rekordbox::PdbRowWriter fieldWriter(pdbPath);
                    uint32_t survivorId = static_cast<uint32_t>(std::stoul(plan.survivor.sourceId));
                    if (plan.keyForSurvivor) {
                        fieldWriter.copyTrackFieldsIfMissing(
                            static_cast<uint32_t>(std::stoul(plan.keyDonorSourceId)), survivorId, true, false, false);
                    }
                    if (plan.bpmForSurvivor) {
                        fieldWriter.copyTrackFieldsIfMissing(
                            static_cast<uint32_t>(std::stoul(plan.bpmDonorSourceId)), survivorId, false, true, false);
                    }
                    if (plan.artworkPathForSurvivor) {
                        fieldWriter.copyTrackFieldsIfMissing(
                            static_cast<uint32_t>(std::stoul(plan.artworkDonorSourceId)), survivorId, false, false,
                            true);
                    }
                    if (!fieldWriter.commit()) {
                        throw std::runtime_error("failed to write " + pdbPath);
                    }
                    ctx.log->record("cleanup: propagated missing bpm/key/artwork onto survivor track id=" +
                                     plan.survivor.sourceId);
                } else if (format == "engine") {
                    // Artwork is deliberately not offered here -- Engine
                    // track artwork isn't writable through libdjinterop
                    // today, see propagateMissingFields()'s own doc
                    // comment. ctx.cueWriter is always this concrete type
                    // for format == "engine" (see makeContext()).
                    auto *engineCueWriter =
                        static_cast<infrastructure::engine::LibdjinteropEngineCueWriter *>(ctx.cueWriter.get());
                    engineCueWriter->propagateMissingFields(plan.survivor.sourceId, plan.bpmForSurvivor,
                                                              plan.keyForSurvivor);
                    ctx.log->record("cleanup: propagated missing bpm/key onto survivor track id=" +
                                     plan.survivor.sourceId);
                } else if (format == "onelibrary") {
                    infrastructure::onelibrary::OneLibraryCueWriter fieldWriter(path.toStdString());
                    if (plan.keyForSurvivor) {
                        std::string donorPath = findTrackFilePath(plan.keyDonorSourceId);
                        if (!donorPath.empty() && !plan.survivor.filePath.empty()) {
                            fieldWriter.propagateMissingFieldsForPath(donorPath, plan.survivor.filePath, false, true,
                                                                       false);
                        }
                    }
                    if (plan.bpmForSurvivor) {
                        std::string donorPath = findTrackFilePath(plan.bpmDonorSourceId);
                        if (!donorPath.empty() && !plan.survivor.filePath.empty()) {
                            fieldWriter.propagateMissingFieldsForPath(donorPath, plan.survivor.filePath, true, false,
                                                                       false);
                        }
                    }
                    if (plan.artworkPathForSurvivor) {
                        std::string donorPath = findTrackFilePath(plan.artworkDonorSourceId);
                        if (!donorPath.empty() && !plan.survivor.filePath.empty()) {
                            fieldWriter.propagateMissingFieldsForPath(donorPath, plan.survivor.filePath, false, false,
                                                                       true);
                        }
                    }
                    ctx.log->record("cleanup: propagated missing bpm/key/artwork onto survivor track id=" +
                                     plan.survivor.sourceId);
                }

                // Best-effort mirror onto OneLibrary too, same reasoning
                // and structure as the merged-cues mirror write above --
                // only reachable when this run's primary format is
                // rekordbox (format == "onelibrary" already wrote
                // OneLibrary directly above, as the primary write).
                if (format == "rekordbox" && !ctx.pioneerRoot.empty() && !plan.survivor.filePath.empty() &&
                    infrastructure::onelibrary::OneLibraryCueWriter::existsFor(ctx.pioneerRoot)) {
                    try {
                        infrastructure::onelibrary::OneLibraryCueWriter oneLibFieldWriter(ctx.pioneerRoot);
                        if (plan.keyForSurvivor) {
                            std::string donorPath = findTrackFilePath(plan.keyDonorSourceId);
                            if (!donorPath.empty()) {
                                oneLibFieldWriter.propagateMissingFieldsForPath(donorPath, plan.survivor.filePath,
                                                                                 false, true, false);
                            }
                        }
                        if (plan.bpmForSurvivor) {
                            std::string donorPath = findTrackFilePath(plan.bpmDonorSourceId);
                            if (!donorPath.empty()) {
                                oneLibFieldWriter.propagateMissingFieldsForPath(donorPath, plan.survivor.filePath,
                                                                                 true, false, false);
                            }
                        }
                        if (plan.artworkPathForSurvivor) {
                            std::string donorPath = findTrackFilePath(plan.artworkDonorSourceId);
                            if (!donorPath.empty()) {
                                oneLibFieldWriter.propagateMissingFieldsForPath(donorPath, plan.survivor.filePath,
                                                                                 false, false, true);
                            }
                        }
                        ctx.log->record("cleanup: also propagated missing bpm/key/artwork into OneLibrary (id=" +
                                         plan.survivor.sourceId + ")");
                    } catch (const std::exception &e) {
                        QString warning = QString("OneLibrary field propagation failed for \"%1\": %2")
                                               .arg(QString::fromStdString(plan.survivor.title))
                                               .arg(QString::fromStdString(e.what()));
                        propagationWarnings << warning;
                        ctx.log->record("cleanup: " + warning.toStdString());
                    }
                }
            }

            for (const auto &doomed : plan.toRemove) {
                ctx.cleanupWriter->removeTrackReplacingWith(doomed.sourceId, plan.survivor.sourceId);
                ctx.log->record("cleanup: removed duplicate track id=" + doomed.sourceId + " (\"" + doomed.title +
                                 "\"), replaced by survivor id=" + plan.survivor.sourceId);

                // Best-effort OneLibrary mirror. Without this, the
                // doomed track's own OneLibrary row is left pointing at a
                // file this loop is about to delete, becoming an orphan
                // (this is exactly how real orphaned rows were found on
                // production data, see docs/onelibrary-format.md).
                // Never fatal to the primary write above.
                if (!ctx.pioneerRoot.empty() && !doomed.filePath.empty() && !plan.survivor.filePath.empty() &&
                    infrastructure::onelibrary::OneLibraryCueWriter::existsFor(ctx.pioneerRoot)) {
                    try {
                        infrastructure::onelibrary::OneLibraryCueWriter oneLibWriter(ctx.pioneerRoot);
                        // Reassigns the doomed row's OneLibrary playlist
                        // memberships onto the survivor instead of
                        // dropping them -- see OneLibraryCueWriter::
                        // removeTrackByPathReplacingWith()'s own comment.
                        oneLibWriter.removeTrackByPathReplacingWith(doomed.filePath, plan.survivor.filePath);
                        ctx.log->record("cleanup: also removed OneLibrary row for id=" + doomed.sourceId);
                    } catch (const std::exception &e) {
                        QString warning = QString("OneLibrary row removal failed for \"%1\": %2")
                                               .arg(QString::fromStdString(doomed.title))
                                               .arg(QString::fromStdString(e.what()));
                        oneLibraryWarnings << warning;
                        ctx.log->record("cleanup: " + warning.toStdString());
                    }
                }

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
            result.statusMessage += QString(" (%1 OneLibrary cue write(s) failed. The primary library write "
                                             "above still succeeded and was not affected: %2)")
                                         .arg(oneLibraryWarnings.size())
                                         .arg(oneLibraryWarnings.join("; "));
        }
        if (!propagationWarnings.isEmpty()) {
            result.statusMessage += QString(" (%1 OneLibrary bpm/key/artwork mirror write(s) failed. The primary "
                                             "library write above still succeeded and was not affected: %2)")
                                         .arg(propagationWarnings.size())
                                         .arg(propagationWarnings.join("; "));
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
        result.statusMessage = QString("Undone - restored %1 file(s) to their state before the last cleanup. "
                                        "Removed library entries and the pending-deletions list are not reverted "
                                        "by this. They reflect what was written to the backups, not what's on "
                                        "disk after restoring.")
                                    .arg(restored);
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

// Runs entirely on a background thread (see CleanupController::
// deleteSelectedPendingFiles()). Re-scans the library fresh, never
// trusts the manifest alone, see resolvePendingDeletions()'s own doc
// comment, deletes every `selected` entry resolvePendingDeletions()
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
        infrastructure::backup::StickWriteLock lock((stickRoot / ".seabass-backups" / ".write.lock").string());
        infrastructure::cleanup::PendingDeletionManifest manifest(
            (stickRoot / ".seabass-pending-deletions.jsonl").string());
        infrastructure::logging::FileOperationLog log((stickRoot / ".seabass.log").string());

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

        // The actual deletion (the one place in the app that
        // permanently destroys real audio file content) lives in
        // infrastructure/cleanup/pending_deletion_applier.cpp, Qt-free
        // and unit-tested there -- this just logs/formats its result.
        auto outcomes = infrastructure::cleanup::applyPendingDeletions(resolution.safeToDelete, manifest);

        int deleted = 0;
        int failed = 0;
        using Status = infrastructure::cleanup::PendingDeletionOutcome::Status;
        for (const auto &outcome : outcomes) {
            switch (outcome.status) {
                case Status::AlreadyAbsent:
                    deleted++;
                    log.record("cleanup: pending deletion already absent from disk, clearing from manifest -> " +
                                outcome.entry.filePath);
                    break;
                case Status::Deleted:
                    deleted++;
                    log.record("cleanup: deleted orphaned duplicate file (backup " + outcome.entry.backupId +
                                ") -> " + outcome.entry.filePath);
                    break;
                case Status::Failed:
                    failed++;
                    log.record("cleanup: failed to delete orphaned duplicate file -> " + outcome.entry.filePath +
                                " (" + outcome.failureReason + ")");
                    break;
            }
        }

        QStringList parts;
        parts << QString("deleted %1 file(s) from disk").arg(deleted);
        if (!resolution.stillReferenced.empty()) {
            parts << QString("%1 file(s) still referenced by a current track, left alone, not deleted")
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
        } else if (format == "onelibrary") {
            infrastructure::onelibrary::OneLibraryReader reader(path.toStdString());
            reader.setProgressReporter(*reporter);
            tracks = application::ScanLibrary(reader).execute();
        } else {
            infrastructure::engine::LibdjinteropEngineReader reader(path.toStdString());
            reader.setProgressReporter(*reporter);
            tracks = application::ScanLibrary(reader).execute();
        }

        // Streaming tracks (Engine/TIDAL) have no real local file.
        // Never let duplicate detection consider one, whether as
        // survivor or doomed. See domain::Track::streamingSource's own
        // doc comment for why.
        tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
                                     [](const domain::Track &t) { return !t.streamingSource.empty(); }),
                     tracks.end());

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

// Runs entirely on a background thread (see CleanupController::
// planManualMerge()). Unlike runRescanTask(), never runs
// DuplicateTrackFinder. The two tracks are already a user-declared
// match, not something to (re-)detect. Re-scans fresh rather than
// trusting whatever ScanPage had in memory when the merge was requested,
// same "never trust stale data right before a mutating decision" stance
// every other write path in this codebase already takes.
CleanupTaskResult runManualMergeTask(QString format, QString path, QString sourceIdA, QString sourceIdB,
                                      std::shared_ptr<QtProgressReporter> reporter)
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

        std::string idA = sourceIdA.toStdString();
        std::string idB = sourceIdB.toStdString();
        const domain::Track *trackA = nullptr;
        const domain::Track *trackB = nullptr;
        for (const auto &t : tracks) {
            if (t.sourceId == idA) {
                trackA = &t;
            } else if (t.sourceId == idB) {
                trackB = &t;
            }
        }
        if (!trackA || !trackB) {
            result.errorMessage = "One or both tracks no longer exist in this library. Rescan and try again.";
            return result;
        }
        if (!trackA->streamingSource.empty() || !trackB->streamingSource.empty()) {
            result.errorMessage = "A streaming track (no local file) can't be merged.";
            return result;
        }

        domain::DuplicateGroup group;
        group.tracks = {*trackA, *trackB};
        result.plans = {domain::DuplicateCleanupPlanner::plan(group)};
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

bool CleanupController::hasOneLibrary(const QString &pioneerRoot) const
{
    return infrastructure::onelibrary::OneLibraryCueWriter::existsFor(pioneerRoot.toStdString());
}

void CleanupController::loadPendingDeletionsOnly(const QString &format, const QString &path)
{
    m_format = format;
    m_path = path;
    refreshPendingDeletions();
}

void CleanupController::planManualMerge(const QString &format, const QString &path, const QString &sourceIdA,
                                         const QString &sourceIdB)
{
    if (m_busy) {
        return;
    }
    m_format = format;
    m_path = path;
    setErrorMessage({});
    // Also cleared here (unlike rescan(), which never needs to): a
    // second merge in the same page session must not have the previous
    // one's leftover success message hide this new plan's own preview
    // (see ScanPage.qml's Repeater, gated on statusMessage being empty).
    setStatusMessage({});
    setScanProgress(0, 0);
    setBusy(true);

    m_watcher.setFuture(QtConcurrent::run(runManualMergeTask, format, path, sourceIdA, sourceIdB, makeReporter()));
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

void CleanupController::search(const QString &query)
{
    m_model.setFilter(query);
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
        (stickRoot / ".seabass-pending-deletions.jsonl").string());

    // rekordbox and Engine each accumulate their own separate pending
    // entries (see PendingDeletion::format). This page only ever shows
    // the one currently selected via the format toggle, same as
    // everything else on it.
    std::vector<infrastructure::cleanup::PendingDeletion> filtered;
    for (auto &entry : manifest.list()) {
        if (entry.format != m_format.toStdString()) {
            continue;
        }
        // fileSizeBytes is never persisted in the manifest (see its own
        // doc comment). Stat it fresh here so "how much space would
        // deleting this free up" reflects the file's real current size,
        // not a guess. 0 if the file's already gone; still worth listing
        // (deleteSelectedPendingFiles() clears an already-absent entry
        // from the manifest instead of erroring).
        std::error_code ec;
        entry.fileSizeBytes = fs::file_size(entry.filePath, ec);
        if (ec) {
            entry.fileSizeBytes = 0;
        }
        filtered.push_back(std::move(entry));
    }
    m_pendingModel.setEntries(std::move(filtered));
    emit pendingDeletionsChanged();
}

QString CleanupController::totalPendingBytesHuman() const
{
    return humanSize(m_pendingModel.totalBytes());
}

QString CleanupController::includedPendingBytesHuman() const
{
    return humanSize(m_pendingModel.includedBytes());
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

}  // namespace seabass::gui
