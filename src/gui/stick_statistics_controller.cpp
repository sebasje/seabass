#include "stick_statistics_controller.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <filesystem>
#include <set>
#include <stdexcept>

#include "application/use_cases/scan_library.hpp"
#include "domain/disk_usage.hpp"
#include "domain/filesystem_compatibility.hpp"
#include "domain/library_statistics.hpp"
#include "infrastructure/benchmark/stick_benchmark_history.hpp"
#include "infrastructure/benchmark/stick_speed_benchmark.hpp"
#include "infrastructure/engine/libdjinterop_engine_reader.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/onelibrary/onelibrary_reader.hpp"
#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"
#include "infrastructure/system/stick_hardware_info.hpp"

namespace seabass::gui
{

namespace fs = std::filesystem;

namespace
{

QVariantMap toVariant(const domain::LibraryStatistics &stats)
{
    QVariantMap m;
    m["trackCount"] = stats.trackCount;
    m["playlistCount"] = stats.playlistCount;
    m["totalCuePoints"] = stats.totalCuePoints;
    m["hotCueCount"] = stats.hotCueCount;
    m["memoryCueCount"] = stats.memoryCueCount;
    m["ratedTrackCount"] = stats.ratedTrackCount;
    m["commentedTrackCount"] = stats.commentedTrackCount;
    m["streamingTrackCount"] = stats.streamingTrackCount;

    QVariantMap perKey;
    for (const auto &[key, count] : stats.tracksPerKey) {
        perKey[key.empty() ? QStringLiteral("(unknown)") : QString::fromStdString(key)] = count;
    }
    m["tracksPerKey"] = perKey;

    QVariantMap perFormat;
    for (const auto &[format, count] : stats.tracksPerFileFormat) {
        perFormat[format.empty() ? QStringLiteral("(unknown)") : QString::fromStdString(format)] = count;
    }
    m["tracksPerFileFormat"] = perFormat;

    QVariantMap byService;
    for (const auto &[service, count] : stats.streamingTracksByService) {
        byService[QString::fromStdString(service)] = count;
    }
    m["streamingTracksByService"] = byService;

    QVariantList bpmList;
    for (const auto &bucket : stats.bpmDistribution) {
        QVariantMap b;
        b["rangeStart"] = bucket.rangeStart;
        b["count"] = bucket.count;
        bpmList << b;
    }
    m["bpmDistribution"] = bpmList;

    return m;
}

QVariantMap toVariant(const domain::DiskUsageNode &node)
{
    QVariantMap m;
    m["label"] = QString::fromStdString(node.label);
    m["sizeBytes"] = QVariant::fromValue<qulonglong>(node.sizeBytes);
    QVariantList children;
    for (const auto &child : node.children) {
        children << toVariant(child);
    }
    m["children"] = children;
    return m;
}

QVariantMap toVariant(const infrastructure::system::StickHardwareInfo &hw,
                      const domain::FilesystemCompatibilityInfo &compat)
{
    QVariantMap m;
    m["filesystem"] = QString::fromStdString(hw.filesystem);
    m["displayName"] = QString::fromStdString(compat.displayName);
    m["maxFileSize"] = QString::fromStdString(compat.maxFileSize);
    m["hardwareNotes"] = QString::fromStdString(compat.hardwareNotes);
    m["recommendedForDjHardware"] = compat.recommendedForDjHardware;
    m["totalBytes"] = QVariant::fromValue<qulonglong>(hw.totalBytes);
    m["freeBytes"] = QVariant::fromValue<qulonglong>(hw.freeBytes);
    m["usbSpeedLabel"] = QString::fromStdString(hw.usbSpeedLabel);
    m["usbSpeedMbps"] = hw.usbSpeedMbps;
    m["stickIdentifier"] = QString::fromStdString(hw.stickIdentifier);
    return m;
}

QVariantMap toVariant(const infrastructure::benchmark::BenchmarkRecord &r)
{
    QVariantMap m;
    m["ranAt"] = QString::fromStdString(r.ranAt);
    m["stickLabel"] = QString::fromStdString(r.stickLabel);
    m["filesystem"] = QString::fromStdString(r.filesystem);
    m["usbSpeedLabel"] = QString::fromStdString(r.usbSpeedLabel);
    m["usbSpeedMbps"] = r.usbSpeedMbps;
    m["databaseReadMbps"] = r.databaseReadMbps;
    m["audioReadMbps"] = r.audioReadMbps;
    m["score"] = r.score;
    return m;
}

// Best-effort recursive directory size, skipping anything that errors
// (permission-denied entries, a symlink loop, the stick being unplugged
// mid-walk) rather than aborting the whole statistics scan over it.
std::uint64_t directorySizeBytes(const std::string &dir)
{
    std::error_code ec;
    if (dir.empty() || !fs::exists(dir, ec) || ec) {
        return 0;
    }
    std::uint64_t total = 0;
    auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
    auto end = fs::recursive_directory_iterator();
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc) && !fileEc) {
            auto size = it->file_size(fileEc);
            if (!fileEc) {
                total += size;
            }
        }
    }
    return total;
}

