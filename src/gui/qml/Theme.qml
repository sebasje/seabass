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

    // Perceptual luminance of the *actual current* background -- not just
    // useSystemTheme, since useSystemTheme:true resolves to Material.System,
    // which can itself land on a dark Plasma color scheme. Deciding by real
    // luminance is correct in every combination (Kelp, light system theme,
    // dark system theme) instead of two.
    readonly property bool isLightBackground: (background.r * 0.299 + background.g * 0.587 + background.b * 0.114) > 0.5

    // ---- Status colors -- same semantic meaning in both modes (a warning
    // reads as a warning regardless of theme), but NOT the same literal
    // hex in both: the pastel shades below read clearly against Kelp's
    // always-dark background (what these were originally tuned against)
    // but wash out to near-illegible against a light background -- pastel
    // green/red/blue text on near-white has poor contrast even though the
    // hue is "right." Each gets a darker, more saturated light-mode
    // counterpart with the same hue family instead. ----
    readonly property color good: isLightBackground ? "#1e7d32" : "#8fce8f"
    readonly property color info: isLightBackground ? "#1456b0" : "#8ab4f8"
    readonly property color danger: isLightBackground ? "#c62828" : "#ff8080"
    readonly property color warnBg: isLightBackground ? "#fbeecb" : "#4a3510"
    readonly property color warnBorder: isLightBackground ? "#c99a2e" : "#c99a2e"
    readonly property color warnText: isLightBackground ? "#6b4f0a" : "#f0d080"
    readonly property color warnIcon: isLightBackground ? "#a3760a" : "#f0c040"
    readonly property color dangerBg: isLightBackground ? "#f8d7d7" : "#5c1a1a"
    readonly property color dangerBorder: "#e74c3c"
    readonly property color dangerText: isLightBackground ? "#5c1a1a" : "#ffffff"
    readonly property color conflictText: isLightBackground ? "#a85300" : "#ffa500"

    function mix(a, b, t) {
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0);
    }

    // ---- Musical key -> Camelot wheel color. The wheel itself (12
    // positions arranged by the circle of fifths, each with a relative
    // major/minor pair) is a standard, vendor-neutral DJ convention, not
    // any one company's IP -- but the actual *colors* Mixed In Key/
    // Rekordbox/etc. paint each wedge are their own product design, so
    // this is an original palette, not a reproduction: hue is simply the
    // wheel position itself (camelot number 1..12 spread evenly around
    // the color wheel), so two keys that are close together on the
    // wheel -- i.e. harmonically compatible, safe to mix -- also look
    // close together in color. That visual-distance-mirrors-harmonic-
    // distance property is the actual creative idea here, not a
    // particular set of hex codes. Minor vs. major then reads as
    // "moodier, more saturated and darker" vs. "brighter" at the same
    // hue, rather than a hue change, so relative major/minor pairs
    // (e.g. Am/C, both camelot 8) stay visibly related too. ----

    // Pitch class (0=C .. 11=B) -> Camelot number, one table per mode.
    // Derived directly from the wheel's own relative-major/minor
    // pairing (e.g. camelot 8 is A minor *and* C major), not guessed.
    readonly property var camelotMinorByPitchClass: [5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10]
    readonly property var camelotMajorByPitchClass: [8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1]
    readonly property var pitchClassByName: ({
        "C": 0, "B#": 0,
        "C#": 1, "Db": 1,
        "D": 2,
        "D#": 3, "Eb": 3,
        "E": 4, "Fb": 4,
        "F": 5, "E#": 5,
        "F#": 6, "Gb": 6,
        "G": 7,
        "G#": 8, "Ab": 8,
        "A": 9,
        "A#": 10, "Bb": 10,
        "B": 11, "Cb": 11,
    })

    // Parses a key string like "Dm", "F#m", "Bb", "C#" into
    // {camelotNumber: 1..12, isMinor: bool}, or null if it isn't a
    // recognized key spelling (never guessed at -- an unparseable key
    // just gets no color, same "don't fabricate it" stance as
    // conflictText/color-tag handling elsewhere in this codebase).
    function parseCamelotKey(keyStr) {
        if (!keyStr || keyStr.length === 0) {
            return null;
        }
        var isMinor = keyStr.length > 1 && keyStr.charAt(keyStr.length - 1) === "m";
        var notePart = isMinor ? keyStr.slice(0, -1) : keyStr;
        var pitchClass = pitchClassByName[notePart];
        if (pitchClass === undefined) {
            return null;
        }
        var camelotNumber = isMinor ? camelotMinorByPitchClass[pitchClass] : camelotMajorByPitchClass[pitchClass];
        return {camelotNumber: camelotNumber, isMinor: isMinor};
    }

    function camelotLabel(keyStr) {
        var parsed = parseCamelotKey(keyStr);
        return parsed ? (parsed.camelotNumber + (parsed.isMinor ? "A" : "B")) : "";
    }

    function colorForKey(keyStr) {
        var parsed = parseCamelotKey(keyStr);
        if (!parsed) {
            return textMuted;
        }
        var hue = (parsed.camelotNumber - 1) / 12;
        return parsed.isMinor ? Qt.hsla(hue, 0.55, 0.38, 1.0) : Qt.hsla(hue, 0.65, 0.55, 1.0);
    }

    // Legible text color for a badge painted with colorForKey()'s output
    // -- computed from real luminance rather than assumed from the HSL
    // lightness that produced it, since the same lightness renders
    // visually lighter or darker depending on hue.
    function contrastingTextColor(bg) {
        var luminance = 0.299 * bg.r + 0.587 * bg.g + 0.114 * bg.b;
        return luminance > 0.6 ? "#000000" : "#ffffff";
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
