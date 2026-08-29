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
    // OneLibrary lives alongside export.pdb under the same PIONEER root
    // (see ScanController::hasOneLibrary()'s own doc comment) -- it's a
    // third view onto the DeviceLibrary side of the stick, not a
    // separately-stored DetectedStick path, so it needs hasRekordbox
    // (the path it reads from) rather than a path of its own.
    readonly property bool hasOneLibrary: root.hasRekordbox && scanController.hasOneLibrary(root.rekordboxPath)

    // Deliberately a plain property, not bound to appSettingsController.
    // preferredFormat (unlike every other page's FormatToggle) -- that
    // setting is a shared, persisted, binary Rekordbox/Engine choice used
    // by Clean Up, Sync, and Local Cue Backup, none of which understand a
    // third "onelibrary" value. Library-source browsing here is scoped to
    // just this page and doesn't persist across restarts.
    property string format: root.hasRekordbox ? "rekordbox" : "engine"
    // A plain function, not a cached property: QML evaluates onFormatChanged
    // before a *dependent* property like a cached "path" has re-settled, so
    // reading a cached path here could see the previous format's value.
    // A function call is always evaluated fresh against the current format.
    function currentPath() {
        // "onelibrary" reads from the same PIONEER root as "rekordbox" --
        // falls into this branch already, no separate case needed.
        return root.format === "engine" ? root.enginePath : root.rekordboxPath;
    }

    ScanController {
        id: scanController
    }

    // Backs the "Merge with..." picker below -- reuses CleanupController
    // wholesale (planManualMerge()/apply()) rather than a bespoke write
    // path, since a manual two-track merge is mechanically identical to
    // an auto-detected group's cleanup once the group exists.
    CleanupController {
        id: mergeController
    }

    // Backs the "click the waveform to add a cue" form in the track info
    // popup below.
    AddCueController {
        id: addCueController
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

    // One decimal place, but only when there actually is one -- "128"
    // reads better than "128.0" for the (very common) case of a whole-
    // number BPM, while a genuinely fractional one (e.g. a half-time
    // edit) still keeps its precision instead of getting rounded away.
    function formatBpm(bpm) {
        if (bpm <= 0) {
            return "--";
        }
        var oneDecimal = bpm.toFixed(1);
        return oneDecimal.endsWith(".0") ? oneDecimal.slice(0, -2) : oneDecimal;
    }

    header: ToolBar {
        // Opaque background override -- see AppSettingsPage.qml's header
        // for why (KDE's Breeze style bleeds the window behind Seabass
        // through an unstyled ToolBar).
        background: Rectangle { color: Theme.surface }

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
                LibrarySourceToggle {
                    current: root.format
                    hasRekordbox: root.hasRekordbox
                    hasEngine: root.hasEngine
                    hasOneLibrary: root.hasOneLibrary
                    onSourceRequested: (value) => root.format = value
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
                // 32 (info button) + 8 (row spacing) + 32 (merge button) --
                // both trailing ToolButtons in the delegate below, not
                // just one. Getting this narrower than the delegate's
                // real trailing content silently pushes every column
                // before it out of alignment (the fill spacer above ends
                // up absorbing a different amount of leftover space in
                // the header than in each row) -- exactly what happened
                // here before this comment existed.
                Label { text: ""; Layout.preferredWidth: 72 }
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
                    // Taller only for the currently playing row -- gives
                    // its inline waveform (below) room to actually be
                    // legible instead of a sliver.
                    height: trackDelegate.isPlaying ? 64 : 56

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
                    required property var playlistNames

                    readonly property bool isPlaying: playbackController.hasTrack
                        && playbackController.currentFormat === root.format
                        && playbackController.currentSourceId === trackDelegate.sourceId

                    // Alternating row shading -- makes it much easier to
                    // track a row across the wide, densely-columned list.
                    // Solid, muted colors rather than a translucent overlay,
                    // so the result doesn't depend on (and can't pick up an
                    // unexpected tint from) whatever's rendered underneath.
                    color: rowMouseArea.pressed ? Theme.rowPressed
                        : rowMouseArea.containsMouse ? Theme.rowHover
                        : (trackDelegate.index % 2 === 0 ? Theme.rowEven : Theme.rowOdd)

                    // Now-playing highlight -- an accent-colored stripe,
                    // same idiom as most media players use for "this one."
                    // Rounded to match every other accent-bordered highlight
                    // in the app (BackupsPage/LocalCuePage's active-field
                    // outline, etc.), all radius: 4 -- this one was square.
                    border.color: trackDelegate.isPlaying ? Theme.accent : "transparent"
                    border.width: trackDelegate.isPlaying ? 2 : 0
                    radius: trackDelegate.isPlaying ? 4 : 0

                    // The currently playing row's own waveform, with cue
                    // markers and live progress, used as a faded
                    // full-row backdrop rather than a discrete column --
                    // declared before (so it renders behind) the row's
                    // real content, and deliberately NOT part of the
                    // RowLayout below, so it can never affect column
                    // widths/alignment the way an inline version did.
                    // Its own internal seek MouseArea is inert here
                    // (rowMouseArea below sits on top and claims every
                    // click first) -- purely decorative; the PlayerBar's
                    // own waveform is still the real interactive one.
                    WaveformView {
                        visible: trackDelegate.isPlaying
                        anchors.fill: parent
                        anchors.margins: 2
                        opacity: 0.35
                        waveformData: playbackController.waveform
                        cueData: playbackController.cues
                        trackDurationMs: playbackController.duration
                        progress: playbackController.duration > 0
                            ? playbackController.position / playbackController.duration : 0
                    }

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
                                source: artworkPath
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

                        KeyBadge { keyName: key }
                        Label { text: root.formatBpm(bpm); Layout.preferredWidth: 50 }
                        Label { text: root.formatDuration(durationSeconds); Layout.preferredWidth: 60 }
                        Label { text: cueCount; Layout.preferredWidth: 50 }
                        Label { text: playCount >= 0 ? playCount : "--"; Layout.preferredWidth: 50 }
                        ToolButton {
                            text: "ⓘ"
                            Layout.preferredWidth: 32
                            ToolTip.visible: hovered
                            ToolTip.text: trackDelegate.playlistNames.length > 0
                                ? "Playlists:\n" + trackDelegate.playlistNames.join("\n")
                                : "Not in any playlist"
                            onClicked: trackInfoPopup.showFor(trackDelegate)
                        }
                        ToolButton {
                            text: "🔗"
                            Layout.preferredWidth: 32
                            enabled: root.format !== "onelibrary"
                            ToolTip.visible: hovered
                            ToolTip.text: root.format === "onelibrary"
                                ? "Merging isn't supported on OneLibrary yet -- switch to DeviceLibrary or Engine OS"
                                : "Merge with another track..."
                            onClicked: mergePickerPopup.showFor(trackDelegate)
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: trackListView.count === 0 && !scanController.busy
                    text: "No tracks found."
                    color: Theme.textMuted
                }
            }

            // One shared popup, reused for whichever row's (i) button was
            // clicked -- populated by showFor() rather than one Popup
            // instance per delegate. The waveform is read on demand right
            // here (never during the bulk scan that fills trackListView),
            // same pattern playbackController.load() already uses.
            Popup {
                id: trackInfoPopup
                modal: true
                focus: true
                x: (root.width - width) / 2
                y: (root.height - height) / 2
                width: 460

                property string trackSourceId: ""
                property string trackTitle: ""
                property string trackArtist: ""
                property var trackCues: []
                property double trackDurationMs: 0
                property var trackPlaylistNames: []
                // -1 means no pending Add-Cue form; set by clicking the
                // waveform below.
                property real pendingPositionMs: -1

                function showFor(delegate) {
                    trackInfoPopup.trackSourceId = delegate.sourceId;
                    trackInfoPopup.trackTitle = delegate.title;
                    trackInfoPopup.trackArtist = delegate.artist;
                    trackInfoPopup.trackCues = delegate.cues;
                    trackInfoPopup.trackDurationMs = delegate.durationSeconds * 1000;
                    trackInfoPopup.trackPlaylistNames = delegate.playlistNames;
                    trackInfoPopup.pendingPositionMs = -1;
                    waveformView.waveformData = playbackController.waveformFor(
                        root.format, root.currentPath(), delegate.sourceId);
                    trackInfoPopup.open();
                }

                // Once a cue is actually added, the popup's own trackCues
                // (a snapshot from when it was opened) is patched locally
                // so the new marker shows up on the waveform immediately,
                // and the page's track list is refreshed in the background
                // so it's not showing stale cue counts next time this
                // popup is reopened.
                Connections {
                    target: addCueController
                    function onStatusMessageChanged() {
                        if (addCueController.statusMessage.length === 0) {
                            return;
                        }
                        var cues = trackInfoPopup.trackCues.slice();
                        if (cueKindCombo.currentIndex === 0) {
                            cues = cues.filter((c) => !(c.kind === "hot" && c.hotCueNumber === hotCueNumberSpin.value));
                        }
                        cues.push({
                            kind: cueKindCombo.currentIndex === 0 ? "hot" : "memory",
                            hotCueNumber: cueKindCombo.currentIndex === 0 ? hotCueNumberSpin.value : 0,
                            positionMs: trackInfoPopup.pendingPositionMs,
                            color: cueKindCombo.currentIndex === 0 ? "#ffcc00" : "#00a5e3",
                        });
                        trackInfoPopup.trackCues = cues;
                        trackInfoPopup.pendingPositionMs = -1;
                        cueCommentField.text = "";
                        root.rescan();
                    }
                }

                ColumnLayout {
                    width: parent.width
                    spacing: 10

                    ColumnLayout {
                        spacing: 1
                        Label {
                            text: trackInfoPopup.trackTitle
                            font.bold: true
                            font.pointSize: Theme.fontMedium
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Label {
                            text: trackInfoPopup.trackArtist
                            color: Theme.textMuted
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }

                    WaveformView {
                        id: waveformView
                        Layout.fillWidth: true
                        Layout.preferredHeight: 80
                        cueData: trackInfoPopup.trackCues
                        trackDurationMs: trackInfoPopup.trackDurationMs
                        progress: -1
                        onPositionClicked: (ms) => {
                            if (root.format !== "onelibrary") {
                                trackInfoPopup.pendingPositionMs = ms;
                            }
                        }
                    }

                    Label {
                        visible: trackInfoPopup.pendingPositionMs < 0
                        text: root.format === "onelibrary"
                            ? "Adding cues isn't supported on OneLibrary yet -- switch to DeviceLibrary or Engine OS."
                            : "Click the waveform above to add a cue there."
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                        font.italic: true
                    }

                    // Position-only for now, no beatgrid snap -- see
                    // AddCueController's own class comment for why.
                    ColumnLayout {
                        id: addCueForm
                        visible: trackInfoPopup.pendingPositionMs >= 0 && root.format !== "onelibrary"
                        Layout.fillWidth: true
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                font.bold: true
                                text: "Add cue at " + root.formatDuration(trackInfoPopup.pendingPositionMs / 1000)
                            }
                            Item { Layout.fillWidth: true }
                            Label { text: "Kind:" }
                            ComboBox {
                                id: cueKindCombo
                                model: ["Hot", "Memory"]
                                Layout.preferredWidth: 110
                            }
                            Label { text: "Slot:"; visible: cueKindCombo.currentIndex === 0 }
                            SpinBox {
                                id: hotCueNumberSpin
                                visible: cueKindCombo.currentIndex === 0
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
                                onClicked: trackInfoPopup.pendingPositionMs = -1
                            }
                            Button {
                                text: "Add Cue"
                                enabled: !addCueController.busy
                                onClicked: addCueController.addCue(root.format, root.currentPath(),
                                    trackInfoPopup.trackSourceId, trackInfoPopup.pendingPositionMs,
                                    cueKindCombo.currentIndex === 0 ? "hot" : "memory", hotCueNumberSpin.value,
                                    cueKindCombo.currentIndex === 0 ? "#ffcc00" : "#00a5e3", cueCommentField.text)
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
                        text: trackInfoPopup.trackPlaylistNames.length > 0
                            ? trackInfoPopup.trackPlaylistNames.join("\n")
                            : "Not in any playlist"
                    }
                }
            }

            // Step 1 of "Merge with...": search for the second track.
            // Deliberately searches ScanController's full unfiltered
            // m_allTracks (via findMergeCandidates()), never the page's
            // own filtered/sorted `tracks` model or scanController.search()
            // -- typing here must not disturb whatever's shown on the page
            // underneath once this closes.
            Popup {
                id: mergePickerPopup
                modal: true
                focus: true
                x: (root.width - width) / 2
                y: (root.height - height) / 2
                width: 480
                height: 420

                property string trackASourceId: ""
                property string trackATitle: ""
                property string trackAArtist: ""

                function showFor(delegate) {
                    mergePickerPopup.trackASourceId = delegate.sourceId;
                    mergePickerPopup.trackATitle = delegate.title;
                    mergePickerPopup.trackAArtist = delegate.artist;
                    mergeSearchField.text = "";
                    mergeCandidatesList.model = [];
                    mergePickerPopup.open();
                    mergeSearchField.forceActiveFocus();
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "Merge “" + mergePickerPopup.trackATitle + " -- " + mergePickerPopup.trackAArtist + "” with:"
                        font.bold: true
                    }
                    TextField {
                        id: mergeSearchField
                        Layout.fillWidth: true
                        placeholderText: "Search title or artist..."
                        onTextChanged: mergeCandidatesList.model =
                            scanController.findMergeCandidates(text, mergePickerPopup.trackASourceId)
                    }
                    ListView {
                        id: mergeCandidatesList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: []
                        spacing: 2

                        ScrollBar.vertical: BigScrollBar {}

                        delegate: ItemDelegate {
                            width: ListView.view.width
                            required property var modelData

                            contentItem: ColumnLayout {
                                spacing: 1
                                Label {
                                    text: modelData.title + " -- " + modelData.artist
                                    font.bold: true
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: modelData.filePath
                                    color: Theme.textMuted
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                    font.pointSize: Theme.fontSmall
                                }
                            }
                            onClicked: {
                                var a = { sourceId: mergePickerPopup.trackASourceId, title: mergePickerPopup.trackATitle,
                                    artist: mergePickerPopup.trackAArtist };
                                mergePickerPopup.close();
                                mergeReviewPopup.showFor(a, modelData);
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: mergeSearchField.text.length === 0
                            text: "Type to search for the track to merge with."
                            color: Theme.textMuted
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: mergeSearchField.text.length > 0 && mergeCandidatesList.count === 0
                            text: "No matches."
                            color: Theme.textMuted
                        }
                    }
                    Button {
                        text: "Cancel"
                        Layout.alignment: Qt.AlignRight
                        onClicked: mergePickerPopup.close()
                    }
                }
            }

            // Step 2: review the plan (survivor, cues to merge, playlist
            // note) before actually applying -- exactly the same
            // DuplicateCleanupPlanner output and apply() path Clean Up
            // Duplicates uses for an auto-detected group, just seeded with
            // this one manually-chosen pair via planManualMerge().
            Popup {
                id: mergeReviewPopup
                modal: true
                focus: true
                x: (root.width - width) / 2
                y: (root.height - height) / 2
                width: 520

                property string trackALabel: ""
                property string trackBLabel: ""

                function showFor(trackA, trackB) {
                    mergeReviewPopup.trackALabel = trackA.title + " -- " + trackA.artist;
                    mergeReviewPopup.trackBLabel = trackB.title + " -- " + trackB.artist;
                    mergeController.planManualMerge(root.format, root.currentPath(), trackA.sourceId, trackB.sourceId);
                    mergeReviewPopup.open();
                }

                onClosed: root.rescan()

                ColumnLayout {
                    width: parent.width
                    spacing: 10

                    Label {
                        text: "Merge Tracks"
                        font.bold: true
                        font.pointSize: Theme.fontLarge
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        text: "“" + mergeReviewPopup.trackALabel + "” + “" + mergeReviewPopup.trackBLabel + "”"
                    }

                    RowLayout {
                        visible: mergeController.busy
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 8
                        BusyIndicator { running: mergeController.busy; implicitWidth: 24; implicitHeight: 24 }
                        Label { text: mergeController.writing ? "Merging..." : "Comparing tracks..."; color: Theme.textMuted }
                    }

                    Label {
                        visible: mergeController.errorMessage.length > 0
                        text: mergeController.errorMessage
                        color: Theme.danger
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: mergeController.statusMessage.length > 0
                        text: mergeController.statusMessage
                        color: Theme.good
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Repeater {
                        // statusMessage only ever gets set once apply()
                        // finishes -- once it's non-empty, hide the plan
                        // preview even though apply() -> onWriteFinished()
                        // triggers its own trailing rescan() that briefly
                        // repopulates `plans` with the full auto-detected
                        // list (the same refresh CleanupPage.qml relies on
                        // after every apply -- not worth special-casing
                        // away just for this dialog).
                        model: mergeController.statusMessage.length === 0
                            ? mergeController.plans : null

                        delegate: ColumnLayout {
                            id: planRow
                            Layout.fillWidth: true
                            spacing: 6

                            required property int index
                            required property var survivor
                            required property var toRemove
                            required property bool differs
                            required property string wastedBytesHuman
                            required property int newCueCount
                            required property bool included

                            RowLayout {
                                Layout.fillWidth: true
                                CheckBox {
                                    checked: planRow.included
                                    onToggled: mergeController.setIncluded(planRow.index, checked)
                                }
                                Label {
                                    text: "Keeps: " + planRow.survivor.title + " -- " + planRow.survivor.artist
                                    font.bold: true
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                            Label {
                                text: planRow.wastedBytesHuman + " freed"
                                    + (planRow.newCueCount > 0 ? "  --  " + planRow.newCueCount + " cue(s) merged onto the survivor" : "")
                                color: Theme.textMuted
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "Conserved: cues (merged, never lost) and playlist membership on both formats "
                                    + "-- every playlist the removed copy was in now points at the kept copy instead. "
                                    + "Not conserved yet: rating, color tag, genre and other tag fields."
                                color: Theme.textMuted
                                font.italic: true
                                font.pointSize: Theme.fontSmall
                            }
                            Label {
                                visible: planRow.differs
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                                text: "These copies differ in quality and length -- the higher-bitrate copy isn't the "
                                    + "longest one. Check the box above if you still want to merge them."
                                color: Theme.conflictText
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true }
                        Button {
                            text: "Merge These Tracks"
                            visible: mergeController.statusMessage.length === 0
                            enabled: !mergeController.busy && mergeController.includedCount > 0
                            onClicked: mergeController.apply()
                        }
                        Button {
                            text: mergeController.statusMessage.length > 0 ? "Done" : "Cancel"
                            enabled: !mergeController.writing
                            onClicked: mergeReviewPopup.close()
                        }
                    }
                }
            }

            Label {
                Layout.margins: 8
                text: trackListView.count + " tracks"
                color: Theme.textMuted
            }
        }
    }

    BusyOverlay {
        anchors.fill: parent
        busy: scanController.busy
        current: scanController.scanCurrent
        total: scanController.scanTotal
        label: "Scanning library..."
    }
}