std::string stickRootFromPaths(const QString &rekordboxPath, const QString &enginePath)
{
    if (!rekordboxPath.isEmpty()) {
        return fs::path(rekordboxPath.toStdString()).parent_path().string();
    }
    if (!enginePath.isEmpty()) {
        return fs::path(enginePath.toStdString()).parent_path().string();
    }
    return "";
}

// Runs entirely on a background thread (see
// StickStatisticsController::scan()) -- no access to the controller.
StickStatisticsScanResult runScanTask(QString stickLabel, QString rekordboxPath, QString enginePath)
{
    StickStatisticsScanResult result;
    try {
        std::string stickRoot = stickRootFromPaths(rekordboxPath, enginePath);
        auto hwInfo = infrastructure::system::readStickHardwareInfo(stickRoot, stickLabel.toStdString());
        auto compat = domain::FilesystemCompatibility::lookup(hwInfo.filesystem);
        result.filesystemInfo = toVariant(hwInfo, compat);

        std::vector<domain::Track> combinedTracks;
        std::set<std::string> seenFilePaths;
        std::vector<std::string> databaseFiles;

        if (!rekordboxPath.isEmpty()) {
            infrastructure::rekordbox::KaitaiRekordboxReader reader(rekordboxPath.toStdString());
            auto tracks = application::ScanLibrary(reader).execute();
            result.rekordboxStats = toVariant(domain::LibraryStatisticsCalculator::calculate(tracks));
            databaseFiles.push_back(rekordboxPath.toStdString() + "/rekordbox/export.pdb");
            for (auto &t : tracks) {
                if (!t.filePath.empty() && seenFilePaths.insert(t.filePath).second) {
                    combinedTracks.push_back(std::move(t));
                }
            }
        }
        if (!enginePath.isEmpty()) {
            infrastructure::engine::LibdjinteropEngineReader reader(enginePath.toStdString());
            auto tracks = application::ScanLibrary(reader).execute();
            result.engineStats = toVariant(domain::LibraryStatisticsCalculator::calculate(tracks));
            databaseFiles.push_back((fs::path(enginePath.toStdString()) / "Database2" / "m.db").string());
            for (auto &t : tracks) {
                // Streaming tracks have no local file by design (see
                // Track::streamingSource) -- never counted as disk usage
                // or sampled for the read-speed benchmark.
                if (!t.streamingSource.empty()) {
                    continue;
                }
                if (!t.filePath.empty() && seenFilePaths.insert(t.filePath).second) {
                    combinedTracks.push_back(std::move(t));
                }
            }
        }
        if (!rekordboxPath.isEmpty() &&
            infrastructure::onelibrary::OneLibraryCueWriter::existsFor(rekordboxPath.toStdString())) {
            infrastructure::onelibrary::OneLibraryReader reader(rekordboxPath.toStdString());
            auto tracks = application::ScanLibrary(reader).execute();
            result.oneLibraryStats = toVariant(domain::LibraryStatisticsCalculator::calculate(tracks));
            databaseFiles.push_back(infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(rekordboxPath.toStdString()));
            // OneLibrary's own tracks reference the same physical files
            // already counted via rekordbox/Engine above -- not added to
            // combinedTracks again, that would double-count disk usage.
        }

        // Sample a spread of real audio files (not just the first N,
        // which on most exports would all be the same artist/album) for
        // the read-speed benchmark's later, separate run.
        constexpr std::size_t maxSamples = 20;
        QVariantList audioSampleFiles;
        if (!combinedTracks.empty()) {
            std::size_t step = std::max<std::size_t>(1, combinedTracks.size() / maxSamples);
            for (std::size_t i = 0; i < combinedTracks.size() && static_cast<std::size_t>(audioSampleFiles.size()) < maxSamples;
                 i += step) {
                if (!combinedTracks[i].filePath.empty()) {
                    audioSampleFiles << QString::fromStdString(combinedTracks[i].filePath);
                }
            }
        }
        QVariantList databaseFilesVariant;
        for (const auto &f : databaseFiles) {
            databaseFilesVariant << QString::fromStdString(f);
        }
        result.benchmarkSamples["databaseFiles"] = databaseFilesVariant;
        result.benchmarkSamples["audioFiles"] = audioSampleFiles;

        // Disk usage breakdown. Audio Files/Artwork come from the
        // already-scanned, deduplicated track list (no extra filesystem
        // walk needed); Database & Analysis Files is a directory-size
        // walk of the catalog root(s) themselves (export.pdb, ANLZ
        // analysis files, Engine's Database2/*), with the artwork
        // subtotal subtracted back out since rekordbox stores artwork
        // inside its own PIONEER root and would otherwise be counted
        // twice.
        std::uint64_t audioBytes = 0;
        std::uint64_t artworkBytes = 0;
        std::set<std::string> distinctArtwork;
        for (const auto &t : combinedTracks) {
            audioBytes += t.fileSizeBytes;
            if (!t.artworkPath.empty() && distinctArtwork.insert(t.artworkPath).second) {
                std::error_code ec;
                auto size = fs::file_size(t.artworkPath, ec);
                if (!ec) {
                    artworkBytes += size;
                }
            }
        }

        std::uint64_t metadataBytes = 0;
        if (!rekordboxPath.isEmpty()) {
            metadataBytes += directorySizeBytes(rekordboxPath.toStdString());
        }
        if (!enginePath.isEmpty()) {
            metadataBytes += directorySizeBytes(enginePath.toStdString());
        }
        metadataBytes = metadataBytes > artworkBytes ? metadataBytes - artworkBytes : 0;

        std::uint64_t usedBytes = hwInfo.totalBytes > hwInfo.freeBytes ? hwInfo.totalBytes - hwInfo.freeBytes : 0;
        std::uint64_t accountedBytes = audioBytes + artworkBytes + metadataBytes;
        std::uint64_t otherBytes = usedBytes > accountedBytes ? usedBytes - accountedBytes : 0;

        domain::DiskUsageNode audioNode = domain::DiskUsageAnalyzer::byArtist(combinedTracks, 10);
        audioNode.label = "Audio Files";

        domain::DiskUsageNode root;
        root.label = stickLabel.toStdString();
        root.sizeBytes = hwInfo.totalBytes;
        root.children = {
            audioNode,
            {"Artwork", artworkBytes, {}},
            {"Database & Analysis Files", metadataBytes, {}},
            {"Other / Unaccounted", otherBytes, {}},
            {"Free Space", hwInfo.freeBytes, {}},
        };

        QVariantMap diskUsageMap;
        diskUsageMap["totalBytes"] = QVariant::fromValue<qulonglong>(hwInfo.totalBytes);
        diskUsageMap["usedBytes"] = QVariant::fromValue<qulonglong>(usedBytes);
        diskUsageMap["freeBytes"] = QVariant::fromValue<qulonglong>(hwInfo.freeBytes);
        diskUsageMap["root"] = toVariant(root);
        result.diskUsage = diskUsageMap;
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

// Runs entirely on a background thread (see
// StickStatisticsController::runBenchmark()).
QVariantMap runBenchmarkTask(QVariantMap samples, QVariantMap filesystemInfo, QString stickLabel)
{
    std::vector<std::string> databaseFiles;
    for (const auto &v : samples.value("databaseFiles").toList()) {
        databaseFiles.push_back(v.toString().toStdString());
    }
    std::vector<std::string> audioFiles;
    for (const auto &v : samples.value("audioFiles").toList()) {
        audioFiles.push_back(v.toString().toStdString());
    }

    auto benchmarkResult = infrastructure::benchmark::StickSpeedBenchmark::run(databaseFiles, audioFiles);

    infrastructure::benchmark::BenchmarkRecord record;
    record.stickLabel = stickLabel.toStdString();
    record.stickIdentifier = filesystemInfo.value("stickIdentifier").toString().toStdString();
    record.filesystem = filesystemInfo.value("filesystem").toString().toStdString();
    record.usbSpeedLabel = filesystemInfo.value("usbSpeedLabel").toString().toStdString();
    record.usbSpeedMbps = filesystemInfo.value("usbSpeedMbps").toDouble();
    record.databaseReadMbps = benchmarkResult.databaseReadMbps;
    record.audioReadMbps = benchmarkResult.audioReadMbps;
    record.score = benchmarkResult.score;

    QVariantMap out;
    try {
        infrastructure::benchmark::StickBenchmarkHistory history;
        history.record(record);
        out["success"] = true;
    } catch (const std::exception &e) {
        out["success"] = false;
        out["errorMessage"] = QString::fromStdString(e.what());
    }
    return out;
}

}  // namespace

StickStatisticsController::StickStatisticsController(QObject *parent) : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<StickStatisticsScanResult>::finished, this,
            &StickStatisticsController::onScanFinished);
    connect(&m_benchmarkWatcher, &QFutureWatcher<QVariantMap>::finished, this,
            &StickStatisticsController::onBenchmarkFinished);
}

