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

    property var snapshots: []
    function refreshSnapshots() { root.snapshots = localCueController.listSnapshots(); }

    function friendlyTimestamp(iso) {
        // "2026-08-27T14:32:05Z" -> Date
        var d = new Date(iso);
        return isNaN(d.getTime()) ? iso : d.toLocaleString(Qt.locale(), "d MMM yyyy, HH:mm:ss");
    }

    LocalCueController {
        id: localCueController
    }

    function refresh() { localCueController.analyzeRestore(root.format, root.currentPath()); }

    Component.onCompleted: {
        refresh();
        refreshSnapshots();
    }
    onFormatChanged: refresh()

    // Snapshot list/description/delete are synchronous calls with no
    // signal of their own, so refresh whenever a background operation
    // (backup, restore-analysis) that might have changed them finishes.
    Connections {
        target: localCueController
        function onBusyChanged() {
            if (!localCueController.busy) {
                root.refreshSnapshots();
            }
        }
    }

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
                enabled: !localCueController.writing
                ToolTip.visible: hovered
                ToolTip.text: localCueController.writing
                    ? "Wait for the write to finish before leaving this page" : "Back"
                onClicked: root.StackView.view.pop()
            }
            Label {
                text: root.stickLabel + " -- Local Cue Backup"
                font.bold: true
                font.pointSize: Theme.fontLarge
            }
            Item { Layout.fillWidth: true }
        }
    }

    Dialog {
        id: confirmDialog
        property string sourceDescription: ""
        anchors.centerIn: parent
        modal: true
        title: "Merge Cues Onto Stick?"
        footer: DialogButtonBox {
            Button { text: "Merge Now"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: localCueController.applyRestore()

        ColumnLayout {
            spacing: 8
            Label {
                text: "Add new cues from this computer's backup onto " + restoreListView.count + " track(s) -- "
                    + "any cue already on the stick is kept exactly as it is, never overwritten."
                    + (confirmDialog.sourceDescription.length > 0
                        ? "\nSource: " + confirmDialog.sourceDescription : "")
                wrapMode: Text.WordWrap
            }
            Label {
                text: "The stick is backed up before anything is written -- once this finishes, "
                    + "\"Undo\" reverts every file it touched. Do not remove the stick while it's running."
                color: Theme.textMuted
                wrapMode: Text.WordWrap
            }
        }
    }

    Dialog {
        id: confirmDeleteSnapshotDialog
        property int targetId: -1
        anchors.centerIn: parent
        modal: true
        title: "Delete This Backup?"
        footer: DialogButtonBox {
            Button { text: "Delete"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: {
            localCueController.deleteSnapshot(targetId);
            root.refreshSnapshots();
        }

        Label {
            text: "This permanently deletes this one backup snapshot from this computer. It never "
                + "touches the stick, and never touches any other backup."
            wrapMode: Text.WordWrap
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        StickWriteWarning {
            visible: localCueController.writing
            text: "Writing cues to the stick -- do not remove it until this finishes."
        }

        Label {
            visible: localCueController.errorMessage.length > 0
            text: localCueController.errorMessage
            color: Theme.danger
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            visible: localCueController.statusMessage.length > 0
            text: localCueController.statusMessage
            color: Theme.good
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Frame {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    ColumnLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Label { text: "Back Up to This Computer"; font.bold: true }
                        Label {
                            text: (root.hasRekordbox && root.hasEngine
                                    ? "Copies this stick's cues -- both Rekordbox and Engine -- to a local backup."
                                    : "Copies this stick's cues to a local backup.")
                                + " Never touches the stick, no confirmation needed."
                            color: Theme.textMuted
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                    TextField {
                        id: backupDescriptionField
                        Layout.preferredWidth: 220
                        placeholderText: "Note (e.g. \"before Berlin gig\")..."
                    }
                    Button {
                        text: "Backup Now"
                        enabled: !localCueController.busy
                        onClicked: {
                            localCueController.backupToComputer(root.stickLabel, backupDescriptionField.text,
                                root.rekordboxPath, root.enginePath);
                            backupDescriptionField.text = "";
                        }
                    }
                }
            }
        }

        Frame {
            Layout.fillWidth: true
            Layout.preferredHeight: root.height * 0.32
            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Label { text: "Backup History"; font.bold: true }
                Label {
                    text: "Each backup here is frozen at the moment it was made -- restoring from one "
                        + "always uses exactly that snapshot, even if newer backups exist."
                    color: Theme.textMuted
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                ListView {
                    id: snapshotListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.snapshots
                    spacing: 2

                    delegate: ItemDelegate {
                        id: snapshotDelegate
                        width: ListView.view.width
                        height: 40

                        required property var modelData

                        contentItem: RowLayout {
                            spacing: 8
                            Label {
                                text: snapshotDelegate.modelData.sourceFormat === "engine" ? "Engine" : "Rekordbox"
                                color: Theme.accent
                                font.bold: true
                                Layout.preferredWidth: 72
                            }
                            Label {
                                text: root.friendlyTimestamp(snapshotDelegate.modelData.createdAt)
                                    + "  --  " + snapshotDelegate.modelData.trackCount + " track(s), "
                                    + snapshotDelegate.modelData.cueCount + " cue(s)"
                                color: Theme.textMuted
                                Layout.preferredWidth: 320
                                elide: Text.ElideRight
                            }
                            TextField {
                                id: snapshotDescriptionField
                                Layout.fillWidth: true
                                placeholderText: "Click to add a note..."
                                text: snapshotDelegate.modelData.description
                                background: Rectangle {
                                    radius: 4
                                    color: snapshotDescriptionField.activeFocus ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08) : "transparent"
                                    border.width: snapshotDescriptionField.activeFocus ? 1 : 0
                                    border.color: Theme.accent
                                }
                                onEditingFinished: {
                                    localCueController.setSnapshotDescription(snapshotDelegate.modelData.id, text);
                                    root.refreshSnapshots();
                                }
                            }
                            Button {
                                text: "Restore From Here"
                                enabled: !localCueController.busy
                                ToolTip.visible: hovered
                                ToolTip.text: "Match this exact backup against the " + (root.format === "engine" ? "Engine" : "Rekordbox")
                                    + " side of the stick (switch the format toggle below to restore the other side) -- results appear below"
                                onClicked: {
                                    confirmDialog.sourceDescription = snapshotDescriptionField.text.length > 0
                                        ? snapshotDescriptionField.text
                                        : root.friendlyTimestamp(snapshotDelegate.modelData.createdAt);
                                    localCueController.analyzeSnapshotRestore(snapshotDelegate.modelData.id, root.format, root.currentPath());
                                }
                            }
                            ToolButton {
                                text: "🗑"
                                font.family: "Noto Sans Symbols2"
                                opacity: 0.55
                                Layout.preferredWidth: 32
                                ToolTip.visible: hovered
                                ToolTip.text: "Delete this backup permanently"
                                onClicked: {
                                    confirmDeleteSnapshotDialog.targetId = snapshotDelegate.modelData.id;
                                    confirmDeleteSnapshotDialog.open();
                                }
                            }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: snapshotListView.count === 0
                        text: "No backups yet -- click \"Backup Now\" above to create the first one."
                        color: Theme.textMuted
                    }
                }
            }
        }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    ColumnLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Label { text: "Merge Cues from This Computer"; font.bold: true }
                        Label {
                            text: "Stick tracks: " + localCueController.stickTrackCount
                                + "   Backed up on this computer: " + localCueController.localTrackCount
                                + "   Have new cues to add: " + restoreListView.count
                            color: Theme.textMuted
                        }
                    }
                    // Scoped to just this section -- unlike every other
                    // page's header-level toggle, "Back Up to This
                    // Computer" above always backs up both formats
                    // together in one snapshot regardless of it, so a
                    // page-wide toggle up in the header implied a scope
                    // it didn't actually have. Merging is genuinely
                    // format-specific (one side of the stick at a time),
                    // so it lives right where it applies instead.
                    FormatToggle {
                        appSettingsController: root.appSettingsController
                        hasRekordbox: root.hasRekordbox
                        hasEngine: root.hasEngine
                    }
                    Button {
                        text: "Re-Analyze Latest"
                        enabled: !localCueController.busy
                        ToolTip.visible: hovered
                        ToolTip.text: "Match the stick against the current merged backup state (not one specific snapshot)"
                        onClicked: {
                            confirmDialog.sourceDescription = "";
                            refresh();
                        }
                    }
                    Button {
                        text: "Merge Onto " + restoreListView.count + " Track(s)"
                        enabled: !localCueController.busy && restoreListView.count > 0
                        onClicked: confirmDialog.open()
                    }
                }

                ListView {
                    id: restoreListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: localCueController.restoreCandidates
                    spacing: 2

                    delegate: ItemDelegate {
                        width: ListView.view.width

                        required property string filename
                        required property string title
                        required property string artist
                        required property string description

                        contentItem: ColumnLayout {
                            spacing: 2
                            Label { text: title.length > 0 ? (title + " -- " + artist) : filename; font.bold: true }
                            Label { text: description + " new cue(s) would be added"; color: Theme.textMuted }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: restoreListView.count === 0 && !localCueController.busy
                        text: "Nothing to merge -- either the stick already has every cue this backup offers,\nor none of its tracks match one backed up on this computer."
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.textMuted
                    }
                }
            }
        }
    }

    BusyOverlay {
        anchors.fill: parent
        busy: localCueController.busy
        current: localCueController.scanCurrent
        total: localCueController.scanTotal
        label: localCueController.writing ? "Restoring cues..." : "Analyzing backups..."
    }
}
