#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QSettings>

namespace seabass::gui
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
    Q_PROPERTY(QString keyNotation READ keyNotation WRITE setKeyNotation NOTIFY keyNotationChanged)
    // Always present (even in a build compiled with SEABASS_EXPERIMENTAL
    // off) so QML can gate the whole Settings section on it.
    Q_PROPERTY(bool experimentalBuildSupported READ experimentalBuildSupported CONSTANT)
#ifdef SEABASS_EXPERIMENTAL_BUILD
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

    // How KeyBadge.qml renders a musical key everywhere it appears
    // (Browse Library's Key column, Library Statistics' "Tracks per
    // key") -- "camelot" (default, e.g. "6A") or "traditional" (e.g.
    // "F♯m"). Either way the badge's color still comes from the same
    // underlying Camelot wheel position (see Theme.colorForKey()), only
    // the printed label changes.
    QString keyNotation() const { return m_keyNotation; }
    void setKeyNotation(const QString &value);

    // See docs/experimental-features.md for the convention this backs:
    // new non-trivial features default to hidden behind
    // experimentalFeaturesEnabled until proven, then graduate to
    // stable (an ActionCard just drops its `experimental: true`).
    static constexpr bool experimentalBuildSupported()
    {
#ifdef SEABASS_EXPERIMENTAL_BUILD
        return true;
#else
        return false;
#endif
    }

#ifdef SEABASS_EXPERIMENTAL_BUILD
    bool experimentalFeaturesEnabled() const { return m_experimentalFeaturesEnabled; }
    void setExperimentalFeaturesEnabled(bool value);
#else
    bool experimentalFeaturesEnabled() const { return false; }
#endif

signals:
    void useSystemThemeChanged();
    void preferredFormatChanged();
    void hideStreamingTracksChanged();
    void keyNotationChanged();
#ifdef SEABASS_EXPERIMENTAL_BUILD
    void experimentalFeaturesEnabledChanged();
#endif

private:
    QSettings m_settings;
    bool m_useSystemTheme = false;
    QString m_preferredFormat = QStringLiteral("rekordbox");
    bool m_hideStreamingTracks = false;
    QString m_keyNotation = QStringLiteral("camelot");
#ifdef SEABASS_EXPERIMENTAL_BUILD
    bool m_experimentalFeaturesEnabled = false;
#endif
};

}  // namespace seabass::gui
