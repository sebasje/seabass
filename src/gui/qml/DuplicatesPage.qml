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

    DuplicatesController {
        id: duplicatesController
    }

    Component.onCompleted: duplicatesController.scan(root.format, root.currentPath())
    onFormatChanged: duplicatesController.scan(root.format, root.currentPath())

    header: ToolBar {
        // See ScanPage.qml's header for why this is needed: a ColumnLayout
        // child sized via anchors.fill doesn't feed its implicit size back
        // up, so without this the second row renders past the ToolBar's
        // bottom edge instead of the ToolBar growing to fit it.
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
                    enabled: !duplicatesController.writing

                    ToolTip.visible: hovered

                    ToolTip.text: duplicatesController.writing
                        ? "Wait for the write to finish before leaving this page" : "Back"
                    onClicked: root.StackView.view.pop()
                }
                Label {
                    text: root.stickLabel + " -- Duplicate Tracks"
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
                    visible: duplicatesController.busy
                    indeterminate: duplicatesController.scanTotal === 0
                    value: duplicatesController.scanTotal > 0
                        ? duplicatesController.scanCurrent / duplicatesController.scanTotal : 0
                    Layout.preferredWidth: 140
                }
                Label {
                    visible: duplicatesController.busy && duplicatesController.scanTotal > 0
                    text: duplicatesController.scanCurrent + " / " + duplicatesController.scanTotal
                    color: Theme.textMuted
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Label {
                    text: plansListView.count + " duplicate group(s) need attention"
                    color: Theme.textMuted
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: "Apply All Fixable"
                    enabled: !duplicatesController.busy
                    ToolTip.visible: hovered
                    ToolTip.text: "Copy cues onto every unambiguous duplicate in one go -- conflicts are left for you to resolve individually"
                    onClicked: duplicatesController.applyAllUnambiguous()
                }
                Button {
                    text: "Undo"
                    visible: duplicatesController.canUndo
                    enabled: !duplicatesController.busy
                    ToolTip.visible: hovered
                    ToolTip.text: "Revert the last consolidation -- restores every file it touched to what it was before"
                    onClicked: duplicatesController.undoLastOperation()
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        StickWriteWarning {
            visible: duplicatesController.writing
            text: "Writing cues to the stick -- do not remove it until this finishes."
        }

        Label {
            visible: duplicatesController.errorMessage.length > 0
            text: duplicatesController.errorMessage
            color: Theme.danger
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            visible: duplicatesController.statusMessage.length > 0
            text: duplicatesController.statusMessage
            color: Theme.good
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
                    hoverEnabled: true
                    onClicked: delegateRoot.expanded = !delegateRoot.expanded

                    ToolTip.visible: hovered
                    ToolTip.text: delegateRoot.filename

                    contentItem: ColumnLayout {
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: delegateRoot.tracks.length > 0
                                    ? (delegateRoot.tracks[0].title + " -- " + delegateRoot.tracks[0].artist)
                                    : delegateRoot.filename
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.preferredWidth: 320
                            }
                            Label {
                                text: delegateRoot.kind === "unambiguous" ? "  (fixable)" : "  (conflict)"
                                color: delegateRoot.kind === "unambiguous" ? Theme.good : Theme.conflictText
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: "Copy Cues"
                                visible: delegateRoot.actionable
                                enabled: !duplicatesController.busy
                                ToolTip.visible: hovered
                                ToolTip.text: "Copy the one copy's cues onto every other copy of this track"
                                onClicked: duplicatesController.applyOne(delegateRoot.index)
                            }
                            Label {
                                text: delegateRoot.expanded ? "▾" : "▸"
                                color: Theme.textMuted
                            }
                        }
                        Label { text: delegateRoot.description; color: Theme.textMuted }
                    }
                }

                // Groups this duplicate set's copies visually -- without a
                // shared border, several stacked expanded groups (each a
                // Repeater of per-track Frames) read as one long undifferentiated
                // list rather than distinct sets of the same track.
                Rectangle {
                    width: parent.width
                    visible: delegateRoot.expanded
                    height: delegateRoot.expanded ? groupColumn.implicitHeight + 16 : 0
                    color: Theme.groupBackground
                    border.color: Theme.borderSubtle
                    radius: 4

                    ColumnLayout {
                        id: groupColumn
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8

                    // The "meta track" this whole frame is about -- the
                    // logical song every copy below is a physical instance
                    // of. Without this, the frame just contains a bare list
                    // of copies with nothing tying them together as one
                    // group.
                    Label {
                        Layout.fillWidth: true
                        text: (delegateRoot.tracks.length > 0
                            ? delegateRoot.tracks[0].title + " -- " + delegateRoot.tracks[0].artist
                            : delegateRoot.filename)
                            + "  (" + delegateRoot.tracks.length + " copies)"
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
                                    Label { text: modelData.title + "  (id=" + modelData.sourceId + ")"; font.bold: true }
                                    Item { Layout.fillWidth: true }
                                    Button {
                                        text: "Copy"
                                        visible: delegateRoot.kind === "conflict"
                                        enabled: modelData.cues.length > 0 && !duplicatesController.busy
                                        ToolTip.visible: hovered
                                        ToolTip.text: "Copy this track's cues to the other copies of the same track"
                                        onClicked: duplicatesController.copyFromTrack(delegateRoot.index, modelData.sourceId)
                                    }
                                    Button {
                                        text: "▶ Play"
                                        enabled: modelData.filePath.length > 0
                                        ToolTip.visible: hovered
                                        ToolTip.text: "Preview this copy's audio and cues"
                                        onClicked: root.playbackController.load(root.format, root.currentPath(), modelData.sourceId,
                                            modelData.filePath, modelData.title, modelData.artist, modelData.artworkPath,
                                            modelData.cues)
                                    }
                                }
                                Label {
                                    text: modelData.playlists.length > 0
                                        ? "Playlists: " + modelData.playlists.join(", ")
                                        : "Playlists: (none)"
                                    color: Theme.textMuted
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
                color: Theme.textMuted
            }
        }
    }
}
