#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QSettings>

namespace djconvert::gui
{

// App-level (not per-stick) preferences, persisted via QSettings under the
// user's standard config location. Main.qml binds Material.theme to
// useSystemTheme; ScanPage/DuplicatesPage/LocalCuePage bind their
// Rekordbox/Engine mode toggle to preferredFormat, so the last-chosen
// format carries over between those pages and across app restarts.
class AppSettingsController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool useSystemTheme READ useSystemTheme WRITE setUseSystemTheme NOTIFY useSystemThemeChanged)
    Q_PROPERTY(QString preferredFormat READ preferredFormat WRITE setPreferredFormat NOTIFY preferredFormatChanged)

public:
    explicit AppSettingsController(QObject *parent = nullptr);

    bool useSystemTheme() const { return m_useSystemTheme; }
    void setUseSystemTheme(bool value);

    QString preferredFormat() const { return m_preferredFormat; }
    void setPreferredFormat(const QString &value);

signals:
    void useSystemThemeChanged();
    void preferredFormatChanged();

private:
    QSettings m_settings;
    bool m_useSystemTheme = false;
    QString m_preferredFormat = QStringLiteral("rekordbox");
};

}  // namespace djconvert::gui
