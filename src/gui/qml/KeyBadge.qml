import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A musical key rendered as a small colored pill, hue picked by its
// Camelot wheel position (see Theme.colorForKey()'s own doc comment for
// why: wheel-adjacent, harmonically compatible keys land close together
// in color too, not just an arbitrary color-per-key). Empty/unparseable
// keys render as a plain "--", same as the bare-text column this
// replaces.
Item {
    id: root
    property string keyName: ""
    Layout.preferredWidth: 50
    Layout.preferredHeight: 22
    Layout.alignment: Qt.AlignVCenter
    implicitWidth: 50
    implicitHeight: 22

    readonly property string camelot: Theme.camelotLabel(root.keyName)

    Label {
        anchors.centerIn: parent
        visible: root.keyName.length === 0 || root.camelot.length === 0
        text: root.keyName.length > 0 ? root.keyName : "--"
        color: Theme.textMuted

        MouseArea {
            id: fallbackHover
            anchors.fill: parent
            hoverEnabled: true
            visible: root.keyName.length > 0
            ToolTip.visible: visible && containsMouse
            ToolTip.text: "Unrecognized key format: " + root.keyName
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: root.camelot.length > 0
        radius: height / 2
        color: Theme.colorForKey(root.keyName)

        Label {
            anchors.centerIn: parent
            text: root.camelot
            font.bold: true
            font.pointSize: Theme.fontSmall
            color: Theme.contrastingTextColor(parent.color)
        }

        MouseArea {
            id: keyHover
            anchors.fill: parent
            hoverEnabled: true
            ToolTip.visible: containsMouse
            ToolTip.text: "Key: " + root.keyName
        }
    }
}
