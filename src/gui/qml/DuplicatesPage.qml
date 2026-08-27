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

    DuplicatesController {
        id: duplicatesController
    }

    Component.onCompleted: duplicatesController.scan(root.format, root.path)
    onFormatChanged: duplicatesController.scan(root.format, root.path)

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
                text: root.stickLabel + " -- Duplicate Tracks"
                font.bold: true
                font.pointSize: 14
            }
            Item { Layout.fillWidth: true }
            FormatToggle {
                appSettingsController: root.appSettingsController
                hasRekordbox: root.hasRekordbox
                hasEngine: root.hasEngine
            }
            Button {
                text: "Apply All Fixable"
                enabled: !duplicatesController.busy
                onClicked: duplicatesController.applyAllUnambiguous()
            }
            ProgressBar {
                visible: duplicatesController.busy
                indeterminate: duplicatesController.scanTotal === 0
                value: duplicatesController.scanTotal > 0
                    ? duplicatesController.scanCurrent / duplicatesController.scanTotal : 0
                Layout.preferredWidth: 140
            }
            Label {
                visible: duplicatesController.busy && duplicatesController.scanTotal > 0
                text: duplicatesController.scanCurrent + " / " + duplicatesController.scanTotal
                color: "gray"
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        Label {
            visible: duplicatesController.errorMessage.length > 0
            text: duplicatesController.errorMessage
            color: "red"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            visible: duplicatesController.statusMessage.length > 0
            text: duplicatesController.statusMessage
            color: "#8fce8f"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        ListView {
            id: plansListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: duplicatesController.plans
            spacing: 4

            delegate: Column {
                id: delegateRoot
                width: ListView.view.width
                spacing: 4

                required property int index
                required property string kind
                required property string filename
                required property string description
                required property bool actionable
                required property var tracks

                property bool expanded: false

                ItemDelegate {
                    width: parent.width
                    onClicked: delegateRoot.expanded = !delegateRoot.expanded

                    contentItem: ColumnLayout {
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: delegateRoot.filename; font.bold: true }
                            Label {
                                text: delegateRoot.kind === "unambiguous" ? "  (fixable)" : "  (conflict)"
                                color: delegateRoot.kind === "unambiguous" ? "#8fce8f" : "orange"
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: "Copy Cues"
                                visible: delegateRoot.actionable
                                onClicked: duplicatesController.applyOne(delegateRoot.index)
                            }
                            Label {
                                text: delegateRoot.expanded ? "▾" : "▸"
                                color: "gray"
                            }
                        }
                        Label { text: delegateRoot.description; color: "gray" }
                    }
                }

                ColumnLayout {
                    width: parent.width
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
                                    Label { text: modelData.title + "  (id=" + modelData.sourceId + ")"; font.bold: true }
                                    Item { Layout.fillWidth: true }
                                    Button {
                                        text: "Copy"
                                        visible: delegateRoot.kind === "conflict"
                                        enabled: modelData.cues.length > 0
                                        ToolTip.visible: hovered
                                        ToolTip.text: "Copy this track's cues to the other copies of the same track"
                                        onClicked: duplicatesController.copyFromTrack(delegateRoot.index, modelData.sourceId)
                                    }
                                    Button {
                                        text: "▶ Play"
                                        enabled: modelData.filePath.length > 0
                                        onClicked: root.playbackController.load(root.format, root.path, modelData.sourceId,
                                            modelData.filePath, modelData.title, modelData.artist, modelData.artworkPath,
                                            modelData.cues)
                                    }
                                }
                                Label {
                                    text: modelData.playlists.length > 0
                                        ? "Playlists: " + modelData.playlists.join(", ")
                                        : "Playlists: (none)"
                                    color: "gray"
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                                WaveformView {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 40
                                    waveformData: modelData.waveform
                                    cueData: modelData.cues
                                    trackDurationMs: modelData.durationMs
                                }
                                Label {
                                    text: modelData.cues.length > 0 ? "Cues:" : "Cues: (none)"
                                    color: "gray"
                                }
                                Repeater {
                                    model: modelData.cues
                                    delegate: Label {
                                        required property var modelData
                                        leftPadding: 12
                                        text: "- " + (modelData.kind === "hot" ? ("hot " + modelData.hotCueNumber) : "memory")
                                            + " @ " + (modelData.positionMs / 1000).toFixed(1) + "s"
                                            + (modelData.color ? "  " + modelData.color : "")
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: plansListView.count === 0 && !duplicatesController.busy
                text: "No duplicate tracks needing attention."
                color: "gray"
            }
        }
    }
}
