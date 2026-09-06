import QtQuick
import QtQuick.Controls
import SeabassGui

// The small "EXPERIMENTAL" pill next to a page title -- see
// docs/experimental-features.md for what earns one and how a feature
// loses it again.
Rectangle {
    radius: 3
    color: Theme.warnBg
    border.color: Theme.warnBorder
    implicitWidth: experimentalLabel.implicitWidth + 8
    implicitHeight: experimentalLabel.implicitHeight + 4
    Label {
        id: experimentalLabel
        anchors.centerIn: parent
        text: "EXPERIMENTAL"
        font.pointSize: Theme.fontTiny
        font.bold: true
        color: Theme.warnText
    }
}
