#include "settings_controller.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include <filesystem>

#include <QVariantMap>

#include "gui/write_guard.hpp"
#include "infrastructure/backup/filesystem_backup_store.hpp"
#include "infrastructure/backup/stick_write_lock.hpp"
#include "infrastructure/rekordbox/rekordbox_settings_fields.hpp"
#include "infrastructure/rekordbox/rekordbox_settings_reader.hpp"
#include "infrastructure/rekordbox/rekordbox_settings_writer.hpp"

namespace seabass::gui
{

namespace fs = std::filesystem;

namespace
{

// Decodes every recognized settings file on the stick into `groups`.
// *errorMessage is set (and groups left empty) on failure, including the
// "nothing recognized" case -- mirrors the previous synchronous load()'s
// exact wording.
QVariantList buildGroups(const QString &pioneerRoot, QString *errorMessage)
{
    QVariantList groups;
    try {
        auto files = infrastructure::rekordbox::readDeviceSettings(pioneerRoot.toStdString());
        for (const auto &file : files) {
            QVariantMap group;
            group["title"] = QString::fromStdString(file.title);
            group["fileName"] = QString::fromStdString(file.fileName);

            QVariantList fields;
            for (const auto &[label, value] : file.fields) {
                QVariantMap fieldMap;
                fieldMap["label"] = QString::fromStdString(label);
                fieldMap["value"] = QString::fromStdString(value);

                QVariantList options;
                for (const auto &field : infrastructure::rekordbox::allSettingsFields()) {
                    if (field.fileName == file.fileName && field.label == label) {
                        for (const auto &option : field.options) {
                            options << QString::fromStdString(option.name);
                        }
                        break;
                    }
                }
                fieldMap["options"] = options;

                fields << fieldMap;
            }
            group["fields"] = fields;

            groups << group;
        }
        if (groups.isEmpty()) {
            *errorMessage = "No recognized settings files found on this stick.";
        }
    } catch (const std::exception &e) {
        *errorMessage = QString::fromStdString(e.what());
    }
    return groups;
}

// Runs entirely on a background thread (see SettingsController::load()) --
// no access to the controller itself.
SettingsTaskResult runLoadTask(QString pioneerRoot)
{
    SettingsTaskResult result;
    result.groups = buildGroups(pioneerRoot, &result.errorMessage);
    return result;
}

// Runs entirely on a background thread (see SettingsController::
// setField()). Writes the one field, then re-decodes every group so the
// caller always gets a fresh, consistent view -- same as the old
// synchronous setField()'s trailing load() call.
SettingsTaskResult runSetFieldTask(QString pioneerRoot, QString fileName, QString fieldLabel, QString optionName)
{
    SettingsTaskResult result;
    QString refusal = refuseIfRekordboxRunning();
    if (!refusal.isEmpty()) {
        QString ignoredLoadError;
        result.groups = buildGroups(pioneerRoot, &ignoredLoadError);
        result.errorMessage = refusal;  // takes priority over ignoredLoadError
        return result;
    }
    try {
        std::string filePath = pioneerRoot.toStdString() + "/" + fileName.toStdString();
        if (!fs::exists(filePath)) {
            result.errorMessage = "Settings file not found: " + fileName;
        } else {
            // Same backup-before-write invariant as every other write path
            // in the app, sharing the same .seabass-backups directory.
            fs::path stickRoot = fs::path(pioneerRoot.toStdString()).parent_path();
            infrastructure::backup::StickWriteLock lock((stickRoot / ".seabass-backups" / ".write.lock").string());
            infrastructure::backup::FilesystemBackupStore backupStore((stickRoot / ".seabass-backups").string());
            backupStore.backup({filePath}, "device-settings");

            bool ok = infrastructure::rekordbox::writeDeviceSettingField(
                pioneerRoot.toStdString(), fileName.toStdString(), fieldLabel.toStdString(), optionName.toStdString());
            if (ok) {
                result.statusMessage = "Saved -- the previous file is backed up.";
            } else {
                result.errorMessage = "Could not save that setting -- the file wasn't in the expected format.";
            }
        }
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }

    QString loadError;
    result.groups = buildGroups(pioneerRoot, &loadError);
    if (result.errorMessage.isEmpty()) {
        result.errorMessage = loadError;
    }
    return result;
}

}  // namespace

SettingsController::SettingsController(QObject *parent) : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<SettingsTaskResult>::finished, this, &SettingsController::onTaskFinished);
}

void SettingsController::load(const QString &pioneerRoot)
{
    if (m_busy) {
        return;
    }
    m_pioneerRoot = pioneerRoot;
    setErrorMessage({});
    setBusy(true);
    m_watcher.setFuture(QtConcurrent::run(runLoadTask, pioneerRoot));
}

void SettingsController::setField(const QString &fileName, const QString &fieldLabel, const QString &optionName)
{
    if (m_busy) {
        return;
    }
    setErrorMessage({});
    setStatusMessage({});
    setBusy(true);
    m_watcher.setFuture(QtConcurrent::run(runSetFieldTask, m_pioneerRoot, fileName, fieldLabel, optionName));
}

void SettingsController::onTaskFinished()
{
    SettingsTaskResult result = m_watcher.result();
    m_groups = result.groups;
    emit groupsChanged();
    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
    }
    if (!result.statusMessage.isEmpty()) {
        setStatusMessage(result.statusMessage);
    }
    setBusy(false);
}

void SettingsController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void SettingsController::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

void SettingsController::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) {
        return;
    }
    m_statusMessage = message;
    emit statusMessageChanged();
}

}  // namespace seabass::gui
