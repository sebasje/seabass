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
    // "camelot" (default, e.g. "6A") or "traditional" (e.g. "F♯m") --
    // AppSettingsController.keyNotation, threaded through by every call
    // site rather than read from Theme directly, so this stays testable/
    // previewable without a running AppSettingsController.
    property string notation: "camelot"
    Layout.preferredWidth: 50 * Theme.iconScale
    Layout.preferredHeight: 22 * Theme.iconScale
    Layout.alignment: Qt.AlignVCenter
    implicitWidth: 50 * Theme.iconScale
    implicitHeight: 22 * Theme.iconScale

    readonly property string camelot: Theme.camelotLabel(root.keyName)
    readonly property string badgeLabel: root.notation === "traditional"
        ? Theme.traditionalLabel(root.keyName) : root.camelot
    // Always the full spoken form regardless of notation -- the whole
    // point of hovering is "how do I actually say this," which the
    // short badge label (either "6A" or "F♯m") doesn't spell out on its
    // own.
    readonly property string spokenLabel: Theme.traditionalSpokenLabel(root.keyName)

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
            text: root.badgeLabel
            font.bold: true
            font.pointSize: Theme.fontSmall
            color: Theme.contrastingTextColor(parent.color)
        }

        MouseArea {
            id: keyHover
            anchors.fill: parent
            hoverEnabled: true
            ToolTip.visible: containsMouse
            ToolTip.text: "Key: " + root.spokenLabel
                + (root.notation === "traditional" ? " (" + root.camelot + ")" : "")
        }
    }
}
