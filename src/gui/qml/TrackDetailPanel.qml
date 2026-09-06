import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// The track details column on the Library page: a resizable, closable
// SplitView pane to the right of the track list (the same idiom as the
// Matching panel), opened by clicking a row. Holds what used to be the
// track info popup: title / artist, the waveform with its cue markers
// (click to add a cue, drag to add a loop), the add-cue form, and the
// playlists the track is in. A proper redesign of the track details is
// still to come; this is the popup's content given a home that does not
// block the list. showFor() is populated from a track delegate; the
// waveform is read on demand right there (never during the bulk scan
// that fills the list), same pattern playbackController.load() uses.
Pane {
    id: panel
    required property var addCueController
    required property var playbackController
    property string format: ""
    property string libraryPath: ""
    signal closeRequested()
    // The page's track list is refreshed after a cue is added, so it is
    // not showing stale cue counts.
    signal rescanRequested()

    padding: 0

    function formatDuration(seconds) {
        var total = Math.round(seconds);
        var m = Math.floor(total / 60);
        var s = total % 60;
        return m + ":" + (s < 10 ? "0" : "") + s;
    }

    property string trackSourceId: ""
    property string trackTitle: ""
    property string trackArtist: ""
    property var trackCues: []
    property double trackDurationMs: 0
    property var trackPlaylistNames: []
    property string trackStreamingSource: ""
    // -1 means no pending Add-Cue form; set by clicking the
    // waveform below.
    property real pendingPositionMs: -1
    // -1 means the pending add is a plain cue; a real value
    // (from dragging out a range) means it's a loop, and
    // pendingPositionMs is the loop-in.
    property real pendingLoopEndMs: -1

    function showFor(delegate) {
        panel.trackSourceId = delegate.sourceId;
        panel.trackTitle = delegate.title;
        panel.trackArtist = delegate.artist;
        panel.trackCues = delegate.cues;
        panel.trackDurationMs = delegate.durationSeconds * 1000;
        panel.trackPlaylistNames = delegate.playlistNames;
        panel.trackStreamingSource = delegate.streamingSource;
        panel.pendingPositionMs = -1;
        panel.pendingLoopEndMs = -1;
        waveformView.waveformData = playbackController.waveformFor(
            panel.format, panel.libraryPath, delegate.sourceId);
        waveformView.format = panel.format;
    }

    // Cues staged for this track and not on the stick yet (see
    // AddCueController: adding stages, the floating Save writes).
    readonly property var pendingCues: {
        var revision = panel.addCueController.pendingRevision;  // re-evaluate when it bumps
        if (typeof panel.addCueController.pendingCuesFor !== "function" || panel.trackSourceId.length === 0) {
            return [];
        }
        return panel.addCueController.pendingCuesFor(panel.trackSourceId);
    }

    // Once a cue is staged, the panel's own trackCues (a snapshot from
    // when the track was shown) is patched locally so the new marker
    // shows up on the waveform immediately; the page's track list is
    // refreshed only once a save has actually written something, so it
    // never shows cue counts the stick does not have yet.
    Connections {
        // A plain JS stand-in (tests) has no signals and is not a QObject.
        target: ("objectName" in panel.addCueController) ? panel.addCueController : null
        ignoreUnknownSignals: true
        function onCuesSaved() {
            panel.rescanRequested();
        }
        function onStatusMessageChanged() {
            if (addCueController.statusMessage.length === 0) {
                return;
            }
            var isLoopAdded = panel.pendingLoopEndMs >= 0;
            var isHot = isLoopAdded || cueKindCombo.currentIndex === 0;
            var cues = panel.trackCues.slice();
            if (isHot) {
                cues = cues.filter((c) => !(c.kind === "hot" && c.hotCueNumber === hotCueNumberSpin.value));
            }
            cues.push({
                kind: isHot ? "hot" : "memory",
                hotCueNumber: isHot ? hotCueNumberSpin.value : 0,
                positionMs: panel.pendingPositionMs,
                isLoop: isLoopAdded,
                loopEndMs: isLoopAdded ? panel.pendingLoopEndMs : 0,
                color: isLoopAdded ? "#3daee9" : (isHot ? "#ffcc00" : "#00a5e3"),
                comment: cueCommentField.text,
            });
            panel.trackCues = cues;
            panel.pendingPositionMs = -1;
            panel.pendingLoopEndMs = -1;
            cueCommentField.text = "";
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 8
            Label {
                text: "Track"
                font.family: Theme.titleFamily
                font.weight: Font.Bold
                font.pointSize: Theme.fontMedium
                Layout.fillWidth: true
            }
            ToolButton {
                objectName: "closeTrackPanelButton"
                text: "✕"
                ToolTip.visible: hovered
                ToolTip.text: "Close track details"
                onClicked: panel.closeRequested()
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.borderSubtle }

        PageScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 12

    ColumnLayout {
        width: parent.width
        spacing: 10

        ColumnLayout {
            spacing: 1
            Label {
                text: panel.trackTitle
                font.bold: true
                font.pointSize: Theme.fontMedium
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Label {
                text: panel.trackArtist
                color: Theme.textMuted
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }

        Label {
            visible: panel.trackStreamingSource.length > 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Streaming source: " + panel.trackStreamingSource
                + " - no local file; playback, merging, and adding cues aren't available for this track."
            color: Theme.textMuted
        }

        WaveformView {
            id: waveformView
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            cueData: panel.trackCues
            trackDurationMs: panel.trackDurationMs
            progress: -1
            cueEditable: true
            onPositionClicked: (ms) => {
                if (panel.trackStreamingSource.length > 0) {
                    return;
                }
                panel.pendingPositionMs = ms;
                panel.pendingLoopEndMs = -1;
            }
            onLoopRangeSelected: (startMs, endMs) => {
                if (panel.trackStreamingSource.length > 0) {
                    return;
                }
                panel.pendingPositionMs = startMs;
                panel.pendingLoopEndMs = endMs;
                // Loops always need a hot slot -- see the
                // combo's own visibility below.
                cueKindCombo.currentIndex = 0;
            }
        }

        Label {
            visible: panel.pendingPositionMs < 0 && panel.trackStreamingSource.length === 0
            text: "Click the waveform above to add a cue there, or drag to add a loop."
            color: Theme.textMuted
            font.pointSize: Theme.fontSmall
        }

        // Position-only for now, no beatgrid snap, see
        // AddCueController's own class comment for why.
        ColumnLayout {
            id: addCueForm
            visible: panel.pendingPositionMs >= 0
            Layout.fillWidth: true
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    font.bold: true
                    text: panel.pendingLoopEndMs >= 0
                        ? "Add loop " + panel.formatDuration(panel.pendingPositionMs / 1000)
                            + " to " + panel.formatDuration(panel.pendingLoopEndMs / 1000)
                        : "Add cue at " + panel.formatDuration(panel.pendingPositionMs / 1000)
                }
                Item { Layout.fillWidth: true }
                // Loops always occupy a hot slot -- same
                // constraint the hardware itself has, no
                // memory-loop equivalent exists to offer here.
                Label { text: "Kind:"; visible: panel.pendingLoopEndMs < 0 }
                ComboBox {
                    id: cueKindCombo
                    model: ["Hot", "Memory"]
                    Layout.preferredWidth: 110
                    visible: panel.pendingLoopEndMs < 0
                }
                Label {
                    text: "Slot:"
                    visible: panel.pendingLoopEndMs >= 0 || cueKindCombo.currentIndex === 0
                }
                SpinBox {
                    id: hotCueNumberSpin
                    visible: panel.pendingLoopEndMs >= 0 || cueKindCombo.currentIndex === 0
                    from: 1
                    to: 8
                    value: 1
                }
            }
            TextField {
                id: cueCommentField
                Layout.fillWidth: true
                placeholderText: "Comment (optional)"
            }
            Label {
                // AddCueController refuses this too (belt and
                // suspenders), but saying so up front skips a
                // pointless rescan-then-fail round trip -- see
                // AddCueController's own class comment for why
                // rekordbox/OneLibrary loop writes are refused
                // rather than silently downgraded to a point.
                visible: panel.pendingLoopEndMs >= 0 && panel.format !== "engine"
                text: "Hot loops can only be added to Engine tracks right now."
                color: Theme.danger
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                visible: addCueController.errorMessage.length > 0
                text: addCueController.errorMessage
                color: Theme.danger
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                visible: addCueController.statusMessage.length > 0
                text: addCueController.statusMessage
                color: Theme.good
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                BusyIndicator {
                    visible: addCueController.busy
                    running: visible
                    implicitWidth: 20
                    implicitHeight: 20
                }
                Label {
                    visible: addCueController.busy
                    text: "Adding..."
                    color: Theme.textMuted
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: "Cancel"
                    enabled: !addCueController.busy
                    onClicked: {
                        panel.pendingPositionMs = -1;
                        panel.pendingLoopEndMs = -1;
                    }
                }
                Button {
                    text: panel.pendingLoopEndMs >= 0 ? "Add Loop" : "Add Cue"
                    enabled: !addCueController.busy
                        && !(panel.pendingLoopEndMs >= 0 && panel.format !== "engine")
                    onClicked: {
                        var isLoop = panel.pendingLoopEndMs >= 0;
                        var isHot = isLoop || cueKindCombo.currentIndex === 0;
                        addCueController.addCue(panel.format, panel.libraryPath,
                            panel.trackSourceId, panel.pendingPositionMs,
                            isHot ? "hot" : "memory", hotCueNumberSpin.value,
                            isLoop ? "#3daee9" : (isHot ? "#ffcc00" : "#00a5e3"), cueCommentField.text,
                            isLoop, isLoop ? panel.pendingLoopEndMs : 0, panel.trackTitle);
                    }
                }
            }
        }

        // What this track has staged and not saved yet.
        ColumnLayout {
            visible: panel.pendingCues.length > 0
            Layout.fillWidth: true
            spacing: 4
            Label {
                text: "Unsaved on this track"
                font.bold: true
                color: Theme.warnText
                font.pointSize: Theme.fontSmall
            }
            Repeater {
                model: panel.pendingCues
                delegate: RowLayout {
                    id: pendingRow
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        color: Theme.text
                        text: (pendingRow.modelData.isLoop ? "Hot loop " + pendingRow.modelData.hotCueNumber
                            : pendingRow.modelData.kind === "hot" ? "Hot cue " + pendingRow.modelData.hotCueNumber
                            : "Memory cue")
                            + " at " + panel.formatDuration(pendingRow.modelData.positionMs / 1000)
                    }
                    ToolButton {
                        text: "Undo"
                        enabled: !addCueController.writing
                        onClicked: addCueController.unstage(pendingRow.modelData.changeId)
                    }
                }
            }
        }

        Label {
            text: "Playlists"
            font.bold: true
            color: Theme.textMuted
            font.pointSize: Theme.fontSmall
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: panel.trackPlaylistNames.length > 0
                ? panel.trackPlaylistNames.join("\n")
                : "Not in any playlist"
        }
    }
        }
    }
}
