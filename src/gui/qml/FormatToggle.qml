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

    Button {
        text: "Rekordbox"
        checkable: true
        flat: !checked
        checked: root.appSettingsController.preferredFormat === "rekordbox"
        enabled: root.hasRekordbox
        ButtonGroup.group: group
        onClicked: root.appSettingsController.preferredFormat = "rekordbox"
    }
    Button {
        text: "Engine"
        checkable: true
        flat: !checked
        checked: root.appSettingsController.preferredFormat === "engine"
        enabled: root.hasEngine
        ButtonGroup.group: group
        onClicked: root.appSettingsController.preferredFormat = "engine"
    }
}
