import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Browse Library's own three-way catalog switch -- Engine OS, Rekordbox's
// classic per-USB Device Library, and Rekordbox 7's newer OneLibrary
// (Device Library Plus). Deliberately separate from FormatToggle.qml
// (used everywhere else): that's a persisted, binary Rekordbox/Engine
// setting shared by Clean Up, Sync, and Local Cue Backup, none of which
// have an "onelibrary" write path -- this is browse-only, scoped to this
// one page, and not persisted. See AboutPage.qml for the fuller
// explanation of what distinguishes all three.
RowLayout {
    id: root
    property string current: "rekordbox"
    property bool hasRekordbox: true
    property bool hasEngine: true
    property bool hasOneLibrary: true
    // Distinct from hasOneLibrary: a page can have a real OneLibrary
    // export present (hasOneLibrary true) but still not support it for
    // the specific operation this toggle drives -- e.g. CleanupPage's
    // destructive removal, which needs OneLibraryCueWriter to reassign a
    // removed track's playlist membership to the survivor and doesn't
    // yet (see cleanup_controller.cpp's own comment). Defaults to
    // mirroring hasOneLibrary, so a page that never sets this keeps the
    // exact same enabled/disabled behavior as before this existed.
    property bool oneLibrarySupported: hasOneLibrary
    // Overrides the disabled tooltip specifically for the "present but
    // not supported here" case above -- the default tooltip below
    // ("Not present on this export") would be actively wrong there.
    property string oneLibraryUnsupportedReason: ""
    // Not named currentChanged -- `property string current` already
    // auto-generates that signal (with no arguments) for its own change
    // notification; declaring another signal with the same name here
    // would collide with it.
    signal sourceRequested(string value)
    spacing: 4

    ButtonGroup { id: group }

    // Glyphs are original/generic, not reproductions of either company's
    // real logo -- same principle FormatToggle.qml's own comment states.
    component SourceButton: Button {
        id: btn
        property string glyph
        property string label
        checkable: true
        flat: !checked
        contentItem: RowLayout {
            spacing: 4
            Label {
                text: btn.glyph
                font.family: "Noto Sans Symbols2"
                color: btn.checked ? Theme.text : btn.palette.buttonText
            }
            Label {
                text: btn.label
                color: btn.checked ? Theme.text : btn.palette.buttonText
            }
        }
        // Explicit background rather than the active style's default --
        // on this KDE system that resolves to org.kde.breeze regardless
        // of the app's own Material palette (same issue already worked
        // around for ToolBar elsewhere) and draws square corners, unlike
        // every other radius: 4 element this app draws itself.
        background: Rectangle {
            radius: 4
            color: btn.checked ? Theme.primary : (btn.hovered ? Theme.rowHover : "transparent")
            border.color: Theme.border
            border.width: btn.checked ? 0 : 1
        }
    }

    SourceButton {
        glyph: "⬡"
        label: "Engine OS"
        checked: root.current === "engine"
        enabled: root.hasEngine
        ButtonGroup.group: group
        ToolTip.visible: hovered
        ToolTip.text: "Denon Engine DJ's own library format (m.db) -- what Denon/inMusic hardware "
            + "(SC5000, Prime series, ...) reads directly from the stick."
        onClicked: root.sourceRequested("engine")
    }
    SourceButton {
        glyph: "◎"
        label: "DeviceLibrary"
        checked: root.current === "rekordbox"
        enabled: root.hasRekordbox
        ButtonGroup.group: group
        ToolTip.visible: hovered
        ToolTip.text: "Rekordbox's classic per-stick export (export.pdb) -- what CDJs and XDJs read "
            + "directly. Every rekordbox export has this."
        onClicked: root.sourceRequested("rekordbox")
    }
    SourceButton {
        glyph: "◈"
        label: "OneLibrary"
        checked: root.current === "onelibrary"
        enabled: root.hasOneLibrary && root.oneLibrarySupported
        ButtonGroup.group: group
        ToolTip.visible: hovered
        ToolTip.text: {
            if (!root.hasOneLibrary) {
                return "Not present on this export -- OneLibrary only exists on newer rekordbox exports.";
            }
            if (!root.oneLibrarySupported && root.oneLibraryUnsupportedReason.length > 0) {
                return root.oneLibraryUnsupportedReason;
            }
            return "Rekordbox 7's newer unified library format (exportLibrary.db) -- mirrors the Device "
                + "Library's tracks in a richer schema. You can add cues here directly, and duplicate "
                + "cues can be copied across copies the same way as the other two catalogs.";
        }
        onClicked: root.sourceRequested("onelibrary")
    }
}
