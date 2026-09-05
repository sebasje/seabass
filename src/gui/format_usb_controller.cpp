#include "format_usb_controller.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include "application/ports/progress_reporter.hpp"
#include "application/use_cases/format_usb_stick.hpp"
#include "domain/usb_filesystem.hpp"
#include "gui/write_guard.hpp"
#include "infrastructure/media/media_factory.hpp"

namespace seabass::gui
{

using application::DetectedStick;
using application::NullProgressReporter;
using application::RemovableMediaLocator;
using application::RemovableMediaMounter;
using application::UsbFormatter;
using domain::UsbFilesystem;

namespace
{

QString filesystemToString(UsbFilesystem fs)
{
    return fs == UsbFilesystem::Fat32 ? QStringLiteral("fat32") : QStringLiteral("exfat");
}

UsbFilesystem filesystemFromString(const QString &s)
{
    return s == QStringLiteral("fat32") ? UsbFilesystem::Fat32 : UsbFilesystem::ExFat;
}

QVariantMap diskToVariant(const DetectedStick &disk)
{
    QVariantMap map;
    map["label"] = QString::fromStdString(disk.label);
    map["wholeDiskPath"] = QString::fromStdString(disk.wholeDiskPath);
    map["devicePath"] = QString::fromStdString(disk.devicePath);
    map["capacityBytes"] = static_cast<qlonglong>(disk.capacityBytes);
    map["mounted"] = disk.mounted;
    map["hasNoFilesystem"] = disk.hasNoFilesystem;
    map["hasDjLibrary"] = disk.rekordboxPath.has_value() || disk.enginePath.has_value();
    QVariantList rootEntries;
    for (const auto &entry : disk.rootEntries) {
        rootEntries.push_back(QString::fromStdString(entry));
    }
    map["rootEntries"] = rootEntries;
    return map;
}

// Runs entirely on a background thread (see FormatUsbController::format())
// -- no access to the controller itself. Owns its own locator/mounter/
// formatter instances rather than sharing the app-wide MediaController's,
// same as every other write controller in this codebase constructs its
// own use case dependencies per task.
FormatUsbTaskResult runFormatTask(QString wholeDiskPath, QString filesystem, QString volumeLabel,
                                    std::shared_ptr<QtProgressReporter> reporter)
{
    FormatUsbTaskResult result;
    QString refusal = refuseIfRekordboxRunning();
    if (!refusal.isEmpty()) {
        result.errorMessage = refusal;
        return result;
    }
    try {
        auto locator = infrastructure::media::createRemovableMediaLocator();
        auto mounter = infrastructure::media::createRemovableMediaMounter();
        auto formatter = infrastructure::media::createUsbFormatter();
        application::FormatUsbStick useCase(*locator, *mounter, *formatter);

        std::string errorMessage;
        bool ok = useCase.execute(wholeDiskPath.toStdString(), filesystemFromString(filesystem),
                                   volumeLabel.toStdString(), errorMessage, *reporter);
        if (!ok) {
            result.errorMessage = QString::fromStdString(errorMessage);
        }
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

}  // namespace

FormatUsbController::FormatUsbController(QObject *parent) : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<FormatUsbTaskResult>::finished, this,
            &FormatUsbController::onFormatFinished);

    auto formatter = infrastructure::media::createUsbFormatter();
    if (auto cap = formatter->maxSizeFor(UsbFilesystem::Fat32)) {
        m_fat32MaxBytes = static_cast<qlonglong>(*cap);
    }

    refresh();
}

void FormatUsbController::refresh()
{
    // Whole-disk enumeration (this project's own udev/Get-Disk-backed
    // adapters) is lightweight -- no library scan, just a handful of
    // devices -- so this runs synchronously on the UI thread, same as
    // MediaController's own sticks refresh.
    auto locator = infrastructure::media::createRemovableMediaLocator();
    QVariantList disks;
    for (const auto &disk : locator->detect()) {
        disks.push_back(diskToVariant(disk));
    }
    setDisks(std::move(disks));
}

QString FormatUsbController::recommendedFilesystem(qlonglong capacityBytes) const
{
    return filesystemToString(domain::recommendedUsbFilesystem(static_cast<std::uint64_t>(capacityBytes)));
}

void FormatUsbController::format(const QString &wholeDiskPath, const QString &filesystem, const QString &volumeLabel)
{
    if (m_busy) {
        return;
    }
    setErrorMessage({});
    setStatusMessage({});
    setBusy(true);

    auto reporter = std::make_shared<QtProgressReporter>();
    m_watcher.setFuture(QtConcurrent::run(runFormatTask, wholeDiskPath, filesystem, volumeLabel, reporter));
}

void FormatUsbController::onFormatFinished()
{
    FormatUsbTaskResult result = m_watcher.result();
    setBusy(false);
    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
        return;
    }
    setStatusMessage(QStringLiteral("Drive formatted successfully."));
    refresh();
}

void FormatUsbController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void FormatUsbController::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

void FormatUsbController::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) {
        return;
    }
    m_statusMessage = message;
    emit statusMessageChanged();
}

void FormatUsbController::setDisks(QVariantList disks)
{
    m_disks = std::move(disks);
    emit disksChanged();
}

}  // namespace seabass::gui
