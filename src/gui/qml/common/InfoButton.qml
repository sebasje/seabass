import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
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

        // Centred on the button, then pulled back inside the window if
        // that would push it past an edge. This button is usually the
        // last thing on a header row, i.e. hard against the window's
        // right edge, where centring alone put most of the text outside
        // the window -- silently clipped, with no scrollbar and no hint
        // that anything was missing.
        readonly property int edgeMargin: 8
        readonly property real centeredX: Math.round((root.width - width) / 2)
        readonly property real sceneX: root.mapToItem(null, 0, 0).x
        readonly property real clampedSceneX: Math.max(
            edgeMargin, Math.min(sceneX + centeredX, root.Window.width - width - edgeMargin))

        x: centeredX + (clampedSceneX - (sceneX + centeredX))
        y: root.height
        width: Math.min(420, root.Window.width - 2 * edgeMargin)
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
