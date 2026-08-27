import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DjConvertGui

Page {
    id: root
    required property string stickLabel
    required property string rekordboxPath
    required property string enginePath
    required property var appSettingsController

    readonly property bool hasRekordbox: rekordboxPath.length > 0
    readonly property bool hasEngine: enginePath.length > 0
    readonly property string format: {
        var pref = appSettingsController.preferredFormat;
        if (pref === "engine" && hasEngine) return "engine";
        if (pref === "rekordbox" && hasRekordbox) return "rekordbox";
        return hasEngine ? "engine" : "rekordbox";
    }
    readonly property string path: format === "engine" ? enginePath : rekordboxPath

    LocalCueController {
        id: localCueController
    }

    function refresh() { localCueController.analyzeRestore(root.format, root.path); }

    Component.onCompleted: refresh()
    onFormatChanged: refresh()

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 12
            ToolButton {
                text: "< Back"
                onClicked: root.StackView.view.pop()
            }
            Label {
                text: root.stickLabel + " -- Local Cue Backup"
                font.bold: true
                font.pointSize: 14
            }
            Item { Layout.fillWidth: true }
            FormatToggle {
                appSettingsController: root.appSettingsController
                hasRekordbox: root.hasRekordbox
                hasEngine: root.hasEngine
            }
            ProgressBar {
                visible: localCueController.busy
                indeterminate: localCueController.scanTotal === 0
                value: localCueController.scanTotal > 0
                    ? localCueController.scanCurrent / localCueController.scanTotal : 0
                Layout.preferredWidth: 140
            }
            Label {
                visible: localCueController.busy && localCueController.scanTotal > 0
                text: localCueController.scanCurrent + " / " + localCueController.scanTotal
                color: "gray"
            }
        }
    }

    Dialog {
        id: confirmDialog
        anchors.centerIn: parent
        modal: true
        title: "Restore Cues?"
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: localCueController.applyRestore()

        ColumnLayout {
            spacing: 8
            Label {
                text: "Restore cues onto " + restoreListView.count + " track(s) that currently have none."
                wrapMode: Text.WordWrap
            }
            Label {
                text: "The stick is backed up before anything is written."
                color: "gray"
                wrapMode: Text.WordWrap
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        Label {
            visible: localCueController.errorMessage.length > 0
            text: localCueController.errorMessage
            color: "red"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            visible: localCueController.statusMessage.length > 0
            text: localCueController.statusMessage
            color: "#8fce8f"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Frame {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    ColumnLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Label { text: "Back Up to This Computer"; font.bold: true }
                        Label {
                            text: "Copies this stick's cues to a local backup -- never touches the stick, no confirmation needed."
                            color: "gray"
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                    Button {
                        text: "Backup Now"
                        enabled: !localCueController.busy
                        onClicked: localCueController.backupToComputer(root.format, root.path, root.stickLabel)
                    }
                }
            }
        }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    ColumnLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Label { text: "Restore from This Computer"; font.bold: true }
                        Label {
                            text: "Stick tracks: " + localCueController.stickTrackCount
                                + "   Backed up on this computer: " + localCueController.localTrackCount
                                + "   Restorable: " + restoreListView.count
                            color: "gray"
                        }
                    }
                    Button {
                        text: "Re-Analyze"
                        enabled: !localCueController.busy
                        onClicked: refresh()
                    }
                    Button {
                        text: "Restore " + restoreListView.count + " Track(s)"
                        enabled: !localCueController.busy && restoreListView.count > 0
                        onClicked: confirmDialog.open()
                    }
                }

                ListView {
                    id: restoreListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: localCueController.restoreCandidates
                    spacing: 2

                    delegate: ItemDelegate {
                        width: ListView.view.width

                        required property string filename
                        required property string title
                        required property string artist
                        required property string description

                        contentItem: ColumnLayout {
                            spacing: 2
                            Label { text: title.length > 0 ? (title + " -- " + artist) : filename; font.bold: true }
                            Label { text: description + " cue(s) would be restored"; color: "gray" }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: restoreListView.count === 0 && !localCueController.busy
                        text: "Nothing to restore -- either every stick track already has cues, or none of\nthem match a track backed up on this computer."
                        horizontalAlignment: Text.AlignHCenter
                        color: "gray"
                    }
                }
            }
        }
    }
}
