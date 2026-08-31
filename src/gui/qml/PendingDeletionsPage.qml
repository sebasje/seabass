import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// Moved out of CleanupPage.qml into its own page (was an embedded panel
// there, competing for space with the actual duplicate-group list it sat
// above) -- this is a distinct concern from finding/consolidating
// duplicates: reviewing and actually deleting the audio files earlier
// cleanups' database edits orphaned but never touched on disk.
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

    // loadPendingDeletionsOnly(), not scan() -- this page only ever shows
    // the pending-deletions list, so there's no reason to pay for a full
    // whole-library duplicate-group rescan just to get here.
    Component.onCompleted: cleanupController.loadPendingDeletionsOnly(root.format, root.currentPath())
    onFormatChanged: cleanupController.loadPendingDeletionsOnly(root.format, root.currentPath())

    header: ToolBar {
        // Opaque background override -- see AppSettingsPage.qml's header
        // for why (KDE's Breeze style bleeds the window behind Seabass
        // through an unstyled ToolBar).
        background: Rectangle { color: Theme.surface }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
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
            PageTitle {
                text: root.stickLabel + " · Delete Orphaned Files"
            }
            Item { Layout.fillWidth: true }
            FormatToggle {
                appSettingsController: root.appSettingsController
                hasRekordbox: root.hasRekordbox
                hasEngine: root.hasEngine
            }
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
            text: "This frees " + cleanupController.includedPendingBytesHuman + ". Each selected file is "
                + "re-verified against the current library right before deletion -- if anything still "
                + "references it, it's left alone and reported instead of deleted.\n\n"
                + "This step is irreversible: a deleted file is gone. The library-database edit that "
                + "originally orphaned it was already backed up separately, when the duplicate was first "
                + "cleaned up -- that backup restores the database entry, not this file."
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        StickWriteWarning {
            visible: cleanupController.writing
            text: "Deleting orphaned files -- do not remove the stick until this finishes."
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

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Label {
                text: pendingListView.count + " file(s) from earlier cleanups are orphaned but still on disk"
                font.bold: true
            }
            Label {
                visible: pendingListView.count > 0
                text: "-- " + cleanupController.totalPendingBytesHuman + " total"
                color: Theme.textMuted
            }
            Item { Layout.fillWidth: true }
            Label {
                visible: cleanupController.pendingDeletionsIncludedCount > 0
                text: cleanupController.pendingDeletionsIncludedCount + " selected ("
                    + cleanupController.includedPendingBytesHuman + ")"
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
            Layout.fillHeight: true
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
                required property string sizeHuman
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
                    Label {
                        text: pendingDelegate.sizeHuman
                        color: Theme.textMuted
                        Layout.preferredWidth: 70
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: pendingListView.count === 0 && !cleanupController.busy
                text: "Nothing orphaned -- every earlier cleanup's files are either still in use or already deleted."
                color: Theme.textMuted
            }
        }
    }

    BusyOverlay {
        anchors.fill: parent
        busy: cleanupController.busy
        current: cleanupController.scanCurrent
        total: cleanupController.scanTotal
        label: cleanupController.writing ? "Deleting files..." : "Loading..."
    }
}
