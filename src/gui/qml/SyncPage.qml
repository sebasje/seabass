import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DjConvertGui

Page {
    id: root
    required property string stickLabel
    required property string rekordboxPath
    required property string enginePath
    required property var playbackController

    SyncController {
        id: syncController
    }

    Component.onCompleted: syncController.analyze(root.rekordboxPath, root.enginePath)

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            ToolButton {
                text: "< Back"
                onClicked: root.StackView.view.pop()
            }
            Label {
                text: root.stickLabel + " -- Sync Cues Between Rekordbox and Engine"
                font.bold: true
                font.pointSize: 14
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "Re-Analyze"
                enabled: !syncController.busy
                ToolTip.visible: hovered
                ToolTip.text: "Re-scan both libraries and recompute what needs syncing"
                onClicked: syncController.analyze(root.rekordboxPath, root.enginePath)
            }
            Button {
                text: "Apply " + plansListView.count + " change(s)"
                enabled: !syncController.busy && plansListView.count > 0
                ToolTip.visible: hovered
                ToolTip.text: "Review and confirm before writing any cues"
                onClicked: confirmDialog.open()
            }
            ProgressBar {
                visible: syncController.busy
                indeterminate: syncController.scanTotal === 0
                value: syncController.scanTotal > 0
                    ? syncController.scanCurrent / syncController.scanTotal : 0
                Layout.preferredWidth: 140
            }
            Label {
                visible: syncController.busy && syncController.scanTotal > 0
                text: syncController.scanCurrent + " / " + syncController.scanTotal
                color: "gray"
            }
        }
    }

    Dialog {
        id: confirmDialog
        anchors.centerIn: parent
        modal: true
        title: "Apply Sync?"
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: syncController.apply()

        ColumnLayout {
            spacing: 8
            Label {
                visible: syncController.toEngineCount > 0
                text: "Copy cues to Engine for " + syncController.toEngineCount + " track(s)."
                wrapMode: Text.WordWrap
            }
            Label {
                visible: syncController.toRekordboxCount > 0
                text: "Copy cues to Rekordbox for " + syncController.toRekordboxCount + " track(s)."
                wrapMode: Text.WordWrap
            }
            Label {
                visible: syncController.toRekordboxCount > 0
                text: "Rekordbox writing is the least-proven part of djconvert -- verify the result\non real hardware before trusting it for a gig."
                color: "orange"
                wrapMode: Text.WordWrap
            }
            Label {
                text: "Both sides are backed up before anything is written."
                color: "gray"
                wrapMode: Text.WordWrap
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        Label {
            visible: syncController.errorMessage.length > 0
            text: syncController.errorMessage
            color: "red"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            visible: syncController.statusMessage.length > 0
            text: syncController.statusMessage
            color: "#8fce8f"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            visible: !syncController.busy
            text: "Rekordbox tracks: " + syncController.rekordboxTrackCount
                + "   Engine tracks: " + syncController.engineTrackCount
                + "   matched, needing sync: " + plansListView.count
            color: "gray"
        }

        ListView {
            id: plansListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: syncController.plans
            spacing: 4

            delegate: Column {
                id: delegateRoot
                width: ListView.view.width
                spacing: 4

                required property int index
                required property string direction
                required property string filename
                required property string description
                required property bool conflict
                required property var tracks

                property bool expanded: false

                ItemDelegate {
                    width: parent.width
                    onClicked: delegateRoot.expanded = !delegateRoot.expanded

                    contentItem: ColumnLayout {
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: delegateRoot.direction === "toEngine" ? "-> Engine" : "-> rekordbox"
                                font.bold: true
                                color: delegateRoot.direction === "toEngine" ? "#8fce8f" : "#8ab4f8"
                                Layout.preferredWidth: 100
                            }
                            Label { text: delegateRoot.filename; font.bold: true }
                            Label {
                                text: delegateRoot.conflict ? "  (conflict resolved by newer file)" : ""
                                color: "orange"
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: delegateRoot.expanded ? "▾" : "▸"
                                color: "gray"
                            }
                        }
                        Label { text: delegateRoot.description; color: "gray"; leftPadding: 108 }
                    }
                }

                ColumnLayout {
                    x: 108
                    width: parent.width - 108
                    visible: delegateRoot.expanded
                    spacing: 8

                    Repeater {
                        model: delegateRoot.tracks
                        delegate: Frame {
                            Layout.fillWidth: true
                            required property var modelData

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 4
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        text: (modelData.side === "rekordbox" ? "Rekordbox: " : "Engine: ")
                                            + modelData.title + " -- " + modelData.artist
                                        font.bold: true
                                    }
                                    Item { Layout.fillWidth: true }
                                    Button {
                                        text: "▶ Play"
                                        enabled: modelData.filePath.length > 0
                                        ToolTip.visible: hovered
                                        ToolTip.text: "Play this copy of the track"
                                        onClicked: root.playbackController.load(modelData.side, modelData.side === "rekordbox" ? root.rekordboxPath : root.enginePath,
                                            modelData.sourceId, modelData.filePath, modelData.title, modelData.artist, modelData.artworkPath,
                                            modelData.cues)
                                    }
                                }
                                WaveformView {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 40
                                    waveformData: modelData.waveform
                                    cueData: modelData.cues
                                    trackDurationMs: modelData.durationMs
                                }
                            }
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: plansListView.count === 0 && !syncController.busy
                text: "Nothing to sync -- matched tracks' cues are already consistent."
                color: "gray"
            }
        }
    }
}
