#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>

namespace djconvert::gui
{

// Wraps readDeviceSettings() for QML: decodes whichever of rekordbox's
// player/mixer settings files are present on a stick, exposed as
// `groups` -- a list of { title, fileName, fields: [{label, value}] }.
class SettingsController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList groups READ groups NOTIFY groupsChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    explicit SettingsController(QObject *parent = nullptr);

    QVariantList groups() const { return m_groups; }
    QString errorMessage() const { return m_errorMessage; }

    // pioneerRoot is the DetectedStick.rekordboxPath.
    Q_INVOKABLE void load(const QString &pioneerRoot);

signals:
    void groupsChanged();
    void errorMessageChanged();

private:
    void setErrorMessage(const QString &message);

    QVariantList m_groups;
    QString m_errorMessage;
};

}  // namespace djconvert::gui
