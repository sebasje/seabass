#include "backup_advisor_controller.hpp"

#include <QDateTime>
#include <QtConcurrent/QtConcurrentRun>

#include <filesystem>
#include <set>

#include "application/use_cases/advise_stick_backup.hpp"
#include "domain/library_fingerprint.hpp"
#include "gui/library_catalog_cache.hpp"
#include "infrastructure/stick_backup/sqlite_db_set.hpp"
#include "infrastructure/system/stick_hardware_info.hpp"

namespace seabass::gui
{

namespace fs = std::filesystem;
using application::StickBackupAdvice;
using application::StickBackupAdviceInput;
using application::StickBackupDescription;

struct BackupAdvisorController::Result
{
    QString mountPoint;
    QVariantMap advice;
};

namespace
{

void appendTracks(std::vector<domain::Track> &tracks, const std::string &format, const QString &path, bool &anyRead)
{
    if (path.isEmpty()) {
        return;
    }
    try {
        std::vector<domain::Track> read = LibraryCatalogCache::instance().tracksFor(format, path.toStdString());
        tracks.insert(tracks.end(), read.begin(), read.end());
        anyRead = true;
    } catch (const std::exception &) {
        // Unreadable catalog: the identifier and label still decide.
    }
}

QVariantMap toVariant(const StickBackupAdvice &advice)
{
    QVariantMap map;
    map["state"] = QString::fromUtf8(std::string(application::toString(advice.state)).c_str());
    map["matchedBy"] = QString::fromUtf8(std::string(application::toString(advice.matchedBy)).c_str());
    map["backupPath"] = QString::fromStdString(advice.backupPath.string());
    map["backupLabel"] = QString::fromStdString(advice.backupLabel);
    map["backupCreatedAt"] = advice.backupCreatedAtUnix > 0
                                 ? QDateTime::fromSecsSinceEpoch(advice.backupCreatedAtUnix).toString(Qt::ISODate)
                                 : QString();
    map["trackOverlap"] = advice.trackOverlap;
    map["cueOverlap"] = advice.cueOverlap;
    map["detail"] = QString::fromStdString(advice.detail);
    return map;
}

}  // namespace

BackupAdvisorController::BackupAdvisorController(QObject *parent) : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<std::shared_ptr<Result>>::finished, this, &BackupAdvisorController::onFinished);
}

BackupAdvisorController::~BackupAdvisorController()
{
    m_watcher.waitForFinished();
}

void BackupAdvisorController::setBackupDirectory(const QString &directory)
{
    if (m_backupDirectory == directory) {
        return;
    }
    m_backupDirectory = directory;
    emit backupDirectoryChanged();
    reassessAll();
}

void BackupAdvisorController::assess(const QString &stickLabel, const QString &mountPoint, const QString &rekordboxPath,
                                     const QString &enginePath)
{
    if (mountPoint.isEmpty()) {
        return;
    }
    const Request request{stickLabel, mountPoint, rekordboxPath, enginePath};
    m_known[mountPoint] = request;
    for (Request &queued : m_queue) {
        if (queued.mountPoint == mountPoint) {
            queued = request;
            return;
        }
    }
    m_queue.push_back(request);
    startNext();
}

void BackupAdvisorController::reassessAll()
{
    for (const Request &request : m_known) {
        assess(request.stickLabel, request.mountPoint, request.rekordboxPath, request.enginePath);
    }
}

void BackupAdvisorController::forget(const QString &mountPoint)
{
    m_known.remove(mountPoint);
    if (m_advice.remove(mountPoint) > 0) {
        emit adviceChanged();
    }
}

void BackupAdvisorController::startNext()
{
    if (m_watcher.isRunning() || m_queue.empty()) {
        return;
    }
    const Request request = m_queue.front();
    m_queue.erase(m_queue.begin());
    const fs::path directory(m_backupDirectory.toStdString());
    emit busyChanged();
    m_watcher.setFuture(QtConcurrent::run([request, directory]() {
        auto result = std::make_shared<Result>();
        result->mountPoint = request.mountPoint;

        StickBackupAdviceInput input;
        input.hasLibrary = !request.rekordboxPath.isEmpty() || !request.enginePath.isEmpty();
        input.stickLabel = request.stickLabel.toStdString();
        input.stickIdentifier =
            infrastructure::system::readStickHardwareInfo(request.mountPoint.toStdString(), input.stickLabel).stickIdentifier;
        if (input.hasLibrary) {
            std::vector<domain::Track> tracks;
            bool anyRead = false;
            appendTracks(tracks, "rekordbox", request.rekordboxPath, anyRead);
            appendTracks(tracks, "engine", request.enginePath, anyRead);
            if (anyRead) {
                input.liveFingerprint = domain::fingerprintLibrary(tracks);
            }
        }
        if (!directory.empty()) {
            input.backups = application::RestoreStickBackup::describeAll(directory);
        }
        // The exact "changed since" test: the same databases the backups
        // captured, fingerprinted as they are on the stick right now.
        std::set<std::string> databasePaths;
        for (const StickBackupDescription &backup : input.backups) {
            for (const auto &[path, hex] : backup.databaseFingerprints) {
                databasePaths.insert(path);
            }
        }
        const fs::path root(request.mountPoint.toStdString());
        for (const std::string &path : databasePaths) {
            if (const auto fingerprint = infrastructure::stick_backup::fingerprintDbSet(root / path)) {
                input.liveDatabaseFingerprints[path] = fingerprint->toHex();
            }
        }
        result->advice = toVariant(application::adviseStickBackup(input));
        return result;
    }));
}

void BackupAdvisorController::onFinished()
{
    const std::shared_ptr<Result> result = m_watcher.result();
    if (result) {
        m_advice[result->mountPoint] = result->advice;
        emit adviceChanged();
    }
    emit busyChanged();
    startNext();
}

}  // namespace seabass::gui
