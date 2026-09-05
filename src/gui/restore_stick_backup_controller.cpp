#include "restore_stick_backup_controller.hpp"

#include <QDateTime>
#include <QDir>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

#include <chrono>
#include <filesystem>

#include "gui/write_guard.hpp"
#include "infrastructure/engine/engine_restore_check.hpp"
#include "infrastructure/media/media_factory.hpp"
#include "infrastructure/stick_backup/backup_manifest.hpp"

namespace seabass::gui
{

namespace fs = std::filesystem;
using application::DetectedStick;
using application::RestoreOptions;
using application::RestorePreview;
using application::RestoreProgress;
using application::RestoreStickBackup;
using application::RestoreSummary;

namespace
{

QVariantMap diskToVariant(const DetectedStick &disk)
{
    QVariantMap map;
    map["label"] = QString::fromStdString(disk.label);
    map["mountPoint"] = QString::fromStdString(disk.mountPoint);
    map["devicePath"] = QString::fromStdString(disk.devicePath);
    map["wholeDiskPath"] = QString::fromStdString(disk.wholeDiskPath);
    map["capacityBytes"] = static_cast<qlonglong>(disk.capacityBytes);
    map["mounted"] = disk.mounted;
    map["hasNoFilesystem"] = disk.hasNoFilesystem;
    map["hasDjLibrary"] = disk.rekordboxPath.has_value() || disk.enginePath.has_value();
    map["usable"] = disk.mounted && !disk.hasNoFilesystem && !disk.mountPoint.empty();
    QVariantList rootEntries;
    for (const auto &entry : disk.rootEntries) {
        rootEntries.push_back(QString::fromStdString(entry));
    }
    map["rootEntries"] = rootEntries;
    return map;
}

QString phaseName(RestoreProgress::Phase phase)
{
    switch (phase) {
    case RestoreProgress::Phase::Analyzing: return QStringLiteral("analyzing");
    case RestoreProgress::Phase::Writing: return QStringLiteral("writing");
    case RestoreProgress::Phase::Removing: return QStringLiteral("removing");
    case RestoreProgress::Phase::Checking: return QStringLiteral("checking");
    }
    return {};
}

QVariantList toVariantList(const std::vector<std::string> &items)
{
    QVariantList list;
    for (const std::string &item : items) {
        list.push_back(QString::fromStdString(item));
    }
    return list;
}

}  // namespace

struct RestoreStickBackupController::AnalyzeResult
{
    RestorePreview preview;
    bool targetGiven = false;
};

struct RestoreStickBackupController::RestoreResult
{
    QString refusal;
    RestoreSummary summary;
};

RestoreStickBackupController::RestoreStickBackupController(QObject *parent) : QObject(parent)
{
    connect(&m_analyzeWatcher, &QFutureWatcher<std::shared_ptr<AnalyzeResult>>::finished, this,
            &RestoreStickBackupController::onAnalyzeFinished);
    connect(&m_restoreWatcher, &QFutureWatcher<std::shared_ptr<RestoreResult>>::finished, this,
            &RestoreStickBackupController::onRestoreFinished);
    refresh();
}

RestoreStickBackupController::~RestoreStickBackupController()
{
    m_cancel.cancel();
    m_restoreWatcher.waitForFinished();
    m_analyzeWatcher.waitForFinished();
}

void RestoreStickBackupController::refresh()
{
    auto locator = infrastructure::media::createRemovableMediaLocator();
    QVariantList disks;
    for (const auto &disk : locator->detect()) {
        disks.push_back(diskToVariant(disk));
    }
    m_disks = std::move(disks);
    emit disksChanged();
}

void RestoreStickBackupController::setArchivePath(const QString &path)
{
    QString cleaned = path;
    if (cleaned.startsWith(QStringLiteral("file://"))) {
        cleaned = QUrl(cleaned).toLocalFile();
    }
    if (m_archivePath == cleaned) {
        return;
    }
    m_archivePath = cleaned;
    m_archiveInfo.clear();
    m_preview.clear();
    m_result.clear();
    emit archivePathChanged();
    emit archiveInfoChanged();
    emit previewChanged();
    emit resultChanged();
}

void RestoreStickBackupController::setDefaultBackupDirectory(const QString &directory)
{
    if (m_defaultBackupDirectory == directory) {
        return;
    }
    m_defaultBackupDirectory = directory;
    emit defaultBackupDirectoryChanged();
}

QString RestoreStickBackupController::archivePathForLabel(const QString &label) const
{
    QString name = label.trimmed();
    for (QChar &c : name) {
        if (QStringLiteral("/\\:*?\"<>|").contains(c) || c.unicode() < 0x20) {
            c = QLatin1Char('_');
        }
    }
    if (name.isEmpty()) {
        name = QStringLiteral("stick");
    }
    return QDir(m_defaultBackupDirectory).filePath(name + QStringLiteral(".zip"));
}

void RestoreStickBackupController::analyze(const QString &targetRoot)
{
    if (m_archivePath.isEmpty() || m_analyzeWatcher.isRunning() || m_restoring) {
        return;
    }
    m_analyzing = true;
    emit busyChanged();
    RestoreOptions options;
    options.archivePath = fs::path(m_archivePath.toStdString());
    options.targetRoot = fs::path(targetRoot.toStdString());
    bool targetGiven = !targetRoot.isEmpty();
    m_analyzeWatcher.setFuture(QtConcurrent::run([options, targetGiven]() {
        auto result = std::make_shared<AnalyzeResult>();
        result->targetGiven = targetGiven;
        result->preview = RestoreStickBackup::preview(options);
        return result;
    }));
}

void RestoreStickBackupController::onAnalyzeFinished()
{
    std::shared_ptr<AnalyzeResult> result = m_analyzeWatcher.result();
    m_analyzing = false;
    if (result) {
        const RestorePreview &p = result->preview;
        QVariantMap info;
        info["error"] = QString::fromStdString(p.error);
        info["label"] = QString::fromStdString(p.stickLabel);
        info["identifier"] = QString::fromStdString(p.stickIdentifier);
        info["status"] = QString::fromUtf8(std::string(infrastructure::stick_backup::toString(p.status)).c_str());
        info["createdAt"] = QDateTime::fromSecsSinceEpoch(p.createdAtUnix).toString(Qt::ISODate);
        info["entries"] = static_cast<qlonglong>(p.entries);
        info["bytes"] = static_cast<qlonglong>(p.bytes);
        info["rejectedCount"] = static_cast<qlonglong>(p.rejected.size());
        m_archiveInfo = info;
        QVariantMap preview;
        if (result->targetGiven && p.error.empty()) {
            preview["filesToWrite"] = static_cast<qlonglong>(p.filesToWrite);
            preview["filesUnchanged"] = static_cast<qlonglong>(p.filesUnchanged);
            preview["bytesToWrite"] = static_cast<qlonglong>(p.bytesToWrite);
            preview["extras"] = static_cast<qlonglong>(p.extras);
            preview["targetHasEngineLibrary"] = p.targetHasEngineLibrary;
            preview["freeBytes"] = static_cast<qlonglong>(p.freeBytesAtTarget);
            preview["enoughFreeSpace"] = p.enoughFreeSpace;
        }
        m_preview = preview;
        if (!p.error.empty()) {
            setErrorMessage(QString::fromStdString(p.error));
        } else {
            setErrorMessage({});
        }
    }
    emit archiveInfoChanged();
    emit previewChanged();
    emit busyChanged();
}

void RestoreStickBackupController::applyProgress(const RestoreProgress &progress)
{
    m_phase = phaseName(progress.phase);
    m_filesDone = static_cast<qlonglong>(progress.filesDone);
    m_filesTotal = static_cast<qlonglong>(progress.filesTotal);
    m_bytesDone = static_cast<qlonglong>(progress.bytesDone);
    m_bytesTotal = static_cast<qlonglong>(progress.bytesTotal);
    m_currentFile = QString::fromStdString(progress.currentFile);
    emit progressChanged();
}

void RestoreStickBackupController::restore(const QString &targetRoot, bool exact)
{
    if (busy() || m_archivePath.isEmpty() || targetRoot.isEmpty()) {
        emit actionFeedback(QStringLiteral("Still busy -- try again once the current operation finishes."), true);
        return;
    }
    setErrorMessage({});
    setStatusMessage({});
    m_result.clear();
    emit resultChanged();
    m_restoring = true;
    emit busyChanged();
    m_phase.clear();
    m_filesDone = m_filesTotal = m_bytesDone = m_bytesTotal = 0;
    m_currentFile.clear();
    emit progressChanged();
    m_cancel = application::CancellationToken();
    RestoreOptions options;
    options.archivePath = fs::path(m_archivePath.toStdString());
    options.targetRoot = fs::path(targetRoot.toStdString());
    options.exact = exact;
    options.cancel = m_cancel;
    options.libraryCheck = infrastructure::engine::checkRestoredEngineLibrary;
    QPointer<RestoreStickBackupController> self(this);
    auto lastPost = std::make_shared<std::chrono::steady_clock::time_point>();
    options.onProgress = [self, lastPost](const RestoreProgress &progress) {
        auto now = std::chrono::steady_clock::now();
        bool edge = progress.bytesDone == 0 || progress.bytesDone == progress.bytesTotal;
        if (!edge && now - *lastPost < std::chrono::milliseconds(100)) {
            return;
        }
        *lastPost = now;
        RestoreProgress copy = progress;
        QMetaObject::invokeMethod(
            self, [self, copy] {
                if (self) {
                    self->applyProgress(copy);
                }
            },
            Qt::QueuedConnection);
    };
    m_restoreWatcher.setFuture(QtConcurrent::run([options]() {
        auto result = std::make_shared<RestoreResult>();
        result->refusal = refuseIfDjSoftwareRunning();
        if (!result->refusal.isEmpty()) {
            return result;
        }
        result->summary = RestoreStickBackup::execute(options);
        return result;
    }));
}

void RestoreStickBackupController::cancel()
{
    m_cancel.cancel();
}

void RestoreStickBackupController::onRestoreFinished()
{
    std::shared_ptr<RestoreResult> result = m_restoreWatcher.result();
    m_restoring = false;
    emit busyChanged();
    if (!result) {
        return;
    }
    if (!result->refusal.isEmpty()) {
        setErrorMessage(result->refusal);
        emit actionFeedback(result->refusal, true);
        return;
    }
    const RestoreSummary &s = result->summary;
    QVariantMap map;
    map["filesWritten"] = static_cast<qlonglong>(s.filesWritten);
    map["filesUnchanged"] = static_cast<qlonglong>(s.filesUnchanged);
    map["directoriesCreated"] = static_cast<qlonglong>(s.directoriesCreated);
    map["extrasRemoved"] = static_cast<qlonglong>(s.extrasRemoved);
    map["bytesWritten"] = static_cast<qlonglong>(s.bytesWritten);
    QVariantList rejected;
    for (const auto &[name, reason] : s.rejected) {
        rejected.push_back(QString::fromStdString(name + " -- " + reason));
    }
    map["rejected"] = rejected;
    map["writeErrors"] = toVariantList(s.writeErrors);
    map["warnings"] = toVariantList(s.warnings);
    map["missingTracks"] = s.missingTrackPaths ? toVariantList(*s.missingTrackPaths) : QVariantList{};
    map["databaseChecked"] = s.missingTrackPaths.has_value();
    m_result = map;
    emit resultChanged();

    switch (s.status) {
    case RestoreSummary::Status::Restored:
        setStatusMessage(QStringLiteral("Restored %1 files (%2 already up to date).").arg(s.filesWritten).arg(s.filesUnchanged));
        emit actionFeedback(m_statusMessage, false);
        break;
    case RestoreSummary::Status::RestoredWithProblems:
        setStatusMessage(QStringLiteral("Restored %1 files, but with problems -- see the report below.").arg(s.filesWritten));
        emit actionFeedback(m_statusMessage, true);
        break;
    case RestoreSummary::Status::Cancelled:
        setStatusMessage(QString::fromStdString(s.message));
        break;
    case RestoreSummary::Status::Failed:
        setErrorMessage(QString::fromStdString(s.message));
        emit actionFeedback(m_errorMessage, true);
        break;
    }
}

void RestoreStickBackupController::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

void RestoreStickBackupController::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) {
        return;
    }
    m_statusMessage = message;
    emit statusMessageChanged();
}

}  // namespace seabass::gui
