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
                text: "‹"

                font.pointSize: Theme.fontHuge
                enabled: !syncController.writing

                ToolTip.visible: hovered

                ToolTip.text: syncController.writing ? "Wait for the write to finish before leaving this page" : "Back"
                onClicked: root.StackView.view.pop()
            }
            Label {
                text: root.stickLabel + " -- Sync Cues Between Rekordbox and Engine"
                font.bold: true
                font.pointSize: Theme.fontLarge
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
            Button {
                text: "Undo"
                visible: syncController.canUndo
                enabled: !syncController.busy
                ToolTip.visible: hovered
                ToolTip.text: "Revert the last sync -- restores every file it touched to what it was before"
                onClicked: syncController.undoLastOperation()
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
                color: Theme.textMuted
            }
        }
    }

    Dialog {
        id: confirmDialog
        anchors.centerIn: parent
        modal: true
        title: "Apply Sync?"
        footer: DialogButtonBox {
            Button { text: "Sync Now"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
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
                color: Theme.conflictText
                wrapMode: Text.WordWrap
            }
            Label {
                text: "Both sides are backed up before anything is written -- once this finishes, "
                    + "\"Undo\" reverts every file it touched. Do not remove the stick while it's running."
                color: Theme.textMuted
                wrapMode: Text.WordWrap
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        StickWriteWarning {
            visible: syncController.writing
            text: "Writing cues to the stick -- do not remove it until this finishes."
        }

        Label {
            visible: syncController.errorMessage.length > 0
            text: syncController.errorMessage
            color: Theme.danger
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            visible: syncController.statusMessage.length > 0
            text: syncController.statusMessage
            color: Theme.good
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            visible: !syncController.busy
            text: "Rekordbox tracks: " + syncController.rekordboxTrackCount
                + "   Engine tracks: " + syncController.engineTrackCount
                + "   matched, needing sync: " + plansListView.count
            color: Theme.textMuted
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
                    id: planRow
                    width: parent.width
                    hoverEnabled: true
                    onClicked: delegateRoot.expanded = !delegateRoot.expanded

                    ToolTip.visible: hovered
                    ToolTip.text: delegateRoot.filename

                    contentItem: ColumnLayout {
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: delegateRoot.direction === "toEngine" ? "Rekordbox -> Engine" : "Engine -> Rekordbox"
                                font.bold: true
                                color: delegateRoot.direction === "toEngine" ? Theme.good : Theme.info
                                Layout.preferredWidth: 160
                            }
                            Label {
                                text: delegateRoot.tracks.length > 0
                                    ? (delegateRoot.tracks[0].title + " -- " + delegateRoot.tracks[0].artist)
                                    : delegateRoot.filename
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: delegateRoot.conflict ? "  (conflict resolved by newer file)" : ""
                                color: Theme.conflictText
                            }
                            Button {
                                text: "Copy"
                                enabled: !syncController.busy
                                ToolTip.visible: hovered
                                ToolTip.text: "Sync just this one track now"
                                onClicked: syncController.applyOne(delegateRoot.index)
                            }
                            Label {
                                text: delegateRoot.expanded ? "▾" : "▸"
                                color: Theme.textMuted
                            }
                        }
                        Label { text: delegateRoot.description; color: Theme.textMuted; leftPadding: 168 }
                    }
                }

                // Groups the two sides' copies of this track visually --
                // matches DuplicatesPage.qml's treatment, and same
                // rationale: several stacked expanded groups otherwise read
                // as one long list rather than distinct matched pairs.
                Rectangle {
                    x: 168
                    width: parent.width - 168
                    visible: delegateRoot.expanded
                    height: delegateRoot.expanded ? syncGroupColumn.implicitHeight + 16 : 0
                    color: Theme.groupBackground
                    border.color: Theme.borderSubtle
                    radius: 4

                    ColumnLayout {
                        id: syncGroupColumn
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8

                    // The "meta track" both sides below are matched copies
                    // of -- ties the pair together as one group rather than
                    // two unrelated-looking Rekordbox/Engine frames.
                    Label {
                        Layout.fillWidth: true
                        text: (delegateRoot.tracks.length > 0
                            ? delegateRoot.tracks[0].title + " -- " + delegateRoot.tracks[0].artist
                            : delegateRoot.filename)
                        font.bold: true
                        elide: Text.ElideRight
                    }

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
                                CueFallbackNotice {
                                    cues: modelData.cues
                                    durationMs: modelData.durationMs
                                }
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
                color: Theme.textMuted
            }
        }
    }
}
