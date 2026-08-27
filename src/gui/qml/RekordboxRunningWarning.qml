import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DjConvertGui

// Shown globally, above every page, whenever Rekordbox appears to be
// running on this machine -- both it and djconvert writing to the same
// stick files at once risks corrupting them. This is a best-effort
// detection (see RekordboxGuardController's doc comment), so every write
// also refuses itself independently; this banner is the visible half of
// that protection. Collapses to zero height when not visible.
Rectangle {
    id: root
    property alias text: label.text
    visible: false
    Layout.fillWidth: true
    implicitHeight: visible ? contentRow.implicitHeight + 16 : 0
    color: Theme.dangerBg
    border.color: Theme.dangerBorder
    radius: 4

    RowLayout {
        id: contentRow
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        Label {
            text: "⛔"
            font.family: "Noto Sans Symbols2"
            font.pointSize: Theme.fontMedium
        }
        Label {
            id: label
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.bold: true
            color: Theme.dangerText
            text: "Rekordbox appears to be running -- writes to this stick are refused until it's closed, "
                + "to avoid corrupting your library."
        }
    }
}
