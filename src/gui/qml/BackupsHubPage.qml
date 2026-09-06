import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// Fans out the "Backups" top-level card into every backup and restore
// option this stick has: the full stick backup into one archive on this
// computer, bringing this stick up to date from a newer copy of its
// library (another mounted stick, or the disk backup), restoring a stick
// backup onto it, and the two older stores -- LocalCuePage (cue
// backup/restore to/from this computer) and BackupsPage (the automatic
// per-write backups kept on the stick itself), both deprecated pending a
// rework. Distinct stores, kept as separate sub-pages rather than merged
// -- only the entry point is shared.
Page {
    id: root
    required property string stickLabel
    required property string rekordboxPath
    required property string enginePath
    required property var appSettingsController
    // The stick's mount point and device, and the stick list's backup
    // advisor: what the update card is decided from. Optional so the
    // page still works when pushed without them.
    property string mountPoint: ""
    property string devicePath: ""
    property var backupAdvisor: null
    signal localCueRequested(string stickLabel, string rekordboxPath, string enginePath)
    signal manageBackupsRequested(string stickLabel, string rekordboxPath, string enginePath)
    signal fullStickBackupRequested(string stickLabel, string rekordboxPath, string enginePath)
    signal restoreStickBackupRequested(string mountPoint, string devicePath, string archivePath)
    signal cloneStickRequested(string sourceLabel, string sourceRekordboxPath, string sourceEnginePath,
                               string targetMountPoint, string targetLabel, bool targetHasLibrary)

    readonly property bool hasRekordbox: rekordboxPath.length > 0
    readonly property bool hasEngine: enginePath.length > 0
    readonly property string stickRoot: root.mountPoint.length > 0 ? root.mountPoint
        : (root.enginePath.length > 0 ? root.enginePath : root.rekordboxPath).replace(/[\/\\][^\/\\]+[\/\\]?$/, "")
    readonly property var advice: root.backupAdvisor !== null && root.mountPoint.length > 0
        ? (root.backupAdvisor.advice[root.mountPoint] || null) : null
    // A newer copy of this stick's library somewhere else -- see
    // BackupAdvisorController's advice map.
    readonly property var updateSource: root.advice && root.advice.updateSource && root.advice.updateSource.kind !== "none"
        ? root.advice.updateSource : null

    header: ToolBar {
        // Opaque background override -- see AppSettingsPage.qml's header
        // for why (KDE's Breeze style bleeds the window behind Seabass
        // through an unstyled ToolBar).
        background: Rectangle { color: Theme.surface }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 12
            BackBreadcrumb {
                middleLabel: root.stickLabel
                title: "Backups"
                onHomeRequested: root.StackView.view.pop(null)
                onBackRequested: root.StackView.view.pop()
            }
            Item { Layout.fillWidth: true }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        ActionCard {
            cardTitle: "Full Stick Backup"
            cardSubtitle: root.advice && root.advice.state === "outdated"
                ? "Update the full stick backup: " + root.advice.detail
                : (root.advice && root.advice.state === "current"
                    ? "Full stick backup is up to date"
                    : "Back up the whole stick into one file on this computer")
            cardIcon: "🗃"
            // Experimental (see docs/experimental-features.md and
            // docs/stick-backup-plan.md): a new archive format and a
            // restore path that overwrites files on a stick.
            experimental: true
            experimentalFeaturesEnabled: root.appSettingsController.experimentalFeaturesEnabled
            enabled: root.hasRekordbox || root.hasEngine
            onClicked: root.fullStickBackupRequested(root.stickLabel, root.rekordboxPath, root.enginePath)
        }
        ActionCard {
            objectName: "updateStickCard"
            cardTitle: "Update Stick"
            cardSubtitle: root.updateSource !== null
                ? (root.advice.diverged === true ? "⚠ " : "") + root.updateSource.detail
                : ""
            cardIcon: "⟳"
            cardIconFont: "Noto Sans Math"
            experimental: true
            experimentalFeaturesEnabled: root.appSettingsController.experimentalFeaturesEnabled
            // A newer copy of this stick's library exists: on another
            // mounted stick (copied via its backup) or as the disk backup
            // itself (restored).
            visible: root.updateSource !== null
                && (!experimental || experimentalFeaturesEnabled)
            enabled: root.updateSource !== null && root.updateSource.enoughSpace !== false
            onClicked: {
                if (root.updateSource.kind === "stick") {
                    root.cloneStickRequested(root.updateSource.label, root.updateSource.rekordboxPath,
                        root.updateSource.enginePath, root.mountPoint, root.stickLabel, true);
                } else {
                    root.restoreStickBackupRequested(root.mountPoint, root.devicePath, root.updateSource.backupPath);
                }
            }
        }
        ActionCard {
            objectName: "restoreCard"
            cardTitle: "Restore a Stick Backup"
            cardSubtitle: "Put a stick backup from this computer back onto " + root.stickLabel
            cardIcon: "🗂"
            experimental: true
            experimentalFeaturesEnabled: root.appSettingsController.experimentalFeaturesEnabled
            enabled: root.hasRekordbox || root.hasEngine
            // No archive named: the restore page picks this stick's own
            // backup by label, and lets another be chosen.
            onClicked: root.restoreStickBackupRequested(root.stickRoot, root.devicePath, "")
        }
        ActionCard {
            objectName: "localCueCard"
            cardTitle: "Local Cue Backup"
            cardSubtitle: "Back up or restore cues on this computer"
            cardIcon: "💿"
            deprecated: true
            deprecatedNote: "Needs rework"
            enabled: root.hasRekordbox || root.hasEngine
            onClicked: root.localCueRequested(root.stickLabel, root.rekordboxPath, root.enginePath)
        }
        ActionCard {
            objectName: "manageBackupsCard"
            cardTitle: "Manage Backups"
            cardSubtitle: "List and clean up automatic write backups"
            cardIcon: "🗄"
            deprecated: true
            deprecatedNote: "Needs rework"
            enabled: root.hasRekordbox || root.hasEngine
            onClicked: root.manageBackupsRequested(root.stickLabel, root.rekordboxPath, root.enginePath)
        }
        Item { Layout.fillHeight: true }
    }
}
