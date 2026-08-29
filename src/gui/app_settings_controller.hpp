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
    Q_PROPERTY(bool hideStreamingTracks READ hideStreamingTracks WRITE setHideStreamingTracks NOTIFY
                   hideStreamingTracksChanged)
    // Always present (even in a build compiled with DJCONVERT_EXPERIMENTAL
    // off) so QML can gate the whole Settings section on it.
    Q_PROPERTY(bool experimentalBuildSupported READ experimentalBuildSupported CONSTANT)
#ifdef DJCONVERT_EXPERIMENTAL_BUILD
    Q_PROPERTY(bool experimentalFeaturesEnabled READ experimentalFeaturesEnabled WRITE setExperimentalFeaturesEnabled
                   NOTIFY experimentalFeaturesEnabledChanged)
#else
    // No WRITE, on purpose: a stable-only build has nothing to toggle,
    // and the getter below is a hardcoded false regardless of anything
    // stored in QSettings from a previous experimental build.
    Q_PROPERTY(bool experimentalFeaturesEnabled READ experimentalFeaturesEnabled CONSTANT)
#endif

public:
    explicit AppSettingsController(QObject *parent = nullptr);

    bool useSystemTheme() const { return m_useSystemTheme; }
    void setUseSystemTheme(bool value);

    QString preferredFormat() const { return m_preferredFormat; }
    void setPreferredFormat(const QString &value);

    // Browse Library's own display preference. Streaming tracks
    // (Engine/TIDAL) are excluded from destructive/consequential
    // operations unconditionally regardless of this setting (see
    // domain::Track::streamingSource); this only controls whether they
    // show up at all. Off by default. Tracks are shown (badged) unless
    // the user opts into hiding them.
    bool hideStreamingTracks() const { return m_hideStreamingTracks; }
    void setHideStreamingTracks(bool value);

    // See docs/experimental-features.md for the convention this backs:
    // new non-trivial features default to hidden behind
    // experimentalFeaturesEnabled until proven, then graduate to
    // stable (an ActionCard just drops its `experimental: true`).
    static constexpr bool experimentalBuildSupported()
    {
#ifdef DJCONVERT_EXPERIMENTAL_BUILD
        return true;
#else
        return false;
#endif
    }

#ifdef DJCONVERT_EXPERIMENTAL_BUILD
    bool experimentalFeaturesEnabled() const { return m_experimentalFeaturesEnabled; }
    void setExperimentalFeaturesEnabled(bool value);
#else
    bool experimentalFeaturesEnabled() const { return false; }
#endif

signals:
    void useSystemThemeChanged();
    void preferredFormatChanged();
    void hideStreamingTracksChanged();
#ifdef DJCONVERT_EXPERIMENTAL_BUILD
    void experimentalFeaturesEnabledChanged();
#endif

private:
    QSettings m_settings;
    bool m_useSystemTheme = false;
    QString m_preferredFormat = QStringLiteral("rekordbox");
    bool m_hideStreamingTracks = false;
#ifdef DJCONVERT_EXPERIMENTAL_BUILD
    bool m_experimentalFeaturesEnabled = false;
#endif
};

}  // namespace djconvert::gui
