import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DjConvertGui

Page {
    id: root
    required property string stickLabel
    required property string rekordboxPath
    required property string enginePath

    BackupsController {
        id: backupsController
    }

    Component.onCompleted: backupsController.load(root.rekordboxPath, root.enginePath)

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            ToolButton {
                text: "< Back"
                onClicked: root.StackView.view.pop()
            }
            Label {
                text: root.stickLabel + " -- Backups"
                font.bold: true
                font.pointSize: 14
            }
            Item { Layout.fillWidth: true }
            Label { text: "Keep:" }
            SpinBox {
                id: keepSpinBox
                from: 0
                to: 1000
                value: 10
                ToolTip.visible: hovered
                ToolTip.text: "How many of the most recent backups to keep"
            }
            Button {
                text: "Clean Up"
                enabled: backupsListView.count > 0
                ToolTip.visible: hovered
                ToolTip.text: "Permanently delete older backup copies -- never touches the stick's live data"
                onClicked: confirmCleanDialog.open()
            }
        }
    }

    Dialog {
        id: confirmCleanDialog
        anchors.centerIn: parent
        modal: true
        title: "Clean Up Backups?"
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: backupsController.clean(keepSpinBox.value)

        Label {
            text: "This permanently deletes the " + Math.max(0, backupsListView.count - keepSpinBox.value)
                + " oldest backup(s) under " + backupsController.backupDir
                + ".\nIt never touches the stick's live Rekordbox/Engine data."
            wrapMode: Text.WordWrap
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        Label {
            visible: backupsController.errorMessage.length > 0
            text: backupsController.errorMessage
            color: "red"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            visible: backupsController.statusMessage.length > 0
            text: backupsController.statusMessage
            color: "#8fce8f"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            text: backupsListView.count + " backup(s), " + backupsController.totalSizeHuman + " total -- " + backupsController.backupDir
            color: "gray"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        ListView {
            id: backupsListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: backupsController.backups
            spacing: 2

            delegate: ItemDelegate {
                width: ListView.view.width

                required property string id
                required property string label
                required property string sizeHuman

                contentItem: RowLayout {
                    Label { text: id; font.family: "monospace"; Layout.preferredWidth: 220 }
                    Label { text: label; color: "gray"; Layout.fillWidth: true }
                    Label { text: sizeHuman; color: "gray" }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: backupsListView.count === 0
                text: "No backups found for this stick."
                color: "gray"
            }
        }
    }
}
