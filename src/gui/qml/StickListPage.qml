import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SeabassGui

// A Page, not a plain Item, specifically so it gets the same
// Material-style implicit background every other page in this app gets
// for free -- a plain Item has no background mechanism at all, which is
// exactly why this page (the very first one shown) kept rendering the
// Qt-default white despite the window/StackView-level background fixes
// applied elsewhere, while every `Page`-rooted page rendered correctly.
Page {
    id: root
    required property var mediaController
    required property var playbackController
    required property var appSettingsController
    required property var backupAdvisor
    // The edit-lock registry (EditSessionRegistry singleton; a fake in
    // tests): which libraries another instance is editing right now.
    property var editRegistry: typeof EditSessionRegistry !== "undefined" ? EditSessionRegistry : null
    function refreshLocks() {
        if (root.editRegistry !== null && root.editRegistry !== undefined) {
            root.editRegistry.refreshLocks();
        }
    }
    function isLockedByOther(libraryId) {
        return libraryId.length > 0 && root.editRegistry !== null && root.editRegistry !== undefined
            && root.editRegistry.lockedByOther.indexOf(libraryId) >= 0;
    }
    function explainLock(libraryId) {
        lockedDialog.openFor(libraryId, root.editRegistry ? root.editRegistry.lockHolder(libraryId) : {});
    }

    // Coming back from a backup or restore: the advice is stale.
    StackView.onActivated: {
        root.backupAdvisor.reassessAll();
        root.refreshLocks();
    }
    // Another instance taking or dropping a lock shows up within 2 s
    // while this page is in front.
    Timer {
        interval: 2000
        repeat: true
        running: root.StackView.status === StackView.Active
        onTriggered: root.refreshLocks()
    }
    // Opening a library that is not on removable media: a restored stick
    // backup, a copy on an internal disk, a test fixture. The folder to
    // pick is the one *holding* PIONEER / Engine Library, which is what a
    // stick's own root looks like -- see MediaController::openFolder.
    FolderDialog {
        id: openFolderDialog
        objectName: "openFolderDialog"
        title: "Open a folder holding a rekordbox or Engine DJ library"
        onAccepted: {
            var message = root.mediaController.openFolder(
                selectedFolder.toString().replace(/^file:\/\//, ""));
            if (message.length > 0) {
                openFolderError.text = message;
                openFolderError.open();
            }
        }
    }
    Dialog {
        id: openFolderError
        objectName: "openFolderError"
        property alias text: openFolderErrorLabel.text
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Cannot open that folder"
        standardButtons: Dialog.Ok
        Label {
            id: openFolderErrorLabel
            width: Math.min(implicitWidth, 420)
            wrapMode: Text.WordWrap
        }
    }
    LockedLibraryDialog {
        id: lockedDialog
        objectName: "lockedDialog"
        onRemoveLockRequested: {
            if (root.editRegistry) {
                root.editRegistry.removeLock(lockedDialog.libraryId);
            }
        }
    }
    signal browseRequested(string stickLabel, string rekordboxPath, string enginePath)
    // Deduplication and Backups are hub pages now (see
    // DuplicatesHubPage.qml / BackupsHubPage.qml), each fanning out to two
    // sub-pages that used to be separate top-level cards here
    // (Deduplication + Clean Up Duplicates; Local Cue Backup + Manage
    // Backups). Library Health used to be a card inside the Deduplication
    // hub too, but it isn't a duplicate-tracks concern (it spans all three
    // catalogs looking for missing files, not just consolidating copies),
    // so it got promoted to its own top-level entry instead.
    signal duplicateTracksHubRequested(string stickLabel, string rekordboxPath, string enginePath)
    signal libraryHealthRequested(string stickLabel, string rekordboxPath, string enginePath)
    signal stickStatisticsRequested(string stickLabel, string rekordboxPath, string enginePath)
    signal engineLibraryCreatorRequested(string stickLabel, string rekordboxPath)
    signal settingsRequested(string stickLabel, string pioneerRoot)
    signal syncRequested(string stickLabel, string rekordboxPath, string enginePath)
    signal appSettingsRequested()
    signal backupsHubRequested(string stickLabel, string rekordboxPath, string enginePath, string mountPoint, string devicePath)
    signal aboutRequested()
    signal donationRequested()
    signal formatUsbRequested()
    // mountPoint (or, for a not-yet-mounted stick, devicePath) preselects
    // the target drive; both empty means "pick one there". archivePath
    // preselects the backup (the advisor's pick), empty picks the newest.
    signal restoreStickBackupRequested(string mountPoint, string devicePath, string archivePath)
    // General entry point (Backups block above the stick list): not
    // tied to any particular stick, so mountPoint/devicePath/archivePath
    // (or stickLabel/rekordboxPath/enginePath) may all be empty; the
    // target page picks its own drive/backup/library from within itself.
    signal localCueRequested(string stickLabel, string rekordboxPath, string enginePath)
    // Copy the library on another mounted stick onto this one -- either a
    // fresh backup stick (targetHasLibrary false) or an update of an older
    // copy (true). The source's catalog paths come from the advisor.
    signal cloneStickRequested(string sourceLabel, string sourceRekordboxPath, string sourceEnginePath,
                               string targetMountPoint, string targetLabel, bool targetHasLibrary)

    // A subtle brand watermark in the corner of the very first page shown --
    // same "Seabass / DJ USB Stick Management" text as AboutPage.qml, just
    // bigger and dimmer, since here it's sitting in the background behind
    // real content rather than being the page's own subject. Declared
    // before the ColumnLayout below (and given no width/height of its own)
    // so it never participates in layout and never intercepts input --
    // it's purely decorative.
    ColumnLayout {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 24
        spacing: 4
        opacity: 0.25

        Label {
            text: "Seabass"
            font.bold: true
            font.pointSize: Theme.baseFontPointSize * 3.2
        }
        Label {
            text: "DJ USB Stick Management"
            font.pointSize: Theme.baseFontPointSize * 1.6
            color: Qt.lighter(Theme.accent, 1.3)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            PageTitle {
                text: "Home"
                level: "page"
            }
            Item { Layout.fillWidth: true }
            ToolButton {
                objectName: "openFolderButton"
                text: "📂"
                font.pointSize: Theme.fontLarge
                ToolTip.visible: hovered
                ToolTip.text: "Open a library from a folder -- a restored stick backup, or a copy on this computer"
                onClicked: openFolderDialog.open()
            }
            ToolButton {
                // Plain "ℹ️"/"⚙️" (with the emoji variation selector) render
                // in the system's color emoji font instead of a flat
                // monochrome glyph -- same full-color look as the
                // ActionCard icons further down the page, not a
                // font.family override forcing the outline symbol style
                // the way this used to.
                text: "ℹ️"
                font.pointSize: Theme.fontLarge
                ToolTip.visible: hovered
                ToolTip.text: "About Seabass"
                onClicked: root.aboutRequested()
            }
            ToolButton {
                text: "⚙️"
                font.pointSize: Theme.fontLarge
                ToolTip.visible: hovered
                ToolTip.text: "Preferences"
                onClicked: root.appSettingsRequested()
            }
            ToolButton {
                id: donateButton
                text: "❤️"
                font.pointSize: Theme.fontLarge
                ToolTip.visible: hovered
                ToolTip.text: "Support Seabass"
                onClicked: root.donationRequested()

                // A slight, infrequent heartbeat -- a soft "lub-dub"
                // every six seconds or so, not a continuous throb -- so
                // it reads as a subtle living detail rather than a
                // distracting animated icon. Two small swells with a
                // slight brightening on each, on sine curves (an organic
                // rise and settle, no snap), the second a touch weaker,
                // then a long rest. Drives scale and opacity directly
                // rather than through a Behavior, which would otherwise
                // re-trigger on every intermediate value this same
                // animation produces.
                SequentialAnimation {
                    running: true
                    loops: Animation.Infinite
                    ParallelAnimation {
                        NumberAnimation { target: donateButton; property: "scale"; to: 1.12; duration: 260; easing.type: Easing.OutSine }
                        NumberAnimation { target: donateButton; property: "opacity"; to: 1.0; duration: 260; easing.type: Easing.OutSine }
                    }
                    ParallelAnimation {
                        NumberAnimation { target: donateButton; property: "scale"; to: 1.0; duration: 340; easing.type: Easing.InOutSine }
                        NumberAnimation { target: donateButton; property: "opacity"; to: 0.85; duration: 340; easing.type: Easing.InOutSine }
                    }
                    PauseAnimation { duration: 90 }
                    ParallelAnimation {
                        NumberAnimation { target: donateButton; property: "scale"; to: 1.07; duration: 220; easing.type: Easing.OutSine }
                        NumberAnimation { target: donateButton; property: "opacity"; to: 1.0; duration: 220; easing.type: Easing.OutSine }
                    }
                    ParallelAnimation {
                        NumberAnimation { target: donateButton; property: "scale"; to: 1.0; duration: 520; easing.type: Easing.InOutSine }
                        NumberAnimation { target: donateButton; property: "opacity"; to: 0.85; duration: 520; easing.type: Easing.InOutSine }
                    }
                    PauseAnimation { duration: 4800 }
                }
            }
        }

        Label {
            visible: root.mediaController.errorMessage.length > 0
            text: root.mediaController.errorMessage
            color: Theme.danger
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // Tools that work on this computer's own backup stores, not on
        // whatever stick happens to be plugged in right now -- pulled out
        // of the per-stick Backups hub for exactly that reason (see
        // BackupsHubPage.qml's own comment for what stays there because
        // it genuinely does need a specific stick). Always here, even
        // with no stick inserted at all.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            Subtitle { text: "Backups"; color: Theme.textMuted }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 12

                ActionCard {
                    objectName: "generalRestoreCard"
                    cardTitle: "Restore a Stick Backup"
                    cardSubtitle: "Put a stick backup from this computer onto any drive"
                    cardIcon: "🗂"
                    experimental: true
                    experimentalFeaturesEnabled: root.appSettingsController.experimentalFeaturesEnabled
                    // No stick preselected: the page itself lists every
                    // mounted drive and every backup on disk to choose from.
                    onClicked: root.restoreStickBackupRequested("", "", "")
                }
                ActionCard {
                    objectName: "generalLocalCueCard"
                    cardTitle: "Local Cue Backup"
                    cardSubtitle: "Cue backups kept on this computer, across every stick"
                    cardIcon: "💿"
                    deprecated: true
                    deprecatedNote: "Needs rework"
                    onClicked: root.localCueRequested("", "", "")
                }
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.mediaController.sticks
            clip: true
            // Not draggable when every stick already fits.
            interactive: contentHeight > height
            spacing: 4

            // A plain Rectangle, not a Frame -- Qt Quick Controls' Material
            // style gives Frame its own additional implicit chrome that a
            // custom `background:` assignment doesn't fully replace (kept
            // rendering a stray light margin around the real content no
            // matter what the override's own color was set to). Every
            // other grouping frame in this app (SyncPage.qml/
            // DuplicatesPage.qml's meta-track groups) already uses this
            // same plain-Rectangle pattern for exactly that reason.
            delegate: Rectangle {
                id: delegateRoot
                width: ListView.view.width
                height: contentColumn.implicitHeight + 24
                color: Theme.surface
                border.color: Theme.border
                radius: 4

                required property string label
                // NOT required: a required property makes delegate
                // creation fail for any model that lacks the role, which
                // took out four StickListPage tests whose fake sticks
                // predate it. Defaulted instead, and the size is hidden
                // when it is zero anyway.
                property var capacityBytes: 0
                required property string mountPoint
                required property string devicePath
                required property bool mounted
                required property bool hasRekordbox
                required property bool hasEngine
                required property string rekordboxPath
                required property string enginePath
                required property bool isSdCard
                required property bool isFolder
                required property string libraryId
                readonly property bool hasKnownLibrary: hasRekordbox || hasEngine
                // Another instance is editing this stick's library: every
                // card that would change it goes read-only.
                readonly property bool lockedByOther: root.isLockedByOther(delegateRoot.libraryId)
                // What the backup advisor found for this stick (see
                // BackupAdvisorController); null until it has looked.
                readonly property var advice: root.backupAdvisor.advice[mountPoint] || null
                readonly property string adviceState: advice ? advice.state : ""
                // Another mounted stick whose library could be copied onto
                // this empty one / is a newer copy of this stick's library.
                readonly property var cloneSource: advice && advice.cloneSource && advice.cloneSource.kind === "stick"
                    ? advice.cloneSource : null
                readonly property var updateSource: advice && advice.updateSource && advice.updateSource.kind !== "none"
                    ? advice.updateSource : null
                // In flight (mount, unmount, or an automatic mount) via this
                // row's own devicePath -- distinct from mediaController.busy,
                // which is true for the whole app while ANY stick's task is
                // running (they're processed one at a time) and used to
                // disable every OTHER row's button too, making a click on a
                // stick nothing else was busy with look like it did nothing.
                readonly property bool thisRowBusy: root.mediaController.busy
                    && root.mediaController.busyDevicePath === delegateRoot.devicePath
                function assessBackup() {
                    if (mounted && mountPoint.length > 0) {
                        root.backupAdvisor.assess(label, mountPoint, rekordboxPath, enginePath);
                    }
                }
                Component.onCompleted: assessBackup()
                onMountedChanged: assessBackup()

                ColumnLayout {
                    id: contentColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    // A plain Item + explicit MouseArea, not an ItemDelegate
                    // -- ItemDelegate's own hover/press background isn't
                    // reliably gated by `enabled` in this KDE-Breeze/Material
                    // style mashup (confirmed: `enabled: !mounted` still left
                    // the row hover-highlighting and accepting clicks once
                    // mounted). ScanPage.qml's track rows already hit the
                    // exact same class of Material-Control-chrome issue and
                    // settled on this same Rectangle+MouseArea sidestep --
                    // see its comment for the fuller story.
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: rowContent.implicitHeight

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -6
                            radius: 4
                            visible: !delegateRoot.mounted
                            color: rowMouseArea.pressed ? Theme.rowPressed
                                : rowMouseArea.containsMouse ? Theme.rowHover
                                : "transparent"
                        }

                        MouseArea {
                            id: rowMouseArea
                            anchors.fill: parent
                            // Not gated on the whole app being busy anymore:
                            // mountStick() queues behind whatever else is
                            // running instead of being silently dropped, so
                            // there's no reason to make this look unusable
                            // meanwhile -- only this row's own task (if any)
                            // disables it.
                            enabled: !delegateRoot.mounted && !delegateRoot.thisRowBusy
                            hoverEnabled: !delegateRoot.mounted && !delegateRoot.thisRowBusy
                            ToolTip.visible: containsMouse
                            ToolTip.text: "Click to mount " + delegateRoot.label
                            onClicked: root.mediaController.mountStick(delegateRoot.devicePath)
                        }

                        RowLayout {
                            id: rowContent
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 12

                            UsbStickIcon {
                                Layout.preferredWidth: Theme.iconSizeNormal
                                Layout.preferredHeight: Theme.iconSizeNormal
                                Layout.alignment: Qt.AlignVCenter
                                isSdCard: delegateRoot.isSdCard
                                isFolder: delegateRoot.isFolder
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                RowLayout {
                                    Layout.fillWidth: true
                                    Subtitle {
                                        text: delegateRoot.label
                                        color: Theme.text
                                    }
                                    Label {
                                        text: delegateRoot.mounted ? "" : "(not mounted)"
                                        color: Theme.textMuted
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Label {
                                        text: delegateRoot.mounted ? delegateRoot.mountPoint : delegateRoot.devicePath
                                        color: Theme.textMuted
                                        font.pointSize: Theme.baseFontPointSize * 0.9
                                        elide: Text.ElideMiddle
                                        Layout.maximumWidth: implicitWidth
                                        Layout.fillWidth: true
                                    }
                                    // The stick's size, beside the path. Hidden
                                    // rather than shown as "0 B" when the locator
                                    // could not read a capacity, which happens for
                                    // a drive with no partition table at all.
                                    Label {
                                        visible: delegateRoot.capacityBytes > 0
                                        text: Theme.humanBytes(delegateRoot.capacityBytes)
                                        color: Theme.textMuted
                                        font.pointSize: Theme.baseFontPointSize * 0.9
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                                RowLayout {
                                    visible: delegateRoot.mounted
                                    Label { text: "DeviceLibrary: " + (delegateRoot.hasRekordbox ? "yes" : "no"); color: Theme.text }
                                    Label { text: "  Engine: " + (delegateRoot.hasEngine ? "yes" : "no"); color: Theme.text }
                                }
                            }
                        }
                    }

                    // Mount/unmount now run on a background thread (a real
                    // syscall/subprocess that can visibly take a moment --
                    // this exact freeze used to look like the app had hung
                    // or the stick had vanished, with zero feedback that
                    // anything was happening). While this row's own
                    // operation is in flight, show a spinner in the eject
                    // button's place instead of leaving it looking dead.

                    BusyIndicator {
                        visible: delegateRoot.thisRowBusy
                        running: visible
                        Layout.preferredWidth: Theme.iconSizeLarge
                        Layout.preferredHeight: Theme.iconSizeLarge
                        Layout.alignment: Qt.AlignVCenter
                    }

                    // A folder was never mounted, so there is nothing to
                    // eject -- the equivalent is dropping it from the
                    // list, which touches nothing on disk.
                    ToolButton {
                        visible: delegateRoot.isFolder
                        objectName: "closeFolderButton"
                        text: "✕"
                        font.pointSize: Theme.fontLarge
                        Layout.preferredWidth: Theme.iconSizeLarge
                        Layout.preferredHeight: Theme.iconSizeLarge
                        Layout.alignment: Qt.AlignVCenter
                        ToolTip.visible: hovered
                        ToolTip.text: "Remove " + delegateRoot.label + " from this list (nothing on disk is changed)"
                        onClicked: root.mediaController.closeFolder(delegateRoot.mountPoint)
                    }

                    ToolButton {
                        visible: !delegateRoot.thisRowBusy && !delegateRoot.isFolder
                        // Not `!root.mediaController.busy`: that disabled
                        // every OTHER row's button too while any one stick's
                        // task was running (including a background auto-
                        // mount), which read as "eject does nothing" on a
                        // stick that was not itself busy at all. A click
                        // here queues behind whatever else is in flight.
                        objectName: "ejectButton"
                        enabled: true
                        text: "⏏"
                        font.family: "Noto Sans Symbols2"
                        font.pointSize: Theme.fontHuge
                        // Rotating the eject glyph 180° to mean "mount"
                        // isn't a real convention -- it just reads as
                        // an upside-down (broken-looking) eject icon.
                        // Kept upright always; the tooltip (and now
                        // click-anywhere-on-the-row) carry the "mount"
                        // meaning instead.
                        Layout.preferredWidth: Theme.iconSizeLarge
                        Layout.preferredHeight: Theme.iconSizeLarge
                        Layout.alignment: Qt.AlignVCenter
                        ToolTip.visible: hovered
                        ToolTip.text: delegateRoot.mounted ? "Eject " + delegateRoot.label : "Mount " + delegateRoot.label
                        onClicked: {
                            if (delegateRoot.mounted) {
                                // Stop first -- unmounting out from under an open
                                // file handle on the playing track would be bad.
                                root.playbackController.stop();
                                root.mediaController.unmountStick(delegateRoot.devicePath);
                            } else {
                                root.mediaController.mountStick(delegateRoot.devicePath);
                            }
                        }
                    }
                }

                // No point showing a wall of disabled action buttons for a
                // stick that isn't mounted yet (nothing here is clickable
                // until it is -- click the row itself to mount) or that's
                // mounted but has no rekordbox/Engine library on it at all
                // (there's nothing for any of these actions to do).
                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    visible: !delegateRoot.hasKnownLibrary
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    text: delegateRoot.mounted
                        ? "No DeviceLibrary or Engine library detected on this stick. "
                          + (delegateRoot.cloneSource !== null
                             ? delegateRoot.cloneSource.detail + " "
                             : "")
                          + (delegateRoot.adviceState === "restore"
                             ? delegateRoot.advice.detail + " (" + delegateRoot.advice.backupLabel + ")"
                             : "Restore a backup onto it, or format it.")
                        : "Click to mount, then Seabass will show what's available here."
                }

                ColumnLayout {
                    // Always visible now, unlike the individual cards
                    // below -- Format is available for every stick
                    // regardless of whether it has a recognized library
                    // (it's the one thing you can do to a stick that
                    // *doesn't*), so this whole grid can no longer be
                    // gated behind hasKnownLibrary the way it used to be.
                    Layout.fillWidth: true

                    GridLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        Layout.bottomMargin: 8
                        columns: 3
                        columnSpacing: 12
                        rowSpacing: 12

                        ActionCard {
                            cardTitle: "Browse Library"
                            cardSubtitle: "View tracks, playlists and cues"
                            cardIcon: "▤"
                            visible: delegateRoot.hasKnownLibrary
                            enabled: delegateRoot.hasRekordbox || delegateRoot.hasEngine
                            onClicked: root.browseRequested(delegateRoot.label, delegateRoot.rekordboxPath, delegateRoot.enginePath)
                        }
                        ActionCard {
                            cardTitle: "Housekeeping"
                            readOnly: delegateRoot.lockedByOther
                            onReadOnlyClicked: root.explainLock(delegateRoot.libraryId)
                            cardSubtitle: "Duplicate stats, copy cues between copies, and clean up"
                            cardIcon: "▣"
                            visible: delegateRoot.hasKnownLibrary
                            enabled: delegateRoot.hasRekordbox || delegateRoot.hasEngine
                            onClicked: root.duplicateTracksHubRequested(delegateRoot.label, delegateRoot.rekordboxPath, delegateRoot.enginePath)
                        }
                        ActionCard {
                            cardTitle: "Library Health"
                            readOnly: delegateRoot.lockedByOther
                            onReadOnlyClicked: root.explainLock(delegateRoot.libraryId)
                            cardSubtitle: "Find rows whose file is missing and repair or clean them up"
                            cardIcon: "🩹"
                            // Graduated from experimental (see
                            // docs/experimental-features.md) after real
                            // use with no incidents.
                            visible: delegateRoot.hasKnownLibrary
                            enabled: delegateRoot.hasRekordbox || delegateRoot.hasEngine
                            onClicked: root.libraryHealthRequested(delegateRoot.label, delegateRoot.rekordboxPath, delegateRoot.enginePath)
                        }
                        ActionCard {
                            cardTitle: "Library Statistics"
                            cardSubtitle: "Filesystem, library stats, disk usage, and read-speed benchmark"
                            cardIcon: "📊"
                            // Graduated from experimental (see
                            // docs/experimental-features.md) after real
                            // use with no incidents.
                            visible: delegateRoot.hasKnownLibrary
                            enabled: delegateRoot.hasRekordbox || delegateRoot.hasEngine
                            onClicked: root.stickStatisticsRequested(delegateRoot.label, delegateRoot.rekordboxPath, delegateRoot.enginePath)
                        }
                        ActionCard {
                            cardTitle: "Create Engine Library"
                            readOnly: delegateRoot.lockedByOther
                            onReadOnlyClicked: root.explainLock(delegateRoot.libraryId)
                            cardSubtitle: "Build a new Engine Library from this stick's DeviceLibrary export"
                            cardIcon: "⚙"
                            cardIconFont: "Noto Sans Symbols"
                            // Experimental (see docs/experimental-features.md):
                            // the first feature here that fabricates a whole
                            // new database from scratch. Only makes sense
                            // when there's rekordbox data to build from and
                            // no Engine Library already present to overwrite.
                            experimental: true
                            experimentalFeaturesEnabled: root.appSettingsController.experimentalFeaturesEnabled
                            visible: delegateRoot.hasKnownLibrary
                            enabled: delegateRoot.hasRekordbox && !delegateRoot.hasEngine
                            onClicked: root.engineLibraryCreatorRequested(delegateRoot.label, delegateRoot.rekordboxPath)
                        }
                        ActionCard {
                            cardTitle: "Sync Cue Points"
                            readOnly: delegateRoot.lockedByOther
                            onReadOnlyClicked: root.explainLock(delegateRoot.libraryId)
                            cardSubtitle: "Copy cues between DeviceLibrary and Engine"
                            cardIcon: "⇄"
                            cardIconFont: "Noto Sans Math"
                            visible: delegateRoot.hasKnownLibrary
                            enabled: delegateRoot.hasRekordbox && delegateRoot.hasEngine
                            onClicked: root.syncRequested(delegateRoot.label, delegateRoot.rekordboxPath, delegateRoot.enginePath)
                        }
                        ActionCard {
                            cardTitle: "Backups"
                            readOnly: delegateRoot.lockedByOther
                            onReadOnlyClicked: root.explainLock(delegateRoot.libraryId)
                            // The advisor's verdict on the full stick backup
                            // leads when it has one; the generic line otherwise.
                            cardSubtitle: {
                                if (delegateRoot.updateSource !== null) {
                                    return "Newer copy on " + delegateRoot.updateSource.label + ": update this stick from here";
                                }
                                switch (delegateRoot.adviceState) {
                                case "outdated": return "Update the full stick backup: " + delegateRoot.advice.detail;
                                case "behind-backup": return delegateRoot.advice.detail;
                                case "current": return "Full stick backup is up to date";
                                case "back-up-new":
                                case "no-backups": return "No full stick backup of this library yet";
                                case "different-library": return delegateRoot.advice.detail + " Back it up as new.";
                                default: return "Local cue backup/restore and automatic write backups";
                                }
                            }
                            cardIcon: "🗄"
                            visible: delegateRoot.hasKnownLibrary
                            enabled: delegateRoot.hasRekordbox || delegateRoot.hasEngine
                            onClicked: root.backupsHubRequested(delegateRoot.label, delegateRoot.rekordboxPath, delegateRoot.enginePath,
                                delegateRoot.mountPoint, delegateRoot.devicePath)
                        }
                        ActionCard {
                            cardTitle: "Device Profile"
                            readOnly: delegateRoot.lockedByOther
                            onReadOnlyClicked: root.explainLock(delegateRoot.libraryId)
                            cardSubtitle: "View this stick's saved Rekordbox player settings"
                            cardIcon: "⚙"
                            cardIconFont: "Noto Sans Symbols"
                            visible: delegateRoot.hasKnownLibrary
                            enabled: delegateRoot.hasRekordbox
                            onClicked: root.settingsRequested(delegateRoot.label, delegateRoot.rekordboxPath)
                        }
                        ActionCard {
                            cardTitle: "Format USB Stick"
                            // There is no drive behind a folder row to
                            // erase, and devicePath is empty for one.
                            visible: !delegateRoot.isFolder
                            cardSubtitle: "Erase and prepare this drive for CDJs, XDJs, and Denon Engine players"
                            cardIcon: "💽"
                            // Experimental (see docs/experimental-features.md):
                            // the first feature here that can permanently
                            // erase a drive, not just modify/consolidate
                            // library data on one -- wants real use before
                            // graduating.
                            experimental: true
                            experimentalFeaturesEnabled: root.appSettingsController.experimentalFeaturesEnabled
                            // Available for every stick regardless of
                            // hasKnownLibrary -- unlike every other card
                            // here, this is the one action meant for a
                            // stick with nothing recognizable on it yet.
                            enabled: !root.mediaController.busy
                            onClicked: root.formatUsbRequested()
                        }
                        // "Restore a Backup" and "Create Backup USB Stick" used
                        // to be two separate cards for the same job (putting
                        // a library onto an empty stick, whichever copy is
                        // newer/available) -- merged into one, since an
                        // empty stick never needs both at once. Prefers a
                        // peer stick's own live copy (cloneSource) over a
                        // disk backup when both exist, same priority order
                        // adviseStickBackup already uses for the update case.
                        ActionCard {
                            cardTitle: "Create Backup USB Stick"
                            readOnly: delegateRoot.lockedByOther
                            onReadOnlyClicked: root.explainLock(delegateRoot.libraryId)
                            // Visible unconditionally (see below), so its
                            // wording must not presuppose a backup exists:
                            // "no-backups" is exactly the state where none
                            // do, and it is a real, common state for this
                            // card -- a freshly formatted stick with an
                            // empty default backup directory reaches it
                            // every time. The old fallback text, "Restore a
                            // library onto this USB stick", read as though
                            // a backup were known to exist and just needed
                            // picking, which is what was reported as
                            // "Seabass offers to restore a backup ... but
                            // we don't have one".
                            cardSubtitle: delegateRoot.cloneSource !== null ? delegateRoot.cloneSource.detail
                                : (delegateRoot.adviceState === "restore"
                                    ? "Restore " + delegateRoot.advice.backupLabel + "'s library onto this stick"
                                    : "No known stick backups yet -- browse for a backup file to restore")
                            cardIcon: "⧉"
                            cardIconFont: "Noto Sans Math"
                            // Experimental with the stick backup it is built
                            // on: either a copy of another mounted stick's
                            // own current library, or an existing backup
                            // from this computer, written onto this stick.
                            experimental: true
                            experimentalFeaturesEnabled: root.appSettingsController.experimentalFeaturesEnabled
                            // Only for a stick with nothing recognizable on
                            // it: the disaster case is a blank replacement
                            // drive. A stick that already has a library
                            // updates from its own Backups page instead.
                            // Restoring writes a whole stick through
                            // devicePath, which a folder row does not have.
                            visible: !delegateRoot.hasKnownLibrary && !delegateRoot.isFolder
                            // The disk-backup path isn't gated on `mounted`:
                            // a stick fresh out of Format USB Stick is not
                            // remounted, and the restore page mounts it
                            // itself when handed the device path. The clone
                            // path does need the source stick mounted, which
                            // cloneSource being non-null already implies
                            // (peers are only ever mounted sticks).
                            enabled: !delegateRoot.thisRowBusy && (delegateRoot.cloneSource === null
                                || (delegateRoot.mounted && delegateRoot.cloneSource.enoughSpace !== false))
                            onClicked: {
                                if (delegateRoot.cloneSource !== null) {
                                    root.cloneStickRequested(delegateRoot.cloneSource.label,
                                        delegateRoot.cloneSource.rekordboxPath, delegateRoot.cloneSource.enginePath,
                                        delegateRoot.mountPoint, delegateRoot.label, false);
                                } else {
                                    root.restoreStickBackupRequested(delegateRoot.mountPoint, delegateRoot.devicePath,
                                        delegateRoot.adviceState === "restore" ? delegateRoot.advice.backupPath : "");
                                }
                            }
                        }
                    }
                }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: parent.count === 0
                text: "No USB sticks detected. Insert one to get started."
                font.pointSize: Theme.fontLarge
                color: Theme.textMuted
            }
        }
    }
}
