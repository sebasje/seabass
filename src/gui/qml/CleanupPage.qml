import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DjConvertGui

Page {
    id: root
    required property string stickLabel
    required property string rekordboxPath
    required property string enginePath
    required property var appSettingsController

    readonly property bool hasRekordbox: rekordboxPath.length > 0
    readonly property bool hasEngine: enginePath.length > 0
    readonly property string format: {
        var pref = appSettingsController.preferredFormat;
        if (pref === "engine" && hasEngine) return "engine";
        if (pref === "rekordbox" && hasRekordbox) return "rekordbox";
        return hasEngine ? "engine" : "rekordbox";
    }
    function currentPath() {
        return root.format === "engine" ? root.enginePath : root.rekordboxPath;
    }

    CleanupController {
        id: cleanupController
    }

    Component.onCompleted: cleanupController.scan(root.format, root.currentPath())
    onFormatChanged: cleanupController.scan(root.format, root.currentPath())

    function formatDuration(ms) {
        var totalSeconds = Math.round(ms / 1000);
        var m = Math.floor(totalSeconds / 60);
        var s = totalSeconds % 60;
        return m + ":" + (s < 10 ? "0" : "") + s;
    }

    header: ToolBar {
        // Opaque background override -- see AppSettingsPage.qml's header
        // for why (KDE's Breeze style bleeds the window behind Seabass
        // through an unstyled ToolBar). This page is the one that made the
        // bug visible: with real "pending deletions" content behind a
        // translucent header, System Settings text bled through the top
        // strip of the window.
        background: Rectangle { color: Theme.surface }

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
                    enabled: !cleanupController.writing
                    ToolTip.visible: hovered
                    ToolTip.text: cleanupController.writing
                        ? "Wait for the write to finish before leaving this page" : "Back"
                    onClicked: root.StackView.view.pop()
                }
                Label {
                    text: root.stickLabel + " -- Clean Up Duplicates"
                    font.bold: true
                    font.pointSize: Theme.fontLarge
                }
                Item { Layout.fillWidth: true }
                FormatToggle {
                    appSettingsController: root.appSettingsController
                    hasRekordbox: root.hasRekordbox
                    hasEngine: root.hasEngine
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                TextField {
                    id: searchField
                    placeholderText: "Search title or artist..."
                    Layout.preferredWidth: 220
                    onTextChanged: cleanupController.search(text)
                }
                Label {
                    text: plansListView.count + " duplicate group(s) found"
                    color: Theme.textMuted
                }
                Label {
                    visible: plansListView.count > 0
                    text: "-- " + cleanupController.totalWastedBytesHuman + " total if every copy kept only one file"
                    color: Theme.textMuted
                }
                Item { Layout.fillWidth: true }
                Label {
                    visible: cleanupController.includedCount > 0
                    text: cleanupController.includedCount + " group(s) selected"
                    color: Theme.textMuted
                }
                Button {
                    text: "Select All"
                    enabled: !cleanupController.busy && plansListView.count > 0
                    onClicked: confirmSelectAllDialog.open()
                }
                Button {
                    text: "Deselect All"
                    enabled: !cleanupController.busy && plansListView.count > 0
                    onClicked: cleanupController.setAllIncluded(false)
                }
                Button {
                    text: "Clean Up Selected"
                    enabled: !cleanupController.busy && cleanupController.includedCount > 0
                    onClicked: confirmCleanupDialog.open()
                }
                Button {
                    text: "Undo"
                    visible: cleanupController.canUndo
                    enabled: !cleanupController.busy
                    ToolTip.visible: hovered
                    ToolTip.text: "Revert the last cleanup -- restores every file it touched to what it was before"
                    onClicked: cleanupController.undoLastOperation()
                }
            }
        }
    }

    Dialog {
        id: confirmSelectAllDialog
        anchors.centerIn: parent
        modal: true
        width: 480
        title: "Select All " + plansListView.count + " Duplicate Group(s)?"
        footer: DialogButtonBox {
            Button { text: "Select All"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: cleanupController.setAllIncluded(true)

        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: "This marks all " + plansListView.count + " currently listed duplicate group(s) -- "
                + cleanupController.totalWastedBytesHuman + " total if every copy kept only one file -- "
                + "for the next \"Clean Up Selected\" click, including groups excluded by default because "
                + "their copies differ in quality (marked with ⚠ below).\n\n"
                + (searchField.text.length > 0
                    ? "Your search (\"" + searchField.text + "\") is currently narrowing this list -- clear it "
                        + "first if you meant to select across your whole library, or leave it as-is to select "
                        + "only these matching groups."
                    : "No search filter is active, so this selects every duplicate group found across your "
                        + "whole library.")
        }
    }

    Dialog {
        id: confirmCleanupDialog
        anchors.centerIn: parent
        modal: true
        width: 480
        title: "Clean Up " + cleanupController.includedCount + " Duplicate Group(s)?"
        footer: DialogButtonBox {
            Button { text: "Clean Up"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: cleanupController.apply()

        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: "For each selected group, every copy except the one kept will be removed from the library: "
                + "its hot/memory cues are merged onto the surviving copy first (nothing is lost), and any "
                + "playlist it belonged to is updated to reference the surviving copy instead.\n\n"
                + "This does NOT delete the removed copies' audio files -- their library entries are removed "
                + "and they're recorded for you to review and delete separately.\n\n"
                + "Everything touched is backed up first and can be undone."
        }
    }

    Dialog {
        id: confirmDeletePendingDialog
        anchors.centerIn: parent
        modal: true
        width: 480
        title: "Delete " + cleanupController.pendingDeletionsIncludedCount + " File(s) From Disk?"
        footer: DialogButtonBox {
            Button { text: "Delete"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: cleanupController.deleteSelectedPendingFiles()

        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: "Each selected file is re-verified against the current library right before deletion -- "
                + "if anything still references it, it's left alone and reported instead of deleted.\n\n"
                + "This step is irreversible: a deleted file is gone. The library-database edit that "
                + "originally orphaned it was already backed up separately, when the duplicate was first "
                + "cleaned up above -- that backup restores the database entry, not this file."
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        StickWriteWarning {
            visible: cleanupController.writing
            text: "Cleaning up duplicates -- do not remove the stick until this finishes."
        }

        Label {
            visible: cleanupController.errorMessage.length > 0
            text: cleanupController.errorMessage
            color: Theme.danger
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            visible: cleanupController.statusMessage.length > 0
            text: cleanupController.statusMessage
            color: Theme.good
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        ColumnLayout {
            id: pendingPanel
            visible: pendingListView.count > 0
            Layout.fillWidth: true
            spacing: 4

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: pendingContent.implicitHeight + 16
                color: Theme.groupBackground
                border.color: Theme.borderSubtle
                radius: 4

                ColumnLayout {
                    id: pendingContent
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Label {
                            text: pendingListView.count + " file(s) from earlier cleanups are orphaned but still on disk"
                            font.bold: true
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            visible: cleanupController.pendingDeletionsIncludedCount > 0
                            text: cleanupController.pendingDeletionsIncludedCount + " selected"
                            color: Theme.textMuted
                        }
                        Button {
                            text: "Select All"
                            enabled: !cleanupController.busy && pendingListView.count > 0
                            onClicked: cleanupController.setAllPendingDeletionIncluded(true)
                        }
                        Button {
                            text: "Deselect All"
                            enabled: !cleanupController.busy && pendingListView.count > 0
                            onClicked: cleanupController.setAllPendingDeletionIncluded(false)
                        }
                        Button {
                            text: "Delete Selected Files"
                            enabled: !cleanupController.busy && cleanupController.pendingDeletionsIncludedCount > 0
                            onClicked: confirmDeletePendingDialog.open()
                        }
                    }

                    ListView {
                        id: pendingListView
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, 160)
                        clip: true
                        model: cleanupController.pendingDeletions
                        spacing: 2

                        ScrollBar.vertical: BigScrollBar {}

                        delegate: ItemDelegate {
                            id: pendingDelegate
                            width: ListView.view.width
                            hoverEnabled: false

                            required property int index
                            required property string title
                            required property string artist
                            required property string filePath
                            required property bool included

                            contentItem: RowLayout {
                                spacing: 8
                                CheckBox {
                                    checked: pendingDelegate.included
                                    onToggled: cleanupController.setPendingDeletionIncluded(pendingDelegate.index, checked)
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Include this file in the next deletion"
                                }
                                Label {
                                    text: pendingDelegate.title + " -- " + pendingDelegate.artist
                                    elide: Text.ElideRight
                                    Layout.preferredWidth: 260
                                }
                                Label {
                                    text: pendingDelegate.filePath
                                    elide: Text.ElideMiddle
                                    color: Theme.textMuted
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
            }
        }

        ListView {
            id: plansListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: cleanupController.plans
            spacing: 4

            ScrollBar.vertical: BigScrollBar {}

            delegate: Column {
                id: delegateRoot
                width: ListView.view.width
                spacing: 4

                required property int index
                required property var survivor
                required property var toRemove
                required property bool differs
                required property string wastedBytesHuman
                required property int newCueCount
                required property bool included

                property bool expanded: false

                ItemDelegate {
                    width: parent.width
                    hoverEnabled: true
                    onClicked: delegateRoot.expanded = !delegateRoot.expanded

                    contentItem: ColumnLayout {
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            CheckBox {
                                checked: delegateRoot.included
                                onToggled: cleanupController.setIncluded(delegateRoot.index, checked)
                                ToolTip.visible: hovered
                                ToolTip.text: "Include this group in the next cleanup"
                            }
                            Label {
                                text: "Keeps: " + delegateRoot.survivor.title + " -- " + delegateRoot.survivor.artist
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.preferredWidth: 320
                            }
                            Label {
                                text: "(removes " + delegateRoot.toRemove.length + " cop"
                                    + (delegateRoot.toRemove.length === 1 ? "y" : "ies") + ")"
                                color: Theme.textMuted
                            }
                            Label {
                                visible: delegateRoot.differs
                                text: "  ⚠ copies differ"
                                color: Theme.conflictText
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: delegateRoot.expanded ? "▾" : "▸"
                                font.pointSize: Theme.fontHuge
                                font.bold: true
                                color: Theme.textMuted
                            }
                        }
                        Label {
                            text: delegateRoot.wastedBytesHuman + " freed"
                                + (delegateRoot.newCueCount > 0 ? "  --  " + delegateRoot.newCueCount + " cue(s) merged onto the survivor" : "")
                            color: Theme.textMuted
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: "Conserved: cues (merged, never lost) and playlist membership on both formats "
                                + "-- every playlist a removed copy was in now points at the kept copy instead. "
                                + "Not conserved yet: rating, color tag, genre and other tag fields on the "
                                + "removed copies aren't copied over."
                            color: Theme.textMuted
                            font.italic: true
                            font.pointSize: Theme.fontSmall
                        }
                        Label {
                            visible: delegateRoot.differs
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            text: "These copies differ in quality and length -- the higher-bitrate copy isn't the "
                                + "longest one. This may be intentional (e.g. a shorter edit kept for specific "
                                + "hardware), so this group is excluded by default -- check it above to include it anyway."
                            color: Theme.conflictText
                        }
                    }
                }

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

                        Label {
                            Layout.fillWidth: true
                            text: "Kept copy (id=" + delegateRoot.survivor.sourceId + ")"
                            font.bold: true
                            color: Theme.good
                        }
                        Label {
                            Layout.fillWidth: true
                            text: (delegateRoot.survivor.bitrate > 0 ? delegateRoot.survivor.bitrate + " kbps, " : "")
                                + root.formatDuration(delegateRoot.survivor.durationMs) + ", "
                                + delegateRoot.survivor.sizeHuman
                            color: Theme.textMuted
                        }

                        Label {
                            Layout.fillWidth: true
                            text: "Removed cop" + (delegateRoot.toRemove.length === 1 ? "y" : "ies")
                            font.bold: true
                            Layout.topMargin: 8
                        }
                        Repeater {
                            model: delegateRoot.toRemove
                            delegate: Label {
                                Layout.fillWidth: true
                                required property var modelData
                                text: "id=" + modelData.sourceId + "  --  "
                                    + (modelData.bitrate > 0 ? modelData.bitrate + " kbps, " : "")
                                    + root.formatDuration(modelData.durationMs) + ", " + modelData.sizeHuman
                                color: Theme.textMuted
                            }
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: plansListView.count === 0 && !cleanupController.busy
                text: "No duplicate tracks with a removable copy found."
                color: Theme.textMuted
            }
        }
    }

    BusyOverlay {
        anchors.fill: parent
        busy: cleanupController.busy
        current: cleanupController.scanCurrent
        total: cleanupController.scanTotal
        label: cleanupController.writing ? "Cleaning up duplicates..." : "Scanning for duplicates..."
    }
}
