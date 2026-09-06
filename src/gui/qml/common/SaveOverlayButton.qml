import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// The one way changes reach the stick: a floating Save button in the
// bottom-right corner of an edit page, prominent on purpose (user
// decision) and disabled until there is something to save. The badge
// shows how many changes are staged; the tooltip lists them.
Item {
    id: root
    required property var session

    readonly property bool hasSession: root.session !== null && root.session !== undefined
    readonly property int pendingCount: root.hasSession ? root.session.pendingCount : 0
    readonly property bool canSave: root.hasSession && root.session.dirty === true && root.session.state !== "writing"

    implicitWidth: button.implicitWidth
    implicitHeight: button.implicitHeight
    visible: root.hasSession

    Button {
        id: button
        objectName: "saveButton"
        anchors.fill: parent
        enabled: root.canSave
        hoverEnabled: true
        leftPadding: 28
        rightPadding: 28
        topPadding: 14
        bottomPadding: 14
        onClicked: root.session.save()

        background: Rectangle {
            radius: height / 2
            color: !button.enabled ? Theme.surface
                : button.down ? Qt.darker(Theme.accent, 1.25)
                : button.hovered ? Qt.lighter(Theme.accent, 1.1)
                : Theme.accent
            border.color: button.enabled ? Qt.darker(Theme.accent, 1.3) : Theme.border
            border.width: 1
        }
        contentItem: RowLayout {
            spacing: 10
            Label {
                text: "Save"
                font.family: Theme.titleFamily
                font.weight: Font.Bold
                font.pointSize: Theme.fontLarge
                color: button.enabled ? "#ffffff" : Theme.textMuted
            }
            Rectangle {
                objectName: "pendingBadge"
                visible: root.pendingCount > 0
                radius: height / 2
                color: button.enabled ? "#ffffff" : Theme.border
                implicitWidth: Math.max(badgeText.implicitWidth + 12, implicitHeight)
                implicitHeight: badgeText.implicitHeight + 6
                Label {
                    id: badgeText
                    anchors.centerIn: parent
                    text: root.pendingCount
                    font.bold: true
                    font.pointSize: Theme.fontSmall
                    color: button.enabled ? Theme.accent : Theme.textMuted
                }
            }
        }

        ToolTip.visible: hovered
        ToolTip.delay: 400
        ToolTip.text: {
            if (!root.hasSession) return "";
            if (root.pendingCount === 0) return "No unsaved changes";
            var lines = root.session.pendingDescriptions.slice(0, 8);
            if (root.pendingCount > 8) lines.push("... and " + (root.pendingCount - 8) + " more");
            return "Save " + root.pendingCount + " change(s) to the stick:\n" + lines.join("\n");
        }
    }
}
