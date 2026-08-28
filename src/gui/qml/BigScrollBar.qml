import QtQuick
import QtQuick.Controls
import DjConvertGui

// Explicit, custom-drawn ScrollBar -- the platform style's own auto-attached
// overlay scrollbar (KDE Breeze, on this project's dev desktop) renders as a
// near-invisible hairline, so every scrollable list defines its own instead
// of relying on the style default. Assign to a Flickable's
// `ScrollBar.vertical` attached property.
ScrollBar {
    id: control
    policy: ScrollBar.AsNeeded
    implicitWidth: 16
    padding: 2

    contentItem: Rectangle {
        implicitWidth: 12
        implicitHeight: 40
        radius: width / 2
        color: control.pressed ? Theme.accent : Theme.textMuted
        opacity: control.pressed ? 0.9 : (control.active ? 0.7 : 0.45)
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }

    background: Rectangle {
        implicitWidth: 16
        radius: width / 2
        color: Theme.groupBackground
    }
}
