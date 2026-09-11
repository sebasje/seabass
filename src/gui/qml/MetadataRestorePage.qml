// SPDX-FileCopyrightText: 2026 Sebastian Kügler <sebas@kde.org>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// Restore Metadata: put the cues from this computer's metadata store
// back on a stick that has lost them.
//
// The counterpart to Metadata Backup, and deliberately its own page.
// Reading a stick into the store risks nothing; writing to the stick is
// a save like every other one in Seabass, so nothing here reaches the
// stick until Save. Each accepted track is staged into the library's
// edit session, and the save backs up before it touches a file.
Page {
    id: root
    required property string stickLabel
    required property string rekordboxPath
    required property string enginePath
    required property string libraryId

    readonly property bool hasStick: root.rekordboxPath.length > 0 || root.enginePath.length > 0
    readonly property string libraryPath: root.rekordboxPath.length > 0 ? root.rekordboxPath : root.enginePath

    // Skip by default, and the asymmetry with a backup is the point: the
    // stick may have been re-cued since the store was last filled, and
    // replacing last night's work with last month's is the worst thing
    // this page could do.
    property bool overwriteConflicts: false

    MetadataRestoreController {
        id: controller
    }

    EditSessionHost {
        id: editHost
        feature: "metadata-restore"
        anchors.fill: parent
        libraryId: root.libraryId
        stickLabel: root.stickLabel
        rekordboxPath: root.rekordboxPath
        enginePath: root.enginePath
    }

    MessagePopup { id: messagePopup }
    Connections {
        target: controller
        function onActionFeedback(message, isError) {
            messagePopup.show(message, isError);
        }
    }

    Component.onCompleted: if (root.hasStick) controller.scan(root.libraryPath, root.overwriteConflicts)
    onOverwriteConflictsChanged: if (root.hasStick) controller.scan(root.libraryPath, root.overwriteConflicts)

    header: ToolBar {
        // Opaque background override: KDE's Breeze style bleeds the
        // window behind Seabass through an unstyled ToolBar.
        background: Rectangle { color: Theme.surface }
        // Every side zeroed so the header's inset is Theme.pageMargin
        // and nothing else. `padding` alone does not do it: styles set
        // horizontalPadding or leftPadding of their own on top of it,
        // 4px under Breeze and 6 under the default style, and that is
        // exactly how far right of the body the breadcrumb used to sit.
        leftPadding: 0
        rightPadding: 0
        topPadding: 0
        bottomPadding: 0
        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.pageMargin
            spacing: Theme.rowSpacing
            BackBreadcrumb {
                middleLabel: root.stickLabel
                title: "Restore Metadata"
                onHomeRequested: root.StackView.view.pop(null)
                onBackRequested: root.StackView.view.pop()
            }
            Item { Layout.fillWidth: true }
            BusyIndicator {
                running: controller.busy
                visible: controller.busy
                implicitWidth: Theme.iconSizeSmall
                implicitHeight: Theme.iconSizeSmall
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.pageMargin
        spacing: Theme.sectionSpacing

        Subtitle {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Tracks on " + root.stickLabel + " that have lost cues the store still has. "
                + "Nothing is written until you press Save."
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.tightSpacing

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pointSize: Theme.fontSmall
                text: {
                    if (controller.busy) {
                        return "Reading this stick and matching it against the store...";
                    }
                    if (!controller.hasScanned) {
                        return root.hasStick ? "Not checked yet." : "No stick selected.";
                    }
                    if (controller.storedTrackCount === 0) {
                        return "The metadata store is empty. Run a Metadata Backup on a stick that still "
                             + "has your cues, and they can be put back here afterwards.";
                    }
                    var line = "Matched " + controller.stickTrackCount + " tracks on the stick against "
                             + controller.storedTrackCount + " in the store.";
                    if (controller.conflictCount > 0 && !root.overwriteConflicts) {
                        line += " " + controller.conflictCount
                             + (controller.conflictCount === 1 ? " track has cues of its own that differ; it is"
                                                               : " tracks have cues of their own that differ; they are")
                             + " left alone.";
                    }
                    return line;
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.tightSpacing
                visible: !controller.busy
                spacing: Theme.rowSpacing

                Label {
                    text: "If the stick already has different cues:"
                    color: Theme.textMuted
                    font.pointSize: Theme.fontSmall
                }
                RadioButton {
                    objectName: "keepStickRadio"
                    text: "Keep the stick's"
                    checked: !root.overwriteConflicts
                    onToggled: if (checked) root.overwriteConflicts = false
                    ToolTip.visible: hovered
                    ToolTip.text: "The safe default: the stick may have been re-cued since the backup."
                }
                RadioButton {
                    objectName: "replaceFromStoreRadio"
                    text: "Replace them from the store"
                    checked: root.overwriteConflicts
                    onToggled: if (checked) root.overwriteConflicts = true
                    ToolTip.visible: hovered
                    ToolTip.text: "For a stick whose cues you know are wrong. Save backs the files up first."
                }
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "stageAllButton"
                    text: "Stage All"
                    enabled: proposalList.count > 0 && !controller.busy && !editHost.writing
                    highlighted: true
                    onClicked: controller.stageAll()
                }
            }

            ProgressReport {
                Layout.fillWidth: true
                Layout.topMargin: Theme.tightSpacing
                visible: controller.busy
                phase: controller.currentPhase
                unitsDone: controller.progressCurrent
                unitsTotal: controller.progressTotal
                unitName: "tracks"
                cancellable: true
                onCancelRequested: controller.cancelScan()
            }
        }

        SelectableText {
            objectName: "errorLabel"
            Layout.fillWidth: true
            visible: controller.errorMessage.length > 0
            text: controller.errorMessage
            color: Theme.danger
        }

        // Said before the save, not discovered in the log afterwards.
        // A page that silently declines to write a field teaches the DJ
        // that the field is unreliable; one that says which format
        // cannot hold it teaches them something true about their own
        // library.
        Label {
            Layout.fillWidth: true
            visible: controller.commentsRekordboxCannotTake > 0
            wrapMode: Text.WordWrap
            color: Theme.warnText
            font.pointSize: Theme.fontSmall
            text: {
                var n = controller.commentsRekordboxCannotTake;
                return (n === 1 ? "One track's comment cannot be put back: it is"
                                : n + " tracks' comments cannot be put back: they are")
                     + " catalogued only in DeviceLibrary, which stores a comment in a fixed space "
                     + "decided when the stick was exported and cannot make room for a new one. "
                     + "Their cues and ratings still go back. Engine and Device Library Plus take "
                     + "comments of any length.";
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.borderSubtle
        }

        ListView {
            id: proposalList
            objectName: "proposalList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: controller.proposals
            spacing: 2
            ScrollBar.vertical: BigScrollBar {}

            delegate: Rectangle {
                id: proposalRow
                required property int index
                required property string title
                required property string artist
                required property string filename
                required property int cueCount
                required property int cuesAdded
                required property bool fillsAGap
                required property bool conflict
                required property int rating
                required property string comment
                required property bool staged

                width: ListView.view.width
                implicitHeight: rowLayout.implicitHeight + 2 * Theme.tightSpacing
                color: rowMouse.containsMouse ? Theme.rowHover
                     : (proposalRow.index % 2 === 0 ? Theme.rowEven : Theme.rowOdd)
                radius: 4

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                }

                RowLayout {
                    id: rowLayout
                    anchors.fill: parent
                    anchors.margins: Theme.tightSpacing
                    anchors.leftMargin: 0
                    spacing: Theme.rowSpacing

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            text: proposalRow.title.length > 0 ? proposalRow.title : proposalRow.filename
                            color: Theme.text
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: proposalRow.artist
                            color: Theme.textMuted
                            font.pointSize: Theme.fontSmall
                            elide: Text.ElideRight
                        }
                    }

                    StatusBadge {
                        visible: proposalRow.conflict
                        label: "replaces " + proposalRow.cueCount
                        badgeColor: Theme.warnIcon
                        tooltipText: "This track has cues of its own. Saving replaces them with the stored ones."
                    }
                    Label {
                        visible: proposalRow.fillsAGap
                        text: proposalRow.cuesAdded + (proposalRow.cuesAdded === 1 ? " cue" : " cues")
                            + " to put back"
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                    }
                    StarRating {
                        visible: proposalRow.rating >= 0
                        value: Math.max(0, proposalRow.rating)
                        editable: false
                        // StarRating only tracks hover while it is
                        // editable, and this one is not, so the hover
                        // comes from a handler of its own.
                        ToolTip.visible: ratingHover.hovered
                        ToolTip.text: "This rating goes back on the track"
                        HoverHandler { id: ratingHover }
                    }
                    Label {
                        visible: proposalRow.comment.length > 0
                        text: "comment"
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                        // A Label is a Text and has no `hovered`; this is
                        // the only place the whole comment can be read.
                        ToolTip.visible: commentHover.hovered
                        ToolTip.text: proposalRow.comment
                        HoverHandler { id: commentHover }
                    }
                    Button {
                        objectName: "stageButton"
                        text: proposalRow.staged ? "Staged" : "Stage"
                        enabled: !editHost.writing
                        checkable: false
                        onClicked: proposalRow.staged ? controller.unstage(proposalRow.index)
                                                      : controller.stage(proposalRow.index)
                        ToolTip.visible: hovered
                        ToolTip.text: proposalRow.staged ? "Staged. Press again to take it back off the list."
                                                         : "Add to this save. Nothing is written until you press Save."
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width * 0.7
                visible: proposalList.count === 0 && !controller.busy && controller.hasScanned
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                text: controller.storedTrackCount === 0
                    ? "Nothing is stored yet, so there is nothing to put back."
                    : "Every track on this stick already has the cues the store holds for it."
            }
        }
    }

    SaveOverlayButton {
        session: editHost.session
        label: "Save to " + root.stickLabel
    }
}
