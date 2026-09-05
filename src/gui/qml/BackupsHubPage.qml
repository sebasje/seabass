import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// Fans out the "Backups" top-level card into its sub-features --
// LocalCuePage (cue backup/restore to/from this computer), BackupsPage
// (listing/cleaning up the automatic per-write backups already kept on the
// stick itself) and, experimentally, StickBackupPage (the whole stick into
// one archive on this computer). Distinct backup stores, kept as separate
// sub-pages rather than merged -- only the entry point is shared.
Page {
    id: root
    required property string stickLabel
    required property string rekordboxPath
    required property string enginePath
    required property var appSettingsController
    signal localCueRequested(string stickLabel, string rekordboxPath, string enginePath)
    signal manageBackupsRequested(string stickLabel, string rekordboxPath, string enginePath)
    signal fullStickBackupRequested(string stickLabel, string rekordboxPath, string enginePath)

    readonly property bool hasRekordbox: rekordboxPath.length > 0
    readonly property bool hasEngine: enginePath.length > 0

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
            cardTitle: "Local Cue Backup"
            cardSubtitle: "Back up or restore cues on this computer"
            cardIcon: "💿"
            enabled: root.hasRekordbox || root.hasEngine
            onClicked: root.localCueRequested(root.stickLabel, root.rekordboxPath, root.enginePath)
        }
        ActionCard {
            cardTitle: "Manage Backups"
            cardSubtitle: "List and clean up automatic write backups"
            cardIcon: "🗄"
            enabled: root.hasRekordbox || root.hasEngine
            onClicked: root.manageBackupsRequested(root.stickLabel, root.rekordboxPath, root.enginePath)
        }
        ActionCard {
            cardTitle: "Full Stick Backup"
            cardSubtitle: "Back up the whole stick into one file on this computer, or put it back"
            cardIcon: "🗃"
            // Experimental (see docs/experimental-features.md and
            // docs/stick-backup-plan.md): a new archive format and a
            // restore path that overwrites files on a stick.
            experimental: true
            experimentalFeaturesEnabled: root.appSettingsController.experimentalFeaturesEnabled
            enabled: root.hasRekordbox || root.hasEngine
            onClicked: root.fullStickBackupRequested(root.stickLabel, root.rekordboxPath, root.enginePath)
        }
        Item { Layout.fillHeight: true }
    }
}
