import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// Prepares a USB drive for real DJ hardware -- FAT32 or exFAT, always
// MBR, picked in plain language with a size-based recommendation. Pushed
// as a top-level page (not per-stick, unlike most of this app's other
// actions) because it must offer blank/unrecognized drives too, which the
// per-stick ActionCard grid on StickListPage.qml structurally can't show
// (that grid only ever renders for a drive StickListPage already
// recognizes as a DJ stick). See the approved plan for the filesystem
// research and hardware-compatibility list this page's copy is based on.
Page {
    id: root

    // Untyped and required, not self-instantiated -- a plain JS object can
    // stand in for the whole controller in tests/qml/tst_FormatUsbPage.qml
    // (same technique tst_TrackDetailPage.qml already uses for its own
    // controllers), so this page never needs a real disk, real udisks2/
    // PowerShell call, or real RemovableMediaLocator result to be tested.
    // The real instance is supplied where this page is pushed (Main.qml).
    required property var controller

    readonly property var disks: controller.disks
    property int selectedIndex: -1
    readonly property var selectedDisk: (root.selectedIndex >= 0 && root.selectedIndex < root.disks.length)
        ? root.disks[root.selectedIndex] : null
    property string selectedFilesystem: "fat32"
    property bool fat32Available: root.selectedDisk === null
        || controller.fat32MaxBytes < 0
        || root.selectedDisk.capacityBytes <= controller.fat32MaxBytes
    // The recommendation always tracks the selected drive's size, whether
    // or not the person has manually picked the other option -- so
    // deviating from it stays visible rather than the tag just following
    // whatever's currently checked.
    readonly property string recommendedFilesystem: root.selectedDisk
        ? controller.recommendedFilesystem(root.selectedDisk.capacityBytes) : ""

    // Preselect the drive that's just been plugged in and has nothing on
    // it yet -- the realistic "I want to format this stick" scenario --
    // falling back to the first drive Seabass can see at all.
    function pickDefaultDrive() {
        for (var i = 0; i < root.disks.length; ++i) {
            if (root.disks[i].hasNoFilesystem) {
                return i;
            }
        }
        return root.disks.length > 0 ? 0 : -1;
    }

    function applySelection(index) {
        root.selectedIndex = index;
        if (root.selectedDisk === null) {
            return;
        }
        root.selectedFilesystem = controller.recommendedFilesystem(root.selectedDisk.capacityBytes);
        volumeLabelField.text = root.selectedDisk.hasNoFilesystem ? "" : root.selectedDisk.label;
    }

    onDisksChanged: root.applySelection(root.pickDefaultDrive())
    Component.onCompleted: root.applySelection(root.pickDefaultDrive())

    header: ToolBar {
        background: Rectangle { color: Theme.surface }
        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 12
            BackBreadcrumb {
                title: "Format USB Stick"
                backEnabled: !controller.busy
                backDisabledTooltip: "Wait for formatting to finish before leaving this page"
                onHomeRequested: root.StackView.view.pop(null)
                onBackRequested: root.StackView.view.pop(null)
            }
            Item { Layout.fillWidth: true }
            BusyIndicator { running: controller.busy; visible: controller.busy; implicitWidth: 20; implicitHeight: 20 }
        }
    }

    Dialog {
        id: confirmDialog
        objectName: "confirmDialog"
        anchors.centerIn: parent
        modal: true
        width: 460
        title: "Format " + (root.selectedDisk ? root.selectedDisk.label : "") + "?"

        // The confirmation text a person must type back exactly -- the
        // new volume label about to be written, not necessarily the
        // drive's current one (a blank drive has no current label at
        // all). Matching against what the label field will actually
        // write is the one thing true for both a blank drive and one
        // being reused.
        readonly property string confirmTarget: volumeLabelField.text.length > 0
            ? volumeLabelField.text : "(untitled)"

        footer: DialogButtonBox {
            Button {
                objectName: "formatAcceptButton"
                text: "Format Drive"
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                enabled: confirmField.text === confirmDialog.confirmTarget
            }
            Button { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onOpened: confirmField.text = ""
        onAccepted: controller.format(root.selectedDisk.wholeDiskPath, root.selectedFilesystem, volumeLabelField.text)

        ColumnLayout {
            width: parent.width
            spacing: 14

            // The strongest warning in this app -- every other destructive
            // action here backs its data up first; formatting has no such
            // safety net, so this is the first genuinely irreversible
            // action in Seabass. Deliberately heavier than this app's
            // usual Dialog body text.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: warnColumn.implicitHeight + 24
                radius: 4
                color: Theme.dangerBg
                border.color: Theme.dangerBorder
                border.width: 1

                RowLayout {
                    id: warnRow
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    Label {
                        text: "⚠"
                        font.pointSize: Theme.fontHuge
                        color: Theme.dangerText
                        Layout.alignment: Qt.AlignTop
                    }
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
                            text: "This permanently erases everything on this drive."
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.dangerText
                            text: "Every file -- including any existing DJ library -- will be gone for good, "
                                + "and this can't be undone. Make sure this is the right drive before continuing."
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
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.family: Theme.dataFamily
                    text: root.selectedDisk ? (root.selectedDisk.wholeDiskPath + "  ·  "
                        + Math.round(root.selectedDisk.capacityBytes / (1000*1000*1000)) + " GB") : ""
                }
                Label { text: "Contains"; color: Theme.textMuted; font.pointSize: Theme.fontSmall }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.family: Theme.dataFamily
                    text: {
                        if (!root.selectedDisk) return "";
                        if (root.selectedDisk.hasNoFilesystem) return "Empty -- no files";
                        var entries = root.selectedDisk.rootEntries;
                        return entries && entries.length > 0 ? entries.join(", ") : "(unknown)";
                    }
                }
                Label { text: "Format"; color: Theme.textMuted; font.pointSize: Theme.fontSmall }
                Label {
                    font.family: Theme.dataFamily
                    text: (root.selectedFilesystem === "fat32" ? "FAT32" : "exFAT") + " (MBR)"
                }
                Label { text: "New label"; color: Theme.textMuted; font.pointSize: Theme.fontSmall }
                Label { font.family: Theme.dataFamily; text: confirmDialog.confirmTarget }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                text: "Type " + confirmDialog.confirmTarget + " to confirm"
            }
            TextField {
                id: confirmField
                objectName: "confirmField"
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
                text: "Prepares a drive so it's readable on CDJs, XDJs, and Denon Engine players -- no "
                    + "filesystem knowledge needed. Pick a drive below; Seabass already knows which format fits it."
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.danger
                visible: controller.errorMessage.length > 0
                text: controller.errorMessage
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.good
                visible: controller.statusMessage.length > 0
                text: controller.statusMessage
            }

            GroupBox {
                label: Subtitle { text: "1. Choose a drive" }
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        text: root.disks.length > 0
                            ? "Every removable drive Seabass can see, formatted or not."
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
                            checked: root.selectedIndex === index
                            onToggled: if (checked) root.applySelection(index)

                            contentItem: RowLayout {
                                x: driveRadio.indicator.width + driveRadio.spacing
                                width: driveRadio.width - x
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Label {
                                        text: driveRadio.modelData.label
                                        font.pointSize: Theme.fontNormal
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        color: Theme.textMuted
                                        font.family: Theme.dataFamily
                                        font.pointSize: Theme.fontTiny
                                        text: driveRadio.modelData.hasNoFilesystem
                                            ? (driveRadio.modelData.wholeDiskPath + " · empty")
                                            : (driveRadio.modelData.wholeDiskPath + " · "
                                                + (driveRadio.modelData.rootEntries.length > 0
                                                    ? driveRadio.modelData.rootEntries.join(", ") : "empty"))
                                    }
                                }
                                StatusBadge {
                                    label: driveRadio.modelData.hasDjLibrary ? "Has a DJ library"
                                        : (driveRadio.modelData.hasNoFilesystem ? "Blank" : "In use")
                                    badgeColor: driveRadio.modelData.hasDjLibrary ? Theme.warnIcon : Theme.good
                                }
                                Label {
                                    font.family: Theme.dataFamily
                                    color: Theme.textMuted
                                    text: Math.round(driveRadio.modelData.capacityBytes / (1000*1000*1000)) + " GB"
                                }
                            }
                        }
                    }
                }
            }

            GroupBox {
                label: Subtitle { text: "2. Choose a format" }
                Layout.fillWidth: true
                visible: root.selectedDisk !== null
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        text: root.selectedDisk
                            ? ("Recommended for this " + Math.round(root.selectedDisk.capacityBytes / (1000*1000*1000))
                                + " GB drive, based on the widest player compatibility.")
                            : ""
                    }

                    ButtonGroup { id: fsGroup }

                    RadioButton {
                        Layout.fillWidth: true
                        text: "Works on every player" + (root.recommendedFilesystem === "fat32" ? "  (Recommended)" : "")
                        ButtonGroup.group: fsGroup
                        checked: root.selectedFilesystem === "fat32"
                        enabled: root.fat32Available
                        onToggled: if (checked) root.selectedFilesystem = "fat32"
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.leftMargin: 28
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                        text: root.fat32Available
                            ? "Very long recordings or lossless masters over 4 GB won't fit -- everything else is unaffected."
                            : "Not available for this drive -- Windows can't create a FAT32 filesystem this large."
                    }

                    RadioButton {
                        Layout.fillWidth: true
                        text: "Modern players, no file-size limit" + (root.recommendedFilesystem === "exfat" ? "  (Recommended)" : "")
                        ButtonGroup.group: fsGroup
                        checked: root.selectedFilesystem === "exfat"
                        onToggled: if (checked) root.selectedFilesystem = "exfat"
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.leftMargin: 28
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                        text: "CDJ-3000(X), OPUS-QUAD, XDJ-XZ/RX3/AZ/AN, OMNIS-DUO, Denon Prime & Engine OS "
                            + "(2022 or newer). Older players won't read it at all."
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                        text: "Drives over 32 GB always use exFAT -- Windows itself can't create a FAT32 "
                            + "filesystem larger than that, so bigger sticks aren't limited by the old 32 GB "
                            + "ceiling, they just skip FAT32 automatically."
                    }
                }
            }

            GroupBox {
                label: Subtitle { text: "3. Name it" }
                Layout.fillWidth: true
                visible: root.selectedDisk !== null
                ColumnLayout {
                    anchors.fill: parent
                    TextField {
                        id: volumeLabelField
                        objectName: "volumeLabelField"
                        Layout.fillWidth: true
                        Layout.maximumWidth: 320
                        placeholderText: "Name this drive..."
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "openConfirmButton"
                    text: "Format Drive…"
                    enabled: root.selectedDisk !== null && !controller.busy
                    onClicked: confirmDialog.open()
                }
            }
        }
    }
}
