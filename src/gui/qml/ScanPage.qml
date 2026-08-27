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
    // A plain function, not a cached property: QML evaluates onFormatChanged
    // before a *dependent* property like a cached "path" has re-settled, so
    // reading a cached path here could see the previous format's value.
    // A function call is always evaluated fresh against the current format.
    function currentPath() {
        return root.format === "engine" ? root.enginePath : root.rekordboxPath;
    }

    ScanController {
        id: scanController
    }

    property int selectedPlaylistIndex: 0

    function rescan() {
        selectedPlaylistIndex = 0;
        scanController.scan(root.format, root.currentPath(), root.format === "engine" ? root.rekordboxPath : "");
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
        // A ColumnLayout child sized purely by anchors.fill doesn't feed its
        // own implicit size back up to the ToolBar, so without this the
        // ToolBar stays single-row tall and the second row of controls
        // renders past its bottom edge, overlapping the page content below.
        implicitHeight: headerLayout.implicitHeight + 20

        ColumnLayout {
            id: headerLayout
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                ToolButton {
                    text: "‹"

                    font.pointSize: Theme.fontHuge

                    ToolTip.visible: hovered

                    ToolTip.text: "Back"
                    onClicked: root.StackView.view.pop()
                }
                Label {
                    text: root.stickLabel + " -- Library"
                    font.bold: true
                    font.pointSize: Theme.fontLarge
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
                    color: Theme.textMuted
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
        color: Theme.danger
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

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                TextField {
                    id: playlistFilterField
                    Layout.fillWidth: true
                    Layout.margins: 6
                    placeholderText: "Filter playlists..."
                }

                ListView {
                    id: playlistListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    property var allNames: ["All tracks"].concat(scanController.playlistNames)
                    model: playlistFilterField.text.length === 0
                        ? allNames
                        : allNames.filter((n, i) => i === 0 || n.toLowerCase().includes(playlistFilterField.text.toLowerCase()))
                    currentIndex: root.selectedPlaylistIndex

                    delegate: ItemDelegate {
                        required property int index
                        required property string modelData
                        width: ListView.view.width
                        text: modelData + " (" + (index === 0
                            ? scanController.totalTrackCount
                            : (scanController.playlistTrackCounts[modelData] ?? 0)) + ")"
                        highlighted: ListView.isCurrentItem
                        onClicked: {
                            root.selectedPlaylistIndex = index;
                            scanController.filterByPlaylist(index === 0 ? "" : modelData);
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            color: Theme.borderSubtle
        }

        // Right pane: tracks in the selected playlist (or all tracks).
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            RowLayout {
                // Mirrors the track delegate's own RowLayout exactly (same
                // left/right inset, spacing and column widths) -- otherwise
                // these headers silently drift out of alignment with the
                // columns they're supposed to label.
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                Layout.topMargin: 4
                Layout.bottomMargin: 4
                spacing: 8
                Label { text: ""; Layout.preferredWidth: 40 }
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

                // A plain Rectangle, not an ItemDelegate -- overriding a
                // Material Control's `background:` property doesn't
                // reliably replace its own implicit chrome (see
                // StickListPage.qml's stick-card delegate for the same
                // fix and the fuller explanation); a bare Rectangle +
                // MouseArea sidesteps it entirely.
                delegate: Rectangle {
                    id: trackDelegate
                    width: ListView.view.width
                    height: 56

                    required property int index
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

                    // Alternating row shading -- makes it much easier to
                    // track a row across the wide, densely-columned list.
                    // Solid, muted colors rather than a translucent overlay,
                    // so the result doesn't depend on (and can't pick up an
                    // unexpected tint from) whatever's rendered underneath.
                    color: rowMouseArea.pressed ? Theme.rowPressed
                        : rowMouseArea.containsMouse ? Theme.rowHover
                        : (trackDelegate.index % 2 === 0 ? Theme.rowEven : Theme.rowOdd)

                    MouseArea {
                        id: rowMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: playbackController.load(root.format, root.currentPath(), trackDelegate.sourceId,
                            trackDelegate.filePath, trackDelegate.title, trackDelegate.artist, trackDelegate.artworkPath,
                            trackDelegate.cues)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8

                        Rectangle {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            color: Theme.surface
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
                                color: Theme.textMuted
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
                    color: Theme.textMuted
                }
            }

            Label {
                Layout.margins: 8
                text: trackListView.count + " tracks"
                color: Theme.textMuted
            }
        }
    }
}
