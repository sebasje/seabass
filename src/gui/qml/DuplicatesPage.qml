import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

Page {
    id: root
    required property string stickLabel
    required property string rekordboxPath
    required property string enginePath
    required property var playbackController
    required property var appSettingsController

    readonly property bool hasRekordbox: rekordboxPath.length > 0
    readonly property bool hasEngine: enginePath.length > 0
    readonly property bool hasOneLibrary: root.hasRekordbox && duplicatesController.hasOneLibrary(root.rekordboxPath)
    // OneLibrary selection is deliberately page-local, not persisted into
    // appSettingsController.preferredFormat -- that setting is shared
    // with ScanPage/LocalCuePage (see FormatToggle.qml's own comment),
    // neither of which know what to do with "onelibrary" as a value.
    // Rekordbox/Engine still persist exactly as before.
    property string localFormatOverride: ""
    readonly property string format: {
        if (root.localFormatOverride.length > 0) return root.localFormatOverride;
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
        // OneLibrary shares rekordbox's own PIONEER root -- exportLibrary.db
        // lives alongside export.pdb there.
        return root.format === "engine" ? root.enginePath : root.rekordboxPath;
    }
    function formatLabel(format) { return FormatLabels.label(format); }

    // "3 hot cue(s), 1 memory cue(s)" -- mirrors sync_controller.cpp's
    // own summarizeCueCounts() (no loop/non-loop split available here:
    // duplicates_controller.cpp's TracksRole cue map doesn't carry an
    // isLoop flag the way SyncController's does).
    function cueSummary(track) {
        var hot = 0, memory = 0;
        var cues = track.cues || [];
        for (var i = 0; i < cues.length; i++) {
            (cues[i].kind === "hot" ? hot++ : memory++);
        }
        var parts = [];
        if (hot > 0) parts.push(hot + " hot cue(s)");
        if (memory > 0) parts.push(memory + " memory cue(s)");
        return parts.length > 0 ? parts.join(", ") : "no cues";
    }

    // The actual conflicting options, one line per copy -- what the
    // Conflict badge's tooltip shows instead of just "these disagree,"
    // so the choice is legible from the badge alone.
    function conflictDetail(tracks) {
        var lines = [];
        for (var i = 0; i < tracks.length; i++) {
            var t = tracks[i];
            var label = t.filePath && t.filePath.length > 0 ? t.filePath.split("/").pop() : ("Copy " + (i + 1));
            lines.push(label + ": " + root.cueSummary(t));
        }
        return lines.join("\n");
    }

    DuplicatesController {
        id: duplicatesController
    }

    Component.onCompleted: duplicatesController.scan(root.format, root.currentPath())
    onFormatChanged: duplicatesController.scan(root.format, root.currentPath())

    header: ToolBar {
        // Opaque background override -- see AppSettingsPage.qml's header
        // for why (KDE's Breeze style bleeds the window behind Seabass
        // through an unstyled ToolBar).
        background: Rectangle { color: Theme.surface }

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
                BackBreadcrumb {
                    middleLabel: "Clean-up and Housekeeping"
                    title: "Duplicate Stats & Sync"
                    backEnabled: !duplicatesController.writing
                    onHomeRequested: root.StackView.view.pop(null)
                    onBackRequested: root.StackView.view.pop()
                }
                Item { Layout.fillWidth: true }
                LibrarySourceToggle {
                    current: root.format
                    hasRekordbox: root.hasRekordbox
                    hasEngine: root.hasEngine
                    hasOneLibrary: root.hasOneLibrary
                    onSourceRequested: (value) => {
                        if (value === "onelibrary") {
                            root.localFormatOverride = "onelibrary";
                        } else {
                            root.localFormatOverride = "";
                            root.appSettingsController.preferredFormat = value;
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Label {
                    text: plansListView.count + " duplicate group(s) need attention"
                    color: Theme.textMuted
                }
                Label {
                    visible: plansListView.count > 0
                    text: "(" + duplicatesController.totalWastedBytesHuman + " could be freed if each were on the stick once)"
                    color: Theme.textMuted
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: "Apply All Fixable"
                    enabled: !duplicatesController.busy
                    ToolTip.visible: hovered
                    ToolTip.text: "Copy cues onto every unambiguous duplicate in one go; conflicts are left for you to resolve individually"
                    onClicked: duplicatesController.applyAllUnambiguous()
                }
                Button {
                    text: "Undo"
                    visible: duplicatesController.canUndo
                    enabled: !duplicatesController.busy
                    ToolTip.visible: hovered
                    ToolTip.text: "Revert the last consolidation: restores every file it touched to what it was before"
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
            text: "Writing cues to the stick. Do not remove it until this finishes."
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

            ScrollBar.vertical: BigScrollBar {}

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
                required property string wastedBytesDescription

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
                                    ? (delegateRoot.tracks[0].title + " - " + delegateRoot.tracks[0].artist)
                                    : delegateRoot.filename
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.preferredWidth: 320
                            }
                            StatusBadge {
                                label: delegateRoot.kind === "unambiguous" ? "Fixable" : "Conflict"
                                badgeColor: delegateRoot.kind === "unambiguous" ? Theme.good : Theme.conflictText
                                tooltipText: delegateRoot.actionable
                                    ? "Conserved: only cues are copied onto the copies missing them. Files, playlists and other metadata are untouched."
                                    : "These copies disagree, so nothing is copied automatically; decide per-track with the "
                                      + "Copy buttons below.\n\n" + root.conflictDetail(delegateRoot.tracks)
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
                                font.pointSize: Theme.fontHuge
                                font.bold: true
                                color: Theme.textMuted
                            }
                        }
                        Label { text: delegateRoot.description; color: Theme.textMuted }
                        Label { text: delegateRoot.wastedBytesDescription; color: Theme.textMuted }
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
                            ? delegateRoot.tracks[0].title + " - " + delegateRoot.tracks[0].artist
                            : delegateRoot.filename)
                            + "  (" + delegateRoot.tracks.length + " copies)"
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Repeater {
                        model: delegateRoot.tracks
                        delegate: TrackWaveformCard {
                            required property var modelData
                            track: modelData
                            formatLabelText: root.formatLabel(root.format)
                            showPlaylists: true
                            actionButtonText: delegateRoot.kind === "conflict" ? "Copy" : ""
                            actionButtonTooltip: delegateRoot.kind === "conflict"
                                ? "Copy this copy's cue points onto the other copy" : ""
                            actionButtonEnabled: modelData.cues.length > 0 && !duplicatesController.busy
                            onActionTriggered: duplicatesController.copyFromTrack(delegateRoot.index, modelData.sourceId)
                            playbackController: root.playbackController
                            playbackPath: root.currentPath()
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

    BusyOverlay {
        anchors.fill: parent
        busy: duplicatesController.busy
        current: duplicatesController.scanCurrent
        total: duplicatesController.scanTotal
        label: duplicatesController.writing ? "Writing cues..." : "Scanning for duplicates..."
    }
}
