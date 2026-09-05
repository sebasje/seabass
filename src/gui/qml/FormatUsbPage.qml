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
    // Tracked by the selected disk's own wholeDiskPath, not a plain
    // index -- selectedIndex/selectedDisk below are derived by searching
    // the *current* disks list for it on every read, so a background
    // refresh (a hotplug event while this page is already open) can
    // never leave a stale index silently pointing at a different
    // physical disk than the one actually chosen. Empty ("") means
    // nothing is selected, and this deliberately starts and stays that
    // way until the person clicks a drive themselves: formatting is
    // destructive enough that this page never guesses on anyone's
    // behalf, not even when opened from a specific stick's own "Format
    // USB Stick" action card -- picking the drive here is always a
    // separate, conscious step.
    property string selectedWholeDiskPath: ""
    readonly property int selectedIndex: {
        for (var i = 0; i < root.disks.length; ++i) {
            if (root.disks[i].wholeDiskPath === root.selectedWholeDiskPath) {
                return i;
            }
        }
        return -1;
    }
    readonly property var selectedDisk: root.selectedIndex >= 0 ? root.disks[root.selectedIndex] : null
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

    function applySelection(index) {
        if (index < 0 || index >= root.disks.length) {
            root.selectedWholeDiskPath = "";
            return;
        }
        var disk = root.disks[index];
        root.selectedWholeDiskPath = disk.wholeDiskPath;
        root.selectedFilesystem = controller.recommendedFilesystem(disk.capacityBytes);
        volumeLabelField.text = disk.hasNoFilesystem ? "" : disk.label;
        if (disk.hasDjLibrary) {
            djLibraryWarningDialog.open();
        }
    }

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

    // Shown every time selection lands on a drive with a recognized DJ
    // library (see applySelection() above) -- separate from confirmDialog
    // below, which gates the actual write. This one's job is just making
    // sure a real, existing library was actually noticed before the
    // person goes any further, not confirming the write itself.
    Dialog {
        id: djLibraryWarningDialog
        objectName: "djLibraryWarningDialog"
        anchors.centerIn: parent
        modal: true
        width: 420
        title: "This Drive Has a DJ Library"
        // No Escape/click-outside dismissal -- must be acknowledged via
        // its own button, so it can't be skipped past accidentally.
        closePolicy: Popup.NoAutoClose

        footer: DialogButtonBox {
            Button {
                objectName: "djLibraryWarningAcknowledgeButton"
                text: "I Understand"
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }

        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: "\"" + (root.selectedDisk ? root.selectedDisk.label : "") + "\" has an existing DJ "
                + "library on it: tracks, playlists, cues, all of it. Formatting will erase it permanently."
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
                            objectName: "confirmDialogDataLossLabel"
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.dangerText
                            text: "All data on \"" + (root.selectedDisk ? root.selectedDisk.label : "") + "\" ("
                                + (root.selectedDisk ? root.selectedDisk.wholeDiskPath : "") + ") will be lost "
                                + "permanently, including any existing DJ library. This cannot be undone."
                        }
                        Label {
                            objectName: "confirmDialogDoubleCheckLabel"
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.dangerText
                            font.weight: Font.Bold
                            text: "Double-check that you've selected the correct storage device before "
                                + "continuing."
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
                        if (root.selectedDisk.hasNoFilesystem) return "Empty, no files";
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
                text: "Prepares a drive so it's readable on CDJs, XDJs, and Denon Engine players. Pick a "
                    + "drive below; Seabass already knows which format fits it."
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
                            objectName: "driveRadio"
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            ButtonGroup.group: driveGroup
                            checked: root.selectedIndex === index
                            // onClicked, not onToggled: clicking the
                            // already-selected drive again is meant to
                            // unselect it, but re-clicking an already-
                            // checked exclusive RadioButton never actually
                            // changes `checked` (nothing to toggle), so
                            // onToggled would never fire for that case.
                            // onClicked fires on every press+release
                            // inside the button regardless of whether
                            // checked changed, so it can tell the two
                            // cases apart itself.
                            onClicked: root.applySelection(root.selectedIndex === index ? -1 : index)

                            // Control.leftPadding (tried first) turned out
                            // not to reliably shift a RowLayout-based
                            // contentItem clear of the indicator -- unlike
                            // Label.leftPadding (used below for the format
                            // RadioButtons), which Text's own internal
                            // layout genuinely respects regardless of
                            // whatever x/width Control's resizeContent()
                            // externally imposes, RowLayout has no
                            // comparable internal padding concept of its
                            // own. Confirmed via an actual offscreen
                            // screenshot (Xvfb + xdotool + xwd, cropped
                            // and zoomed 4x) that leftPadding alone still
                            // left the checked indicator drawn directly
                            // over "WHALESHARK2"'s first two letters.
                            // Layout.leftMargin on the RowLayout's own
                            // first child is a real Qt Quick Layouts
                            // property RowLayout consumes internally when
                            // positioning its children -- immune to the
                            // same external-stomping problem since
                            // nothing outside the RowLayout ever touches
                            // it.
                            contentItem: RowLayout {
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: driveRadio.indicator.width + driveRadio.spacing
                                    spacing: 2
                                    Label {
                                        text: driveRadio.modelData.label
                                        font.pointSize: Theme.fontNormal
                                    }
                                    Label {
                                        objectName: "driveSubtitleLabel"
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
                                    objectName: "driveStatusBadge"
                                    // "In use" used to render exactly like
                                    // "Blank" (both Theme.good) -- correct
                                    // for a genuinely empty drive, but
                                    // misleading for one that already has
                                    // real (non-DJ-library) files on it:
                                    // formatting loses those too.
                                    label: driveRadio.modelData.hasDjLibrary ? "Has a DJ library"
                                        : (driveRadio.modelData.hasNoFilesystem ? "Blank" : "Data will be lost")
                                    badgeColor: driveRadio.modelData.hasDjLibrary ? Theme.danger
                                        : (driveRadio.modelData.hasNoFilesystem ? Theme.good : Theme.danger)
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
                objectName: "formatGroupBox"
                label: Subtitle { text: "2. Choose a format" }
                Layout.fillWidth: true
                // Greyed out (not hidden) until a drive is picked -- shows
                // what's coming next rather than making the page jump
                // around every time the drive selection changes.
                enabled: root.selectedDisk !== null
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
                        id: fat32Radio
                        objectName: "fat32Radio"
                        Layout.fillWidth: true
                        Layout.maximumWidth: 560
                        // Without this, Layout.fillWidth can't actually
                        // shrink the control below its own implicitWidth,
                        // and a wrapping Label's implicitWidth is still
                        // its full *unwrapped* single-line width (wrapMode
                        // only affects rendering once a width is already
                        // imposed, not the implicit-size calculation) --
                        // so the button (and the whole page) was forced at
                        // least as wide as this row's longest unwrapped
                        // string, overflowing a narrower window instead of
                        // the text ever actually wrapping.
                        Layout.minimumWidth: 0
                        ButtonGroup.group: fsGroup
                        checked: root.selectedFilesystem === "fat32"
                        enabled: root.fat32Available
                        onToggled: if (checked) root.selectedFilesystem = "fat32"

                        // A custom contentItem, so this can wrap -- the
                        // default RadioButton contentItem doesn't. Uses
                        // Label's own `leftPadding` (a real Text/Label
                        // property that Text's internal layout genuinely
                        // respects, both for where it paints and for how
                        // much width is left to wrap/elide within), NOT a
                        // manual `x`/`width` binding: Control's own
                        // resizeContent() imperatively calls
                        // contentItem->setPosition()/setSize() on every
                        // relayout, silently overwriting a plain `x:`/
                        // `width:` QML binding out from under it. That's
                        // what caused the checked-state overlap bug this
                        // page shipped with twice already -- confirmed
                        // via an actual offscreen screenshot
                        // (DISPLAY=:99 xdotool + xwd, not just a
                        // synthetic test) that the checked radio's own
                        // indicator visibly overlapped "FAT32"'s first
                        // couple of characters. leftPadding is left
                        // completely alone by resizeContent(), so it
                        // can't be clobbered the same way.
                        contentItem: Label {
                            objectName: "fat32Label"
                            leftPadding: fat32Radio.indicator.width + fat32Radio.spacing
                            wrapMode: Text.WordWrap
                            verticalAlignment: Text.AlignVCenter
                            text: "FAT32 · Works on every player"
                                + (root.recommendedFilesystem === "fat32" ? "  (Recommended)" : "")
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 560
                        Layout.leftMargin: 28
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                        text: root.fat32Available
                            ? "Very long recordings or lossless masters over 4 GB won't fit. Everything else "
                                + "is unaffected."
                            : "Not available for this drive: Windows can't create a FAT32 filesystem this large."
                    }

                    RadioButton {
                        id: exfatRadio
                        objectName: "exfatRadio"
                        Layout.fillWidth: true
                        Layout.maximumWidth: 560
                        // See fat32Radio's own comment above.
                        Layout.minimumWidth: 0
                        ButtonGroup.group: fsGroup
                        checked: root.selectedFilesystem === "exfat"
                        onToggled: if (checked) root.selectedFilesystem = "exfat"

                        // See fat32Radio's own comment: leftPadding, not
                        // a manual x/width binding.
                        contentItem: Label {
                            objectName: "exfatLabel"
                            leftPadding: exfatRadio.indicator.width + exfatRadio.spacing
                            wrapMode: Text.WordWrap
                            verticalAlignment: Text.AlignVCenter
                            text: "exFAT · Modern players, no file-size limit"
                                + (root.recommendedFilesystem === "exfat" ? "  (Recommended)" : "")
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 560
                        Layout.leftMargin: 28
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                        text: "CDJ-3000(X), OPUS-QUAD, XDJ-XZ/RX3/AZ/AN, OMNIS-DUO, Denon Prime & Engine OS "
                            + "(2022 or newer). Older players won't read it at all."
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 560
                        Layout.topMargin: 6
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                        text: "Drives over 32 GB always use exFAT: Windows itself can't create a FAT32 "
                            + "filesystem larger than that, so bigger sticks aren't limited by the old 32 GB "
                            + "ceiling, they just skip FAT32 automatically."
                    }
                }
            }

            GroupBox {
                objectName: "nameGroupBox"
                label: Subtitle { text: "3. Name it" }
                Layout.fillWidth: true
                // See "2. Choose a format" above: greyed out, not hidden.
                enabled: root.selectedDisk !== null
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4
                    TextField {
                        id: volumeLabelField
                        objectName: "volumeLabelField"
                        Layout.fillWidth: true
                        Layout.maximumWidth: 320
                        placeholderText: "Name this drive..."
                        // FAT32/exFAT volume labels are limited by the
                        // filesystem spec itself (not a Seabass choice):
                        // 11 characters for FAT32, 15 for exFAT.
                        maximumLength: root.selectedFilesystem === "fat32" ? 11 : 15
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 320
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                        text: root.selectedFilesystem === "fat32"
                            ? "Up to 11 characters for FAT32."
                            : "Up to 15 characters for exFAT."
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
