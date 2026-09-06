import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// Blocks leaving a page (or the app) with staged changes. Only two ways
// out, by design: save them or throw them away.
Dialog {
    id: dialog
    property string message: "You have made changes to your library. Please save these changes to your library first."
    property string saveText: "Save Changes"
    property string discardText: "Discard Changes"
    property int pendingCount: 0

    signal saveRequested()
    signal discardRequested()

    anchors.centerIn: parent
    modal: true
    closePolicy: Popup.NoAutoClose
    width: 480
    title: "Unsaved changes"

    footer: DialogButtonBox {
        Button {
            objectName: "saveButton"
            text: dialog.saveText
            highlighted: true
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        Button {
            objectName: "discardButton"
            text: dialog.discardText
            DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            onClicked: {
                dialog.close();
                dialog.discardRequested();
            }
        }
    }
    onAccepted: dialog.saveRequested()

    ColumnLayout {
        width: parent.width
        spacing: 8
        Label {
            objectName: "messageLabel"
            color: Theme.text
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: dialog.message
        }
        Label {
            visible: dialog.pendingCount > 0
            Layout.fillWidth: true
            color: Theme.textMuted
            font.pointSize: Theme.fontSmall
            text: dialog.pendingCount + " change(s) are staged and not on the stick yet."
        }
    }
}
