import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// Metadata Backup: read what the DJ added to this stick's tracks into a
// database on this computer, and browse what is in there.
//
// The two halves sit on one page on purpose. "Back up" and "look at what
// I have backed up" are the same question asked before and after, and a
// browse view on its own page would be a place nobody visits until
// something has already gone wrong.
//
// Nothing here writes to the stick. Putting metadata back is a different
// card with a different page, because it is a different decision.
Page {
    id: root
    required property string stickLabel
    required property string rekordboxPath
    required property string enginePath
    required property string libraryId

    readonly property bool hasStick: root.rekordboxPath.length > 0 || root.enginePath.length > 0
    // Any catalog directory will do: the controller reads every catalog
    // on the stick that one belongs to.
    readonly property string libraryPath: root.rekordboxPath.length > 0 ? root.rekordboxPath : root.enginePath

    // Overwrite by default: the stick is where the DJ works, so a cue
    // set there last night is newer than whatever is stored here.
    property bool overwriteOnConflict: true

    // The row whose cues are showing, or -1. One at a time: this is a
    // list to scan, not a tree to keep open.
    property var expandedTrackId: -1

    function formatBytes(bytes) {
        if (bytes <= 0) return "0 MB";
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(0) + " KB";
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + " MB";
        return (bytes / (1024 * 1024 * 1024)).toFixed(2) + " GB";
    }

    MetadataBackupController {
        id: controller
    }

    header: ToolBar {
        // Opaque background override: KDE's Breeze style bleeds the
        // window behind Seabass through an unstyled ToolBar.
        background: Rectangle { color: Theme.surface }
        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 12
            BackBreadcrumb {
                middleLabel: root.stickLabel
                title: "Metadata Backup"
                onHomeRequested: root.StackView.view.pop(null)
                onBackRequested: root.StackView.view.pop()
            }
            Item { Layout.fillWidth: true }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 14

        Subtitle {
            Layout.fillWidth: true
            // Subtitle is a plain Label, so a sentence longer than the
            // one-liners the other pages give it clips instead of
            // wrapping. Set here rather than on the shared component:
            // every other page's subtitle fits on its line today, and
            // wrapping is a per-use decision.
            wrapMode: Text.WordWrap
            text: "The cues, ratings and comments you put on your tracks, kept on this computer. "
                + "The audio can be re-imported from anywhere. This cannot."
        }

        // ---- run a backup -------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            radius: 8
            color: Theme.surface
            border.width: 1
            border.color: Theme.borderSubtle
            implicitHeight: runLayout.implicitHeight + 28

            ColumnLayout {
                id: runLayout
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Label {
                        text: root.hasStick ? "Back up " + root.stickLabel : "No stick selected"
                        color: Theme.text
                        font.family: Theme.titleFamily
                        font.weight: Theme.cardTitleWeight
                        font.pointSize: Theme.fontMedium
                    }
                    Item { Layout.fillWidth: true }
                    InfoButton {
                        explanationTitle: "What a metadata backup stores"
                        explanationText: "Seabass reads every catalog on the stick (DeviceLibrary, "
                            + "Device Library Plus and Engine) and folds them into one entry per file, so a "
                            + "track all three list is stored once with the union of its cues.\n\n"
                            + "Stored: cues and loops, rating, comment, play count, playlist membership, "
                            + "cover art, and enough of the title, artist and length to find the track "
                            + "again later.\n\n"
                            + "Not stored: waveforms, beat grids, analysis files and audio. All of it is "
                            + "derived from the audio file, all of it is large, and none of it is your work.\n\n"
                            + "Tracks are matched on their path within the stick, so the same track on a "
                            + "rebuilt stick lands on the entry it already had."
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: !controller.busy
                    text: "Nothing on the stick is changed or at risk. This only ever writes here: "
                        + controller.storeLocation
                    color: Theme.textMuted
                    font.pointSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: !controller.busy
                    spacing: 12

                    Label {
                        text: "If a track is already stored and differs:"
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                    }
                    RadioButton {
                        objectName: "overwriteRadio"
                        text: "Take the stick's version"
                        checked: root.overwriteOnConflict
                        onToggled: if (checked) root.overwriteOnConflict = true
                        ToolTip.visible: hovered
                        ToolTip.text: "The usual case: you have been cueing on the stick since the last backup."
                    }
                    RadioButton {
                        objectName: "keepStoredRadio"
                        text: "Keep what is stored"
                        checked: !root.overwriteOnConflict
                        onToggled: if (checked) root.overwriteOnConflict = false
                        ToolTip.visible: hovered
                        ToolTip.text: "For a stick you suspect has lost cues. Empty fields are still filled in."
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        objectName: "backUpNowButton"
                        text: "Back Up Now"
                        enabled: root.hasStick && !controller.busy
                        highlighted: true
                        onClicked: controller.backUp(root.libraryPath, root.libraryId, root.stickLabel,
                                                     root.overwriteOnConflict)
                    }
                }

                ProgressReport {
                    Layout.fillWidth: true
                    visible: controller.busy
                    phase: controller.currentPhase
                    unitsDone: controller.progressCurrent
                    unitsTotal: controller.progressTotal
                    unitName: "tracks"
                    cancellable: true
                    onCancelRequested: controller.cancel()
                }
            }
        }

        SelectableText {
            objectName: "errorLabel"
            Layout.fillWidth: true
            visible: controller.errorMessage.length > 0
            text: controller.errorMessage
            color: Theme.danger
        }

        // ---- what is in the store -----------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            TextField {
                objectName: "searchField"
                Layout.fillWidth: true
                Layout.minimumWidth: 110
                placeholderText: "Search title, artist or filename"
                onTextChanged: controller.search(text)
            }
            Label {
                Layout.maximumWidth: 320
                text: controller.storedTrackCount === 0
                    ? "Nothing stored yet"
                    : (controller.matchCount === controller.storedTrackCount
                        ? controller.storedTrackCount + " tracks stored, "
                          + root.formatBytes(controller.artworkBytes) + " of cover art"
                        : controller.matchCount + " of " + controller.storedTrackCount + " tracks")
                color: Theme.textMuted
                font.pointSize: Theme.fontSmall
                elide: Text.ElideRight
            }
        }

        ListView {
            id: trackList
            objectName: "storedTrackList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: controller.browseModel
            spacing: 2
            ScrollBar.vertical: BigScrollBar {}

            // Paged from SQL, so the list asks for the next page only
            // when the user has actually scrolled to the end of this one.
            onAtYEndChanged: if (atYEnd && controller.canLoadMore) controller.loadMore()

            delegate: Rectangle {
                id: trackRow
                required property int index
                required property var trackId
                required property string title
                required property string artist
                required property string filename
                required property string relativePath
                required property string durationText
                required property int rating
                required property string comment
                required property int cueCount
                required property int playlistCount
                required property string artworkUrl
                required property string stickLabel

                readonly property bool expanded: root.expandedTrackId === trackRow.trackId

                width: ListView.view.width
                implicitHeight: rowLayout.implicitHeight + 16
                color: rowMouse.containsMouse ? Theme.rowHover
                     : (trackRow.index % 2 === 0 ? Theme.rowEven : Theme.rowOdd)
                radius: 4

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.expandedTrackId = trackRow.expanded ? -1 : trackRow.trackId
                }

                ColumnLayout {
                    id: rowLayout
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: Theme.iconSizeNormal
                            Layout.preferredHeight: Theme.iconSizeNormal
                            radius: 3
                            color: Theme.groupBackground
                            Image {
                                anchors.fill: parent
                                source: trackRow.artworkUrl
                                visible: trackRow.artworkUrl.length > 0
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: true
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 1
                            Label {
                                Layout.fillWidth: true
                                text: trackRow.title.length > 0 ? trackRow.title : trackRow.filename
                                color: Theme.text
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: trackRow.artist.length > 0 ? trackRow.artist : trackRow.relativePath
                                color: Theme.textMuted
                                font.pointSize: Theme.fontSmall
                                elide: Text.ElideRight
                            }
                        }

                        Label {
                            text: trackRow.durationText
                            color: Theme.textMuted
                            font.pointSize: Theme.fontSmall
                        }
                        StarRating {
                            value: Math.max(0, trackRow.rating)
                            editable: false
                            visible: trackRow.rating >= 0
                        }
                        StatusBadge {
                            visible: trackRow.cueCount > 0
                            label: trackRow.cueCount + (trackRow.cueCount === 1 ? " cue" : " cues")
                            badgeColor: Theme.good
                        }
                        Label {
                            text: trackRow.stickLabel
                            color: Theme.textMuted
                            font.pointSize: Theme.fontTiny
                            elide: Text.ElideRight
                            Layout.maximumWidth: 120
                        }
                    }

                    // ---- the one expanded row ------------------------
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.iconSizeNormal + 10
                        visible: trackRow.expanded
                        spacing: 2

                        Label {
                            Layout.fillWidth: true
                            text: trackRow.relativePath
                            color: Theme.textMuted
                            font.pointSize: Theme.fontTiny
                            elide: Text.ElideMiddle
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: trackRow.comment.length > 0
                            text: "Comment: " + trackRow.comment
                            color: Theme.text
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }
                        Repeater {
                            model: trackRow.expanded ? controller.cuesFor(trackRow.trackId) : []
                            Label {
                                required property var modelData
                                text: (modelData.kind === "hot"
                                        ? "Hot cue " + modelData.hotCueNumber
                                        : "Memory cue")
                                    + " at " + modelData.positionText
                                    + (modelData.isLoop ? " (loop)" : "")
                                    + (modelData.comment.length > 0 ? ": " + modelData.comment : "")
                                color: Theme.textMuted
                                font.pointSize: Theme.fontSmall
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: trackRow.playlistCount > 0 && trackRow.expanded
                            text: "Playlists: " + controller.playlistsFor(trackRow.trackId).join(", ")
                            color: Theme.textMuted
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width * 0.7
                visible: trackList.count === 0 && !controller.busy
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: controller.storedTrackCount === 0
                    ? "Nothing has been backed up yet. Back Up Now reads this stick and stores what you added to it."
                    : "No stored track matches that search."
                color: Theme.textMuted
            }
        }
    }

    // ---- what the run did ---------------------------------------------
    OperationSummaryDialog {
        id: summaryDialog
    }

    Connections {
        target: controller
        function onResultChanged() {
            if (!controller.hasResult) {
                return;
            }
            var run = controller.lastRun;
            var detail = [];
            if (run.tracksAdded > 0) detail.push(run.tracksAdded + " newly stored");
            if (run.tracksUpdated > 0) detail.push(run.tracksUpdated + " brought up to date");
            if (run.tracksSkipped > 0) detail.push(run.tracksSkipped + " left as stored");
            if (run.tracksUnchanged > 0) detail.push(run.tracksUnchanged + " already current");
            if (run.tracksWithoutFile > 0) detail.push(run.tracksWithoutFile + " with no file on this stick");
            var lines = [detail.join(", ") + "."];
            lines.push(run.cuesStored + (run.cuesStored === 1 ? " cue" : " cues") + " stored"
                       + (run.artworkFilesAdded > 0
                           ? ", " + run.artworkFilesAdded + " new cover "
                             + (run.artworkFilesAdded === 1 ? "image" : "images")
                           : "") + ".");
            lines.push("Read from: " + run.catalogsRead.join(", ") + ".");
            if (run.catalogsUnreadable.length > 0) {
                lines.push("Could not read: " + run.catalogsUnreadable.join(", ")
                           + ". Anything only those held was not backed up.");
            }
            summaryDialog.show({
                written: run.tracksAdded + run.tracksUpdated,
                total: run.tracksSeen,
                unit: "tracks",
                verb: "stored",
                cancelled: run.cancelled,
                detail: lines.join("\n")
            });
        }
    }
}
