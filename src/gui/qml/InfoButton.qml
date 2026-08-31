import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// A small "?" button that opens a popup with an explanation, in place of
// pointing someone at a file in the source tree they don't have (e.g. a
// docs/*.md) -- anything a person running the built app needs to
// understand should be explained inside the app itself.
ToolButton {
    id: root
    required property string explanationTitle
    required property string explanationText

    text: "?"
    implicitWidth: 28
    implicitHeight: 28

    ToolTip.visible: hovered
    ToolTip.text: "More info"

    onClicked: popup.open()

    Popup {
        id: popup
        x: Math.round((root.width - width) / 2)
        y: root.height
        width: 420
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surface
            border.color: Theme.textMuted
            radius: 6
        }

        ColumnLayout {
            width: parent.width
            spacing: 8

            Label {
                text: root.explanationTitle
                font.bold: true
                font.pointSize: Theme.fontMedium
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                text: root.explanationText
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }
}
