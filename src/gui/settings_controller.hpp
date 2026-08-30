#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QVariantList>

namespace seabass::gui
{

// Result of a background task -- see SettingsController::load()/setField().
// Built entirely on a worker thread, with no access to the controller.
struct SettingsTaskResult
{
    QVariantList groups;
    QString errorMessage;  // empty on success
    QString statusMessage;
};

// Wraps readDeviceSettings() for QML: decodes whichever of rekordbox's
// player/mixer settings files are present on a stick, exposed as
// `groups` -- a list of { title, fileName, fields: [{label, value}] }.
// Both loading and writing are disk I/O, so (like every other
// write-capable controller) they run on a background thread via
// QtConcurrent rather than ever blocking the UI thread.
class SettingsController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList groups READ groups NOTIFY groupsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit SettingsController(QObject *parent = nullptr);

    QVariantList groups() const { return m_groups; }
    bool busy() const { return m_busy; }
    QString errorMessage() const { return m_errorMessage; }
    QString statusMessage() const { return m_statusMessage; }

    // pioneerRoot is the DetectedStick.rekordboxPath.
    Q_INVOKABLE void load(const QString &pioneerRoot);

    // Sets one field to one of its known options (see `options` on each
    // field in `groups`), backing up the settings file first. Refuses
    // (sets errorMessage, changes nothing) for any field/option Seabass
    // doesn't fully recognize.
    Q_INVOKABLE void setField(const QString &fileName, const QString &fieldLabel, const QString &optionName);

signals:
    void groupsChanged();
    void busyChanged();
    void errorMessageChanged();
    void statusMessageChanged();

private:
    void onTaskFinished();
    void setBusy(bool busy);
    void setErrorMessage(const QString &message);
    void setStatusMessage(const QString &message);

    QFutureWatcher<SettingsTaskResult> m_watcher;
    QString m_pioneerRoot;
    QVariantList m_groups;
    bool m_busy = false;
    QString m_errorMessage;
    QString m_statusMessage;
};

}  // namespace seabass::gui
