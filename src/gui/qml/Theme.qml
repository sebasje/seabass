pragma Singleton
import QtQuick

// Single source of truth for every color and font size this app's own
// custom-drawn UI uses -- no other .qml file should have a literal
// "#xxxxxx" color or a hardcoded font.pixelSize/font.pointSize. Two
// concerns, kept together because both come down to "stop hardcoding
// values the platform or the user's own settings should own":
//
// Colors: when useSystemTheme is false ("Kelp" -- this app's own always-
// dark theme, named after the seabass palette's "Kelp" swatch), every
// color below comes from the fixed kelp* palette and is never sampled
// from Plasma/Material -- so "dark" always actually means dark,
// regardless of what light theme the desktop might be using elsewhere.
// When useSystemTheme is true, colors are derived from Material's own
// resolved palette (materialBackground/materialForeground/etc., pushed in
// by Main.qml via Binding, since only an Item inside the ApplicationWindow's
// tree can read the Material attached properties correctly) so this app's
// custom Rectangles match Material-styled Controls exactly and follow
// Plasma's light/dark choice.
//
// Fonts: every size is a multiple of the *system* font size
// (Qt.application.font.pointSize, which Qt's KDE platform theme
// integration already sets from Plasma's own font settings) rather than
// an absolute pixel/point number, so changing Plasma's font size setting
// resizes this app's text too.
QtObject {
    id: root

    // ---- Pushed in from Main.qml (see its Binding elements) ----
    property bool useSystemTheme: false
    property color materialBackground: "#121212"
    property color materialForeground: "#e0e0e0"
    property color materialDivider: "#33ffffff"

    // ---- "Kelp" -- this app's own always-dark palette ----
    readonly property color kelpBackground: "#14181c"
    readonly property color kelpSurface: "#1a1f24"
    readonly property color kelpBorder: "#333a40"
    readonly property color kelpBorderSubtle: "#2c3238"
    readonly property color kelpText: "#e8ecef"
    readonly property color kelpAccent: "#3daee9"
    readonly property color kelpPrimary: "#123a52"

    // ---- Resolved semantic surface roles -- read only these ----
    readonly property color background: useSystemTheme ? materialBackground : kelpBackground
    readonly property color surface: useSystemTheme ? Qt.tint(materialBackground, Qt.rgba(materialForeground.r, materialForeground.g, materialForeground.b, 0.03)) : kelpSurface
    readonly property color border: useSystemTheme ? materialDivider : kelpBorder
    readonly property color borderSubtle: border
    readonly property color text: useSystemTheme ? materialForeground : kelpText
    readonly property color textMuted: mix(background, text, 0.55)
    // Our own brand colors ("Current"/"Abyss") -- deliberately fixed
    // regardless of useSystemTheme, unlike the structural colors above.
    readonly property color accent: kelpAccent
    readonly property color primary: kelpPrimary

    // Interactive/row-shading states -- always a solid, opaque blend of
    // `surface` toward `text` (never a translucent overlay: compositing a
    // semi-transparent Rectangle over whatever happens to render beneath
    // it is what produced a visibly wrong tint once before). Blending
    // toward `text` rather than always darkening or always lightening is
    // what makes this correct in both directions: `text` is light in dark
    // themes and dark in light themes, so the blend naturally goes the
    // right way in both without special-casing which theme is active.
    readonly property color rowEven: surface
    readonly property color rowOdd: mix(surface, text, 0.045)
    readonly property color rowHover: mix(surface, text, 0.09)
    readonly property color rowPressed: mix(surface, text, 0.16)
    readonly property color groupBackground: mix(surface, background, 0.5)

    // ---- Status colors -- fixed across both modes (a warning should
    // read as a warning regardless of theme), but still centralized here
    // rather than repeated as literals at every call site. ----
    readonly property color good: "#8fce8f"
    readonly property color info: "#8ab4f8"
    readonly property color danger: "#ff8080"
    readonly property color warnBg: "#4a3510"
    readonly property color warnBorder: "#c99a2e"
    readonly property color warnText: "#f0d080"
    readonly property color warnIcon: "#f0c040"
    readonly property color dangerBg: "#5c1a1a"
    readonly property color dangerBorder: "#e74c3c"
    readonly property color dangerText: "#ffffff"
    readonly property color conflictText: "#ffa500"

    function mix(a, b, t) {
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0);
    }

    // ---- Font scale -- every size a multiple of the system's own font
    // point size, never an absolute number. Qt.application.font already
    // reflects Plasma's "General font" setting on this platform; a few
    // desktops/build configs report pointSize as -1 (pixel-size-only
    // fonts), so fall back to a conventional 10pt default rather than
    // propagate a nonsensical negative multiplier. ----
    readonly property real baseFontPointSize: Qt.application.font.pointSize > 0 ? Qt.application.font.pointSize : 10
    readonly property real fontTiny: baseFontPointSize * 0.85
    readonly property real fontSmall: baseFontPointSize * 0.92
    readonly property real fontNormal: baseFontPointSize
    readonly property real fontMedium: baseFontPointSize * 1.15
    readonly property real fontLarge: baseFontPointSize * 1.4
    readonly property real fontXLarge: baseFontPointSize * 1.6
    readonly property real fontHuge: baseFontPointSize * 2.2
}
