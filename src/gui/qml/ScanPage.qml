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

    ScanController {
        id: scanController
    }

    property int selectedPlaylistIndex: 0

    function rescan() {
        selectedPlaylistIndex = 0;
        scanController.scan(root.format, root.path, root.format === "engine" ? root.rekordboxPath : "");
    }

    Component.onCompleted: rescan()
    onFormatChanged: rescan()

    function formatDuration(seconds) {
        var total = Math.round(seconds);
        var m = Math.floor(total / 60);
        var s = total % 60;
        return m + ":" + (s < 10 ? "0" : "") + s;
    }

    header: ToolBar {
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                ToolButton {
                    text: "< Back"
                    onClicked: root.StackView.view.pop()
                }
                Label {
                    text: root.stickLabel + " -- Library"
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
                    visible: scanController.busy
                    indeterminate: scanController.scanTotal === 0
                    value: scanController.scanTotal > 0
                        ? scanController.scanCurrent / scanController.scanTotal : 0
                    Layout.preferredWidth: 140
                }
                Label {
                    visible: scanController.busy && scanController.scanTotal > 0
                    text: scanController.scanCurrent + " / " + scanController.scanTotal
                    color: "gray"
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                TextField {
                    id: searchField
                    Layout.preferredWidth: 260
                    placeholderText: "Search title or artist..."
                    onTextChanged: scanController.search(text)
                }
                Item { Layout.fillWidth: true }
                Label { text: "Sort by" }
                ComboBox {
                    id: sortCombo
                    Layout.preferredWidth: 140
                    textRole: "text"
                    valueRole: "value"
                    model: [
                        { text: "Playlist Order", value: "playlist" },
                        { text: "Title", value: "title" },
                        { text: "Artist", value: "artist" },
                        { text: "Key", value: "key" },
                        { text: "BPM", value: "bpm" },
                        { text: "Duration", value: "duration" },
                        { text: "Cues", value: "cues" },
                        { text: "Plays", value: "plays" },
                    ]
                    onActivated: scanController.setSort(currentValue, sortDirectionButton.checked)
                }
                ToolButton {
                    id: sortDirectionButton
                    checkable: true
                    checked: true
                    text: checked ? "▲ Ascending" : "▼ Descending"
                    onCheckedChanged: scanController.setSort(sortCombo.currentValue, checked)
                }
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
                    required property var cues

                    onClicked: playbackController.load(root.format, root.path, sourceId, filePath, title, artist, artworkPath, cues)

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
