#include "app_settings_controller.hpp"

namespace djconvert::gui
{

AppSettingsController::AppSettingsController(QObject *parent)
    : QObject(parent), m_settings("djconvert", "djconvert-gui")
{
    m_useSystemTheme = m_settings.value("useSystemTheme", false).toBool();
}

void AppSettingsController::setUseSystemTheme(bool value)
{
    if (m_useSystemTheme == value) {
        return;
    }
    m_useSystemTheme = value;
    m_settings.setValue("useSystemTheme", value);
    emit useSystemThemeChanged();
}

}  // namespace djconvert::gui
