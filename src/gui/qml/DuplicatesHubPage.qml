import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DjConvertGui

// Fans out the "Duplicate Tracks" top-level card into its two sub-features
// -- non-destructive stats/metadata-sync (DuplicatesPage) and the
// destructive survivor-select+delete flow (CleanupPage). Split into two
// pages, not merged into one, specifically so the destructive flow always
// requires its own deliberate navigation step rather than sitting next to
// read-only stats where a stray click could reach it.
Page {
    id: root
    required property string stickLabel
    required property string rekordboxPath
    required property string enginePath
    signal duplicatesStatsRequested(string stickLabel, string rekordboxPath, string enginePath)
    signal cleanupRequested(string stickLabel, string rekordboxPath, string enginePath)

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
            ToolButton {
                text: "‹"
                font.pointSize: Theme.fontHuge
                ToolTip.visible: hovered
                ToolTip.text: "Back"
                onClicked: root.StackView.view.pop()
            }
            Label {
                text: root.stickLabel + " -- Duplicate Tracks"
                font.bold: true
                font.pointSize: Theme.fontLarge
            }
            Item { Layout.fillWidth: true }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        ActionCard {
            cardTitle: "Duplicate Stats & Sync"
            cardSubtitle: "Space wasted, out-of-sync copies, sync metadata and cue points across them"
            cardIcon: "▣"
            enabled: root.hasRekordbox || root.hasEngine
            onClicked: root.duplicatesStatsRequested(root.stickLabel, root.rekordboxPath, root.enginePath)
        }
        ActionCard {
            cardTitle: "Clean Up Duplicates"
            cardSubtitle: "Remove redundant copies, keep the best one"
            cardIcon: "🧹"
            enabled: root.hasRekordbox || root.hasEngine
            onClicked: root.cleanupRequested(root.stickLabel, root.rekordboxPath, root.enginePath)
        }
        Item { Layout.fillHeight: true }
    }
}
