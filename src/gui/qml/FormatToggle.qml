import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Rekordbox/Engine mode switch shared by ScanPage, DuplicatesPage and
// LocalCuePage -- bound to AppSettingsController.preferredFormat, so the
// last-chosen format is remembered across all three (and across app
// restarts). Whichever side isn't present on this stick is disabled
// rather than hidden, so the control's shape stays stable.
RowLayout {
    id: root
    required property var appSettingsController
    property bool hasRekordbox: true
    property bool hasEngine: true
    spacing: 4

    ButtonGroup { id: group }

    // "◎"/"⬡" are original, generic glyphs -- not reproductions of either
    // company's actual logo -- used purely so the two modes are visually
    // distinct at a glance. See AboutPage.qml for the trademark note. Kept
    // in their own Label (not appended into the button's text) since the
    // symbol font that has these glyphs has no Latin letters of its own.
    component FormatButton: Button {
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
                color: btn.palette.buttonText
            }
            Label {
                text: btn.label
                color: btn.palette.buttonText
            }
        }
    }

    FormatButton {
        glyph: "◎"
        label: "Rekordbox"
        checked: root.appSettingsController.preferredFormat === "rekordbox"
        enabled: root.hasRekordbox
        ButtonGroup.group: group
        onClicked: root.appSettingsController.preferredFormat = "rekordbox"
    }
    FormatButton {
        glyph: "⬡"
        label: "Engine"
        checked: root.appSettingsController.preferredFormat === "engine"
        enabled: root.hasEngine
        ButtonGroup.group: group
        onClicked: root.appSettingsController.preferredFormat = "engine"
    }
}
