import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// A real, modal, in-window popup for a status/error message that would
// otherwise be easy to miss as plain inline text -- e.g. the result of
// an action taken somewhere else on a long page (LocalCuePage's
// "Restore From Here" buttons up in Backup History, whose outcome used
// to only show as a quiet line of text down in a different section).
// Deliberately does NOT auto-dismiss (an earlier version did, on a
// timer -- explicitly rejected: the message needs to actually be seen
// and acknowledged, not flash past). Renders in this window (parented
// to Overlay.overlay, not a separate top-level window), and only closes
// on an explicit action -- the "OK" button, the close glyph, or Escape.
Popup {
    id: popup

    property alias text: label.text
    property color messageColor: Theme.good

    parent: Overlay.overlay
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 16

    background: Rectangle {
        radius: 8
        color: Theme.surface
        border.color: popup.messageColor
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Label {
                id: label
                Layout.fillWidth: true
                Layout.maximumWidth: 480
                color: popup.messageColor
                wrapMode: Text.WordWrap
            }
            ToolButton {
                text: "✕"
                flat: true
                implicitWidth: Theme.iconSizeSmall
                implicitHeight: Theme.iconSizeSmall
                onClicked: popup.close()
            }
        }
        Button {
            Layout.alignment: Qt.AlignRight
            text: "OK"
            onClicked: popup.close()
        }
    }

    function show(message, color) {
        popup.text = message;
        popup.messageColor = color;
        popup.open();
    }
}