void StickStatisticsController::scan(const QString &stickLabel, const QString &rekordboxPath, const QString &enginePath)
{
    if (m_busy) {
        return;
    }
    setErrorMessage({});
    setBusy(true);
    m_watcher.setFuture(QtConcurrent::run(runScanTask, stickLabel, rekordboxPath, enginePath));
}

void StickStatisticsController::onScanFinished()
{
    StickStatisticsScanResult result = m_watcher.result();
    setBusy(false);
    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
        return;
    }
    m_filesystemInfo = result.filesystemInfo;
    m_rekordboxStats = result.rekordboxStats;
    m_engineStats = result.engineStats;
    m_oneLibraryStats = result.oneLibraryStats;
    m_diskUsage = result.diskUsage;
    m_benchmarkSamples = result.benchmarkSamples;
    emit resultsChanged();
    loadBenchmarkHistory();
}

void StickStatisticsController::runBenchmark()
{
    if (m_benchmarkRunning || m_filesystemInfo.isEmpty()) {
        return;
    }
    setBenchmarkErrorMessage({});
    setBenchmarkRunning(true);
    m_benchmarkWatcher.setFuture(
        QtConcurrent::run(runBenchmarkTask, m_benchmarkSamples, m_filesystemInfo, m_filesystemInfo.value("stickIdentifier").toString()));
}

