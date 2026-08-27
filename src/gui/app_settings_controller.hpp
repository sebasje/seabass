#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QSettings>

namespace djconvert::gui
{

// App-level (not per-stick) preferences, persisted via QSettings under the
// user's standard config location. Currently just the theme choice --
// Main.qml binds Material.theme to useSystemTheme.
class AppSettingsController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool useSystemTheme READ useSystemTheme WRITE setUseSystemTheme NOTIFY useSystemThemeChanged)

public:
    explicit AppSettingsController(QObject *parent = nullptr);

    bool useSystemTheme() const { return m_useSystemTheme; }
    void setUseSystemTheme(bool value);

signals:
    void useSystemThemeChanged();

private:
    QSettings m_settings;
    bool m_useSystemTheme = false;
};

}  // namespace djconvert::gui
