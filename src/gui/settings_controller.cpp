#include "settings_controller.hpp"

#include <QVariantMap>

#include "infrastructure/rekordbox/rekordbox_settings_reader.hpp"

namespace djconvert::gui
{

SettingsController::SettingsController(QObject *parent) : QObject(parent) {}

void SettingsController::load(const QString &pioneerRoot)
{
    setErrorMessage({});

    try {
        auto files = infrastructure::rekordbox::readDeviceSettings(pioneerRoot.toStdString());

        QVariantList groups;
        for (const auto &file : files) {
            QVariantMap group;
            group["title"] = QString::fromStdString(file.title);
            group["fileName"] = QString::fromStdString(file.fileName);

            QVariantList fields;
            for (const auto &[label, value] : file.fields) {
                QVariantMap fieldMap;
                fieldMap["label"] = QString::fromStdString(label);
                fieldMap["value"] = QString::fromStdString(value);
                fields << fieldMap;
            }
            group["fields"] = fields;

            groups << group;
        }
        m_groups = groups;

        if (groups.isEmpty()) {
            setErrorMessage("No recognized settings files found on this stick.");
        }
    } catch (const std::exception &e) {
        setErrorMessage(QString::fromStdString(e.what()));
    }

    emit groupsChanged();
}

void SettingsController::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

}  // namespace djconvert::gui