void StickStatisticsController::onBenchmarkFinished()
{
    QVariantMap result = m_benchmarkWatcher.result();
    setBenchmarkRunning(false);
    if (!result.value("success").toBool()) {
        setBenchmarkErrorMessage(result.value("errorMessage").toString());
        return;
    }
    loadBenchmarkHistory();
}

void StickStatisticsController::loadBenchmarkHistory()
{
    QString stickIdentifier = m_filesystemInfo.value("stickIdentifier").toString();
    if (stickIdentifier.isEmpty()) {
        m_benchmarkHistory = {};
        emit benchmarkHistoryChanged();
        return;
    }
    try {
        infrastructure::benchmark::StickBenchmarkHistory history;
        auto records = history.historyFor(stickIdentifier.toStdString());
        QVariantList list;
        for (const auto &r : records) {
            list << toVariant(r);
        }
        m_benchmarkHistory = list;
    } catch (const std::exception &e) {
        setBenchmarkErrorMessage(QString::fromStdString(e.what()));
    }
    emit benchmarkHistoryChanged();
}

void StickStatisticsController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void StickStatisticsController::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

void StickStatisticsController::setBenchmarkRunning(bool running)
{
    if (m_benchmarkRunning == running) {
        return;
    }
    m_benchmarkRunning = running;
    emit benchmarkRunningChanged();
}

void StickStatisticsController::setBenchmarkErrorMessage(const QString &message)
{
    if (m_benchmarkErrorMessage == message) {
        return;
    }
    m_benchmarkErrorMessage = message;
    emit benchmarkErrorMessageChanged();
}

}  // namespace seabass::gui
