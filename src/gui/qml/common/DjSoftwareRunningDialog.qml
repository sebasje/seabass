import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// The process guard's modal: up while rekordbox or Engine DJ is running
// *and* a library is in edit or write mode (guard.blocking). No buttons
// on purpose -- it closes itself the moment the other app is gone, and
// while it is up the guard polls every second (guard.dialogOpen).
Dialog {
    id: dialog
    required property var guard

    anchors.centerIn: parent
    modal: true
    closePolicy: Popup.NoAutoClose
    width: 520
    title: "Close " + dialog.guard.conflictingSoftware + " first"

    onOpened: dialog.guard.dialogOpen = true
    onClosed: dialog.guard.dialogOpen = false

    function sync() {
        if (dialog.guard.blocking && !dialog.opened) {
            dialog.open();
        } else if (!dialog.guard.blocking && dialog.opened) {
            dialog.close();
        }
    }

    Connections {
        target: dialog.guard
        function onBlockingChanged() { dialog.sync(); }
    }
    Component.onCompleted: sync()

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label {
            objectName: "messageLabel"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: dialog.guard.conflictingSoftware + " is running. Please close it until your changes have been "
                + "saved. If you have already made changes to your USB stick in " + dialog.guard.conflictingSoftware
                + ", better discard the changes made here, you might lose data. (You have been warned!)"
        }
        Label {
            Layout.fillWidth: true
            text: "Checking every second; this closes by itself once " + dialog.guard.conflictingSoftware
                + " is gone."
            color: Theme.textMuted
            font.pointSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }
    }
}
