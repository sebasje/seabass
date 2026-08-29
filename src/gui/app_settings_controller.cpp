#include "app_settings_controller.hpp"

namespace djconvert::gui
{

AppSettingsController::AppSettingsController(QObject *parent)
    : QObject(parent), m_settings("djconvert", "djconvert-gui")
{
    m_useSystemTheme = m_settings.value("useSystemTheme", false).toBool();
    m_preferredFormat = m_settings.value("preferredFormat", "rekordbox").toString();
    m_hideStreamingTracks = m_settings.value("hideStreamingTracks", false).toBool();
#ifdef DJCONVERT_EXPERIMENTAL_BUILD
    m_experimentalFeaturesEnabled = m_settings.value("experimentalFeaturesEnabled", false).toBool();
#endif
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

void AppSettingsController::setPreferredFormat(const QString &value)
{
    if (m_preferredFormat == value) {
        return;
    }
    m_preferredFormat = value;
    m_settings.setValue("preferredFormat", value);
    emit preferredFormatChanged();
}

void AppSettingsController::setHideStreamingTracks(bool value)
{
    if (m_hideStreamingTracks == value) {
        return;
    }
    m_hideStreamingTracks = value;
    m_settings.setValue("hideStreamingTracks", value);
    emit hideStreamingTracksChanged();
}

#ifdef DJCONVERT_EXPERIMENTAL_BUILD
void AppSettingsController::setExperimentalFeaturesEnabled(bool value)
{
    if (m_experimentalFeaturesEnabled == value) {
        return;
    }
    m_experimentalFeaturesEnabled = value;
    m_settings.setValue("experimentalFeaturesEnabled", value);
    emit experimentalFeaturesEnabledChanged();
}
#endif

}  // namespace djconvert::gui
