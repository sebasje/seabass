import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Shown while a controller is actively writing to the stick (not merely
// scanning it) -- unplugging mid-write can corrupt the very file being
// written. Collapses to zero height when not visible, so pages can leave
// it in their layout unconditionally.
Rectangle {
    id: root
    property alias text: label.text
    visible: false
    Layout.fillWidth: true
    implicitHeight: visible ? contentRow.implicitHeight + 16 : 0
    color: "#4a3510"
    border.color: "#c99a2e"
    radius: 4

    RowLayout {
        id: contentRow
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        Label {
            text: "⚠"
            font.family: "Noto Sans Symbols2"
            font.pointSize: 16
            color: "#f0c040"
        }
        Label {
            id: label
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: "#f0d080"
            text: "Writing to the stick -- do not remove it until this finishes."
        }
    }
}
