import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DjConvertGui

Page {
    id: root
    required property string format
    required property string path
    required property string stickLabel
    required property var playbackController

    ScanController {
        id: scanController
    }

    property int selectedPlaylistIndex: 0

    Component.onCompleted: scanController.scan(root.format, root.path)

    function formatDuration(seconds) {
        var total = Math.round(seconds);
        var m = Math.floor(total / 60);
        var s = total % 60;
        return m + ":" + (s < 10 ? "0" : "") + s;
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            ToolButton {
                text: "< Back"
                onClicked: root.StackView.view.pop()
            }
            Label {
                text: root.stickLabel + " -- " + (root.format === "rekordbox" ? "rekordbox" : "Engine") + " library"
                font.bold: true
                font.pointSize: 14
            }
            Item { Layout.fillWidth: true }
            BusyIndicator {
                running: scanController.busy
                visible: scanController.busy
                implicitWidth: 24
                implicitHeight: 24
            }
        }
    }

    Label {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 12
        visible: scanController.errorMessage.length > 0
        text: scanController.errorMessage
        color: "#ff8080"
        wrapMode: Text.WordWrap
    }

    RowLayout {
        anchors.fill: parent
        anchors.topMargin: scanController.errorMessage.length > 0 ? 40 : 0
        spacing: 0

        // Left pane: playlists.
        Pane {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            padding: 0

            ListView {
                id: playlistListView
                anchors.fill: parent
                clip: true
                model: ["All tracks"].concat(scanController.playlistNames)
                currentIndex: root.selectedPlaylistIndex

                delegate: ItemDelegate {
                    width: ListView.view.width
                    text: modelData
                    highlighted: ListView.isCurrentItem
                    onClicked: {
                        root.selectedPlaylistIndex = index;
                        scanController.filterByPlaylist(index === 0 ? "" : modelData);
                    }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            color: "#333"
        }

        // Right pane: tracks in the selected playlist (or all tracks).
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 8
                Label { text: ""; Layout.preferredWidth: 48 }
                Label { text: "Title"; font.bold: true; Layout.fillWidth: true }
                Label { text: "Key"; font.bold: true; Layout.preferredWidth: 50 }
                Label { text: "BPM"; font.bold: true; Layout.preferredWidth: 50 }
                Label { text: "Time"; font.bold: true; Layout.preferredWidth: 60 }
                Label { text: "Cues"; font.bold: true; Layout.preferredWidth: 50 }
                Label { text: "Plays"; font.bold: true; Layout.preferredWidth: 50 }
            }

            ListView {
                id: trackListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: scanController.tracks

                delegate: ItemDelegate {
                    width: ListView.view.width
                    height: 56

                    required property string sourceId
                    required property string title
                    required property string artist
                    required property double durationSeconds
                    required property int cueCount
                    required property int playCount
                    required property string filePath
                    required property string artworkPath
                    required property double bpm
                    required property string key

                    onClicked: playbackController.load(root.format, root.path, sourceId, filePath, title, artist, artworkPath)

                    contentItem: RowLayout {
                        spacing: 8

                        Rectangle {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            color: "#2a2a2a"
                            Image {
                                anchors.fill: parent
                                visible: artworkPath.length > 0
                                source: artworkPath.length > 0 ? "file://" + artworkPath : ""
                                fillMode: Image.PreserveAspectCrop
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Label {
                                text: title
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: artist
                                color: "#999"
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        Label { text: key.length > 0 ? key : "--"; Layout.preferredWidth: 50 }
                        Label { text: bpm > 0 ? bpm.toFixed(1) : "--"; Layout.preferredWidth: 50 }
                        Label { text: root.formatDuration(durationSeconds); Layout.preferredWidth: 60 }
                        Label { text: cueCount; Layout.preferredWidth: 50 }
                        Label { text: playCount >= 0 ? playCount : "--"; Layout.preferredWidth: 50 }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: trackListView.count === 0 && !scanController.busy
                    text: "No tracks found."
                    color: "gray"
                }
            }

            Label {
                Layout.margins: 8
                text: trackListView.count + " tracks"
                color: "gray"
            }
        }
    }
}
