#include "infrastructure/paths/seabass_paths.hpp"
#include "app_settings_controller.hpp"

#include <QDir>
#include <QStandardPaths>

namespace seabass::gui
{

QString AppSettingsController::defaultStickBackupDirectory()
{
    return QString::fromStdString(infrastructure::paths::localFullBackupsDir().string());
}

AppSettingsController::AppSettingsController(QObject *parent)
    : QObject(parent), m_settings("seabass", "seabass")
{
    m_useSystemTheme = m_settings.value("useSystemTheme", false).toBool();
    m_preferredFormat = m_settings.value("preferredFormat", "rekordbox").toString();
    m_hideStreamingTracks = m_settings.value("hideStreamingTracks", false).toBool();
    m_keyNotation = m_settings.value("keyNotation", "camelot").toString();
    m_stickBackupDirectory = m_settings.value("stickBackupDirectory", defaultStickBackupDirectory()).toString();
    if (m_stickBackupDirectory.isEmpty()) {
        m_stickBackupDirectory = defaultStickBackupDirectory();
    }
    m_lastBrowsePlaylistName = m_settings.value("lastBrowsePlaylistName", "").toString();
#ifdef SEABASS_EXPERIMENTAL_BUILD
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

void AppSettingsController::setKeyNotation(const QString &value)
{
    if (m_keyNotation == value) {
        return;
    }
    m_keyNotation = value;
    m_settings.setValue("keyNotation", value);
    emit keyNotationChanged();
}

void AppSettingsController::setStickBackupDirectory(const QString &value)
{
    QString effective = value.isEmpty() ? defaultStickBackupDirectory() : value;
    if (m_stickBackupDirectory == effective) {
        return;
    }
    m_stickBackupDirectory = effective;
    m_settings.setValue("stickBackupDirectory", effective);
    emit stickBackupDirectoryChanged();
}

void AppSettingsController::setLastBrowsePlaylistName(const QString &value)
{
    if (m_lastBrowsePlaylistName == value) {
        return;
    }
    m_lastBrowsePlaylistName = value;
    m_settings.setValue("lastBrowsePlaylistName", value);
    emit lastBrowsePlaylistNameChanged();
}

#ifdef SEABASS_EXPERIMENTAL_BUILD
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

}  // namespace seabass::gui
