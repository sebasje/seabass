import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// Fans out the "Backups" top-level card into its two sub-features --
// LocalCuePage (cue backup/restore to/from this computer) and BackupsPage
// (listing/cleaning up the automatic per-write backups already kept on the
// stick itself). Two distinct backup stores, kept as two sub-pages rather
// than merged, same as before this reorg -- only the entry point changed.
Page {
    id: root
    required property string stickLabel
    required property string rekordboxPath
    required property string enginePath
    signal localCueRequested(string stickLabel, string rekordboxPath, string enginePath)
    signal manageBackupsRequested(string stickLabel, string rekordboxPath, string enginePath)

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
        Item { Layout.fillHeight: true }
    }
}
