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

    SyncController {
        id: syncController
    }

    // Original/generic labels, not either company's real branding -- same
    // convention as every other catalog badge/glyph in this app.
    function formatLabel(format) {
        if (format === "engine") return "Engine";
        if (format === "onelibrary") return "OneLibrary";
        return "DeviceLibrary";
    }
    function pathForFormat(format) {
        return format === "engine" ? root.enginePath : root.rekordboxPath;
    }

    // A conflict's own title/artist alone can't tell two conflicts on
    // duplicate copies of the same track apart (same title, same
    // artist, different file) -- appending the actual filename makes
    // "this title shown 5 times" legible as 5 distinct files instead
    // of looking like a bug.
    function conflictHeading(conflict) {
        var filename = conflict.targetPath.split("/").pop();
        if (conflict.targetTitle.length === 0) {
            return conflict.targetPath;
        }
        return conflict.targetTitle + " - " + conflict.targetArtist + "  [" + filename + "]";
    }

    Component.onCompleted: syncController.analyze(root.rekordboxPath, root.enginePath)

    header: ToolBar {
        // Opaque background override, see AppSettingsPage.qml's header
        // for why (KDE's Breeze style bleeds the window behind Seabass
        // through an unstyled ToolBar).
        background: Rectangle { color: Theme.surface }

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
            PageTitle {
                text: root.stickLabel + " · Sync Cue Points"
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
                ToolTip.text: "Revert the last sync - restores every file it touched to what it was before"
                onClicked: syncController.undoLastOperation()
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
            Repeater {
                model: syncController.directionCounts
                delegate: Label {
                    required property var modelData
                    text: "Copy cues to " + root.formatLabel(modelData.targetFormat) + " from "
                        + root.formatLabel(modelData.sourceFormat) + " for " + modelData.count + " track(s)."
                    wrapMode: Text.WordWrap
                }
            }
            Label {
                visible: {
                    for (var i = 0; i < syncController.directionCounts.length; i++) {
                        if (syncController.directionCounts[i].targetFormat === "rekordbox") return true;
                    }
                    return false;
                }
                text: "DeviceLibrary writing is the least-proven part of Seabass. Verify the result\non real hardware before trusting it for a gig."
                color: Theme.conflictText
                wrapMode: Text.WordWrap
            }
            Label {
                text: "Every catalog involved is backed up before anything is written. Once this finishes, "
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
            text: "Writing cues to the stick. Do not remove it until this finishes."
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
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: syncController.unresolvedConflicts.length > 0

            Label {
                text: "Unresolved conflicts (" + syncController.unresolvedConflicts.length + ")"
                font.bold: true
                color: Theme.warnText
            }

            // Bounded and independently scrollable (BigScrollBar, same as
            // every other list in this app -- plansListView below gets
            // the same treatment) rather than growing the whole page
            // unboundedly: with many conflicts this would otherwise push
            // the plan list, and eventually the page itself, past the
            // window with no way to reach the rest.
            ListView {
                id: conflictListView
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 420)
                clip: true
                spacing: 8
                model: syncController.unresolvedConflicts
                ScrollBar.vertical: BigScrollBar {}

                delegate: Rectangle {
                    id: conflictCard
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    color: Theme.warnBg
                    border.color: Theme.warnBorder
                    radius: 4
                    height: conflictColumn.implicitHeight + 16

                    ColumnLayout {
                        id: conflictColumn
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 6

                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.warnText
                            font.bold: true
                            text: root.conflictHeading(conflictCard.modelData)
                                + "  (" + root.formatLabel(conflictCard.modelData.targetFormat) + " needs cues, but "
                                + root.formatLabel(conflictCard.modelData.sourceAFormat) + " and "
                                + root.formatLabel(conflictCard.modelData.sourceBFormat) + " disagree)"
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            // Same investigation tools (waveform with cue
                            // markers, Play, CueFallbackNotice) the
                            // ordinary plan list below already gives each
                            // side of a match -- a conflict deserves the
                            // same look, not just a one-line summary.
                            Repeater {
                                model: [
                                    { track: conflictCard.modelData.sourceATrack, format: conflictCard.modelData.sourceAFormat,
                                      summary: conflictCard.modelData.sourceASummary, hasJunkCue: conflictCard.modelData.sourceAHasJunkCue, useSourceA: true },
                                    { track: conflictCard.modelData.sourceBTrack, format: conflictCard.modelData.sourceBFormat,
                                      summary: conflictCard.modelData.sourceBSummary, hasJunkCue: conflictCard.modelData.sourceBHasJunkCue, useSourceA: false }
                                ]
                                delegate: TrackWaveformCard {
                                    required property var modelData
                                    track: modelData.track
                                    formatLabelText: root.formatLabel(modelData.format) + " (" + modelData.summary + ")"
                                    actionButtonText: "Use this"
                                    actionButtonTooltip: "Apply (and overwrite) these cue points to the other track"
                                    onActionTriggered: syncController.resolveConflict(conflictCard.index, modelData.useSourceA)
                                    hintText: modelData.hasJunkCue
                                        ? "This side has a 0:00 memory cue that's usually accidental - consider cleaning it up in Clean Up before deciding."
                                        : ""
                                    playbackController: root.playbackController
                                    playbackPath: root.pathForFormat(modelData.track.side)
                                }
                            }
                        }
                    }
                }
            }
        }

        Label {
            visible: !syncController.busy
            text: "DeviceLibrary tracks: " + syncController.rekordboxTrackCount
                + "   Engine tracks: " + syncController.engineTrackCount
                + (syncController.oneLibraryTrackCount > 0 ? "   OneLibrary tracks: " + syncController.oneLibraryTrackCount : "")
                + "   needing sync: " + plansListView.count
            color: Theme.textMuted
        }

        ListView {
            id: plansListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: syncController.plans
            spacing: 4
            ScrollBar.vertical: BigScrollBar {}

            delegate: Column {
                id: delegateRoot
                width: ListView.view.width
                spacing: 4

                required property int index
                required property string sourceFormat
                required property string targetFormat
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
                                text: root.formatLabel(delegateRoot.sourceFormat) + " -> " + root.formatLabel(delegateRoot.targetFormat)
                                font.bold: true
                                color: Theme.good
                                Layout.preferredWidth: 160
                            }
                            Label {
                                text: delegateRoot.tracks.length > 0
                                    ? (delegateRoot.tracks[0].title + " - " + delegateRoot.tracks[0].artist)
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
                        Label {
                            text: delegateRoot.description
                            color: Theme.textMuted
                            leftPadding: 168
                        }
                    }
                }

                // Groups the two sides' copies of this track visually -
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
                    // of, ties the pair together as one group rather than
                    // two unrelated-looking Rekordbox/Engine frames.
                    Label {
                        Layout.fillWidth: true
                        text: (delegateRoot.tracks.length > 0
                            ? delegateRoot.tracks[0].title + " - " + delegateRoot.tracks[0].artist
                            : delegateRoot.filename)
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Repeater {
                        model: delegateRoot.tracks
                        delegate: TrackWaveformCard {
                            required property var modelData
                            track: modelData
                            formatLabelText: root.formatLabel(modelData.side)
                            playbackController: root.playbackController
                            playbackPath: root.pathForFormat(modelData.side)
                        }
                    }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: plansListView.count === 0 && !syncController.busy
                text: "Nothing to sync. Matched tracks' cues are already consistent."
                color: Theme.textMuted
            }
        }
    }

    BusyOverlay {
        anchors.fill: parent
        busy: syncController.busy
        current: syncController.scanCurrent
        total: syncController.scanTotal
        label: syncController.writing ? "Syncing cues..." : "Scanning for sync differences..."
    }
}
