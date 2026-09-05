import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SeabassGui

// Top-level "Restore a Stick Backup" (experimental). Lives next to Format
// USB Stick rather than under a stick, because the disaster case is a
// lost or dead stick and a fresh blank one -- which the per-stick card
// grid structurally never shows. The per-stick backup page pushes this
// same page with the drive and archive preselected.
Page {
    id: root
    required property var controller
    property string preselectedMountPoint: ""
    property string preselectedArchivePath: ""
    property string preselectedLabel: ""
    signal formatUsbRequested()

    readonly property var disks: controller.disks || []
    readonly property var info: controller.archiveInfo || ({})
    readonly property var preview: controller.preview || ({})
    readonly property var result: controller.result || ({})
    property int selectedIndex: -1
    readonly property var selectedDisk: (root.selectedIndex >= 0 && root.selectedIndex < root.disks.length)
        ? root.disks[root.selectedIndex] : null
    property bool exact: false
    readonly property bool archiveReady: root.info.label !== undefined && (!root.info.error || root.info.error.length === 0)
    readonly property bool needsTypedConfirmation: root.selectedDisk !== null
        && (root.selectedDisk.hasDjLibrary === true || root.exact || root.preview.extras > 0)
    readonly property bool canRestore: root.archiveReady && root.selectedDisk !== null && root.selectedDisk.usable === true
        && root.preview.filesToWrite !== undefined && root.preview.enoughFreeSpace !== false && root.controller.busy !== true

    function pickDefaultDrive() {
        if (root.preselectedMountPoint.length > 0) {
            for (var i = 0; i < root.disks.length; ++i) {
                if (root.disks[i].mountPoint === root.preselectedMountPoint) return i;
            }
        }
        for (var j = 0; j < root.disks.length; ++j) {
            if (root.disks[j].usable === true) return j;
        }
        return -1;
    }

    function applySelection(index) {
        root.selectedIndex = index;
        if (root.selectedDisk !== null && root.selectedDisk.usable === true && root.controller.analyze) {
            root.controller.analyze(root.selectedDisk.mountPoint);
        } else if (root.controller.analyze) {
            root.controller.analyze("");
        }
    }

    function friendlyTimestamp(iso) {
        if (!iso) return "";
        var d = new Date(iso);
        return isNaN(d.getTime()) ? iso : d.toLocaleString(Qt.locale(), "d MMM yyyy, HH:mm");
    }

    // `disks` usually resolves (and onDisksChanged picks the default
    // drive) before this runs; only re-analyze here when the archive to
    // restore from was changed by a preselection.
    Component.onCompleted: {
        var archiveChanged = false;
        if (root.preselectedArchivePath.length > 0) {
            root.controller.archivePath = root.preselectedArchivePath;
            archiveChanged = true;
        } else if (root.preselectedLabel.length > 0 && root.controller.archivePathForLabel) {
            root.controller.archivePath = root.controller.archivePathForLabel(root.preselectedLabel);
            archiveChanged = true;
        }
        if (root.selectedIndex < 0) {
            root.applySelection(root.pickDefaultDrive());
        } else if (archiveChanged) {
            root.applySelection(root.selectedIndex);
        }
    }
    onDisksChanged: if (root.selectedIndex < 0) root.applySelection(root.pickDefaultDrive())

    MessagePopup { id: messagePopup }
    Connections {
        // A plain JS stand-in (tests) has no signals and is not a QObject.
        target: ("objectName" in root.controller) ? root.controller : null
        ignoreUnknownSignals: true
        function onActionFeedback(message, isError) {
            messagePopup.show(message, isError ? Theme.danger : Theme.good);
        }
    }

    FileDialog {
        id: archiveDialog
        title: "Choose a Seabass stick backup"
        nameFilters: ["Stick backups (*.zip)", "All files (*)"]
        currentFolder: "file://" + (root.controller.defaultBackupDirectory || "")
        onAccepted: {
            root.controller.archivePath = selectedFile.toString();
            root.applySelection(root.selectedIndex);
        }
    }

    header: ToolBar {
        background: Rectangle { color: Theme.surface }
        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 12
            BackBreadcrumb {
                title: "Restore a Stick Backup"
                backEnabled: !root.controller.busy
                backDisabledTooltip: "Wait for the restore to finish (or cancel it) before leaving this page"
                onHomeRequested: root.StackView.view.pop(null)
                onBackRequested: root.StackView.view.pop()
            }
            Rectangle {
                radius: 3
                color: Theme.warnBg
                border.color: Theme.warnBorder
                implicitWidth: experimentalLabel.implicitWidth + 8
                implicitHeight: experimentalLabel.implicitHeight + 4
                Label {
                    id: experimentalLabel
                    anchors.centerIn: parent
                    text: "EXPERIMENTAL"
                    font.pointSize: Theme.fontTiny
                    font.bold: true
                    color: Theme.warnText
                }
            }
            Item { Layout.fillWidth: true }
            BusyIndicator { running: root.controller.analyzing === true; visible: running; implicitWidth: 20; implicitHeight: 20 }
        }
    }

    Dialog {
        id: confirmDialog
        objectName: "confirmDialog"
        anchors.centerIn: parent
        modal: true
        width: 480
        title: "Restore onto " + (root.selectedDisk ? root.selectedDisk.label : "") + "?"
        readonly property string confirmTarget: root.selectedDisk ? (root.selectedDisk.label.length > 0 ? root.selectedDisk.label : "(untitled)") : ""
        footer: DialogButtonBox {
            Button {
                objectName: "restoreAcceptButton"
                text: "Restore Drive"
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                enabled: !root.needsTypedConfirmation || confirmField.text === confirmDialog.confirmTarget
            }
            Button { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onOpened: confirmField.text = ""
        onAccepted: root.controller.restore(root.selectedDisk.mountPoint, root.exact)

        ColumnLayout {
            width: parent.width
            spacing: 14
            Rectangle {
                Layout.fillWidth: true
                visible: root.needsTypedConfirmation
                implicitHeight: warnColumn.implicitHeight + 24
                radius: 4
                color: Theme.dangerBg
                border.color: Theme.dangerBorder
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10
                    Label { text: "⚠"; font.family: "Noto Sans Symbols2"; font.pointSize: Theme.fontHuge; color: Theme.dangerText; Layout.alignment: Qt.AlignTop }
                    ColumnLayout {
                        id: warnColumn
                        Layout.fillWidth: true
                        spacing: 4
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.dangerText
                            font.family: Theme.titleFamily
                            font.weight: Font.Bold
                            font.pointSize: Theme.fontMedium
                            text: root.exact ? "This overwrites files and removes everything the backup doesn't contain."
                                             : "This overwrites files on a drive that already holds a DJ library."
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.dangerText
                            text: (root.preview.filesToWrite || 0) + " file(s) on " + confirmDialog.confirmTarget
                                + " will be written from the backup"
                                + (root.exact ? " and " + (root.preview.extras || 0) + " file(s) or folder(s) not in the backup removed." : ".")
                                + " Audio files on the drive are not backed up first."
                        }
                    }
                }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 4
                Label { text: "Drive"; color: Theme.textMuted; font.pointSize: Theme.fontSmall }
                Label { font.family: Theme.dataFamily; text: root.selectedDisk ? root.selectedDisk.mountPoint + "  ·  " + Theme.humanBytes(root.selectedDisk.capacityBytes) : "" }
                Label { text: "From"; color: Theme.textMuted; font.pointSize: Theme.fontSmall }
                Label { Layout.fillWidth: true; elide: Text.ElideMiddle; font.family: Theme.dataFamily; text: (root.info.label || "") + "  ·  " + root.friendlyTimestamp(root.info.createdAt) }
                Label { text: "Mode"; color: Theme.textMuted; font.pointSize: Theme.fontSmall }
                Label { text: root.exact ? "Exact restore" : "Overlay (keeps other files)" }
            }
            Label {
                visible: root.needsTypedConfirmation
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                text: "Type " + confirmDialog.confirmTarget + " to confirm"
            }
            TextField {
                id: confirmField
                objectName: "confirmField"
                visible: root.needsTypedConfirmation
                Layout.fillWidth: true
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 16
        contentWidth: availableWidth
        ScrollBar.vertical: BigScrollBar {}

        ColumnLayout {
            width: parent.width
            spacing: 16

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                text: "Puts a full stick backup onto a drive -- a fresh stick after losing one, or the same stick after a bad "
                    + "night. Nothing is written until you confirm."
            }
            StickWriteWarning {
                visible: root.controller.restoring === true
                text: "Restoring. Do not remove the drive until this finishes."
            }
            Label {
                Layout.fillWidth: true
                visible: root.controller.errorMessage.length > 0
                wrapMode: Text.WordWrap
                color: Theme.danger
                text: root.controller.errorMessage
            }

            GroupBox {
                label: Subtitle { text: "1. Choose a backup" }
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        TextField {
                            objectName: "archivePathField"
                            Layout.fillWidth: true
                            font.family: Theme.dataFamily
                            text: root.controller.archivePath
                            placeholderText: "Path to a Seabass stick backup (.zip)"
                            onEditingFinished: {
                                root.controller.archivePath = text;
                                root.applySelection(root.selectedIndex);
                            }
                        }
                        Button { text: "Choose File…"; onClicked: archiveDialog.open() }
                    }
                    RowLayout {
                        visible: root.archiveReady
                        spacing: 8
                        Label { text: root.info.label }
                        Label { text: "·"; color: Theme.textMuted }
                        Label { text: root.friendlyTimestamp(root.info.createdAt) }
                        Label { text: "·"; color: Theme.textMuted }
                        Label { font.family: Theme.dataFamily; text: Theme.humanBytes(root.info.bytes) }
                        Label { text: "·"; color: Theme.textMuted }
                        Label { font.family: Theme.dataFamily; text: root.info.entries + " entries" }
                        StatusBadge {
                            label: root.info.status === "complete" ? "VERIFIED" : "INCOMPLETE"
                            badgeColor: root.info.status === "complete" ? Theme.good : Theme.warnIcon
                            tooltipText: root.info.status === "complete"
                                ? "Every entry was verified when this backup was written."
                                : "This backup was interrupted or its database was not captured; restoring gives you what it holds."
                        }
                    }
                }
            }

            GroupBox {
                label: Subtitle { text: "2. Choose a drive" }
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        text: root.disks.length > 0
                            ? "Every removable drive Seabass can see. A drive must be formatted and mounted to receive a restore."
                            : "No removable drives found. Plug one in and it will appear here."
                    }
                    ButtonGroup { id: driveGroup }
                    Repeater {
                        model: root.disks
                        delegate: RadioButton {
                            id: driveRadio
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            ButtonGroup.group: driveGroup
                            enabled: modelData.usable === true && root.controller.busy !== true
                            checked: root.selectedIndex === index
                            onToggled: if (checked) root.applySelection(index)
                            contentItem: RowLayout {
                                x: driveRadio.indicator.width + driveRadio.spacing
                                width: driveRadio.width - x
                                spacing: 10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Label { text: driveRadio.modelData.label.length > 0 ? driveRadio.modelData.label : "(untitled)"; color: Theme.text }
                                    Label {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        color: Theme.textMuted
                                        font.family: Theme.dataFamily
                                        font.pointSize: Theme.fontTiny
                                        text: driveRadio.modelData.hasNoFilesystem
                                            ? driveRadio.modelData.wholeDiskPath + " · no filesystem"
                                            : (driveRadio.modelData.mounted
                                               ? driveRadio.modelData.mountPoint + (driveRadio.modelData.rootEntries.length > 0 ? " · " + driveRadio.modelData.rootEntries.join(", ") : " · empty")
                                               : driveRadio.modelData.devicePath + " · not mounted")
                                    }
                                }
                                StatusBadge {
                                    label: driveRadio.modelData.hasDjLibrary ? "Has a DJ library"
                                        : (driveRadio.modelData.hasNoFilesystem ? "Blank" : (driveRadio.modelData.mounted ? "Mounted" : "Not mounted"))
                                    badgeColor: driveRadio.modelData.hasDjLibrary ? Theme.warnIcon : (driveRadio.modelData.usable ? Theme.good : Theme.textMuted)
                                }
                                Button {
                                    visible: driveRadio.modelData.hasNoFilesystem === true
                                    text: "Format it first…"
                                    flat: true
                                    onClicked: root.formatUsbRequested()
                                }
                                Label { font.family: Theme.dataFamily; color: Theme.textMuted; text: Theme.humanBytes(driveRadio.modelData.capacityBytes) }
                            }
                        }
                    }
                }
            }

            GroupBox {
                label: Subtitle { text: "3. What will happen" }
                Layout.fillWidth: true
                visible: root.archiveReady && root.selectedDisk !== null && root.selectedDisk.usable === true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6
                    GridLayout {
                        columns: 2
                        columnSpacing: 16
                        rowSpacing: 6
                        Label { text: "Restore"; color: Theme.textMuted; font.pointSize: Theme.fontSmall }
                        Label {
                            objectName: "previewLabel"
                            font.family: Theme.dataFamily
                            text: root.preview.filesToWrite !== undefined
                                ? root.preview.filesToWrite + " file(s) to write (" + Theme.humanBytes(root.preview.bytesToWrite) + "), "
                                  + root.preview.filesUnchanged + " already up to date"
                                : "Analyzing…"
                        }
                        Label { text: "On the drive"; color: Theme.textMuted; font.pointSize: Theme.fontSmall }
                        Label {
                            font.family: Theme.dataFamily
                            text: (root.preview.extras || 0) + " file(s) or folder(s) not in the backup"
                                + (root.exact ? " -- will be removed" : " -- kept")
                        }
                        Label { text: "Free space"; color: Theme.textMuted; font.pointSize: Theme.fontSmall }
                        Label {
                            font.family: Theme.dataFamily
                            color: root.preview.enoughFreeSpace === false ? Theme.danger : Theme.good
                            text: Theme.humanBytes(root.preview.freeBytes) + " available, " + Theme.humanBytes(root.preview.bytesToWrite) + " needed"
                        }
                    }
                    CheckBox {
                        objectName: "exactCheckBox"
                        text: "Exact restore -- also remove files and folders that aren't in the backup"
                        checked: root.exact
                        onCheckedChanged: root.exact = checked
                    }
                    Label {
                        Layout.leftMargin: 28
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                        text: "Asks you to type the drive's name before starting."
                    }
                }
            }

            // Progress while restoring.
            Frame {
                Layout.fillWidth: true
                visible: root.controller.restoring === true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Label { font.bold: true; text: root.controller.phase === "removing" ? "Removing files not in the backup" : (root.controller.phase === "checking" ? "Checking the restored library" : "Restoring files") }
                        Item { Layout.fillWidth: true }
                        Button { objectName: "cancelRestoreButton"; text: "Cancel"; onClicked: root.controller.cancel() }
                    }
                    ProgressBar {
                        id: restoreBar
                        Layout.fillWidth: true
                        Layout.preferredHeight: 16
                        indeterminate: root.controller.bytesTotal <= 0
                        value: root.controller.bytesTotal > 0 ? root.controller.bytesDone / root.controller.bytesTotal : 0
                        background: Rectangle { implicitHeight: 16; radius: 8; color: Theme.surface; border.color: Theme.borderSubtle }
                        contentItem: Item {
                            implicitHeight: 16
                            clip: true
                            Rectangle { visible: !restoreBar.indeterminate; height: parent.height; width: restoreBar.visualPosition * parent.width; radius: 8; color: Theme.accent }
                        }
                    }
                    RowLayout {
                        spacing: 8
                        Label { font.family: Theme.dataFamily; text: root.controller.filesDone + " / " + root.controller.filesTotal + " files" }
                        Label { text: "·"; color: Theme.textMuted }
                        Label { font.family: Theme.dataFamily; text: Theme.humanBytes(root.controller.bytesDone) + " of " + Theme.humanBytes(root.controller.bytesTotal) }
                    }
                    Label { Layout.fillWidth: true; elide: Text.ElideMiddle; color: Theme.textMuted; font.pointSize: Theme.fontSmall; font.family: Theme.dataFamily; text: root.controller.currentFile }
                }
            }

            // Result report.
            Frame {
                Layout.fillWidth: true
                visible: root.result.filesWritten !== undefined
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6
                    Label { font.bold: true; text: "Result" }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: root.controller.errorMessage.length > 0 ? Theme.danger : Theme.good
                        text: root.controller.statusMessage
                    }
                    Label {
                        font.family: Theme.dataFamily
                        text: root.result.filesWritten + " written · " + root.result.filesUnchanged + " unchanged · "
                            + root.result.directoriesCreated + " folders created · " + root.result.extrasRemoved + " removed"
                    }
                    Label {
                        visible: root.result.databaseChecked === true
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: (root.result.missingTracks || []).length === 0 ? Theme.good : Theme.danger
                        text: (root.result.missingTracks || []).length === 0
                            ? "Engine database opens and every track it references is present."
                            : "Engine database opens, but " + root.result.missingTracks.length + " referenced track(s) are missing:"
                    }
                    Repeater {
                        model: (root.result.missingTracks || []).slice(0, 20)
                        delegate: Label { required property string modelData; Layout.leftMargin: 16; font.family: Theme.dataFamily; font.pointSize: Theme.fontSmall; color: Theme.danger; text: modelData }
                    }
                    Repeater {
                        model: (root.result.rejected || []).concat(root.result.writeErrors || []).concat(root.result.warnings || [])
                        delegate: Label { required property string modelData; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pointSize: Theme.fontSmall; color: Theme.conflictText; text: modelData }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "openConfirmButton"
                    text: "Restore Onto " + (root.selectedDisk ? (root.selectedDisk.label.length > 0 ? root.selectedDisk.label : "Drive") : "Drive") + "…"
                    highlighted: true
                    enabled: root.canRestore
                    onClicked: confirmDialog.open()
                }
            }
        }
    }
}
