import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// Fans out the "Duplicate Tracks" top-level card into its sub-features,
// non-destructive stats/metadata-sync (DuplicatesPage), the destructive
// survivor-select+delete flow (CleanupPage), and reviewing/deleting the
// actual audio files those cleanups orphaned but never touched on disk
// (PendingDeletionsPage). Three separate pages, not merged into one,
// specifically so each destructive step always requires its own
// deliberate navigation rather than sitting next to read-only stats
// where a stray click could reach it.
Page {
    id: root
    required property string stickLabel
    required property string rekordboxPath
    required property string enginePath
    signal duplicatesStatsRequested(string stickLabel, string rekordboxPath, string enginePath)
    signal cleanupRequested(string stickLabel, string rekordboxPath, string enginePath)
    signal pendingDeletionsRequested(string stickLabel, string rekordboxPath, string enginePath)
    signal junkCueCleanupRequested(string stickLabel, string rekordboxPath, string enginePath)

    readonly property bool hasRekordbox: rekordboxPath.length > 0
    readonly property bool hasEngine: enginePath.length > 0
    readonly property bool hasOneLibrary: root.hasRekordbox && pendingCheckController.hasOneLibrary(root.rekordboxPath)

    // Cheap (a small text file + a stat() per entry, no library scan,
    // see loadPendingDeletionsOnly()'s own doc comment), so checked
    // synchronously right when this hub opens, purely to decide whether
    // "Delete Orphaned Files" has anything to do. Reused sequentially for
    // both formats rather than one instance each, right after
    // loadPendingDeletionsOnly() populates the model, every entry starts
    // included, so pendingDeletionsIncludedCount is exactly the total
    // count at that moment, before either format's next call overwrites it.
    CleanupController {
        id: pendingCheckController
    }
    property int rekordboxPendingCount: 0
    property int enginePendingCount: 0
    readonly property bool hasPendingDeletions: rekordboxPendingCount > 0 || enginePendingCount > 0

    function refreshPendingCounts() {
        if (root.hasRekordbox) {
            pendingCheckController.loadPendingDeletionsOnly("rekordbox", root.rekordboxPath);
            root.rekordboxPendingCount = pendingCheckController.pendingDeletionsIncludedCount;
        }
        if (root.hasEngine) {
            pendingCheckController.loadPendingDeletionsOnly("engine", root.enginePath);
            root.enginePendingCount = pendingCheckController.pendingDeletionsIncludedCount;
        }
    }

    // StackView.onActivated, not Component.onCompleted, fires both when
    // this hub is first pushed AND every time navigation returns to it
    // (from Clean Up Duplicates, which can create new orphaned files, or
    // from Delete Orphaned Files itself, which clears them), so the card
    // below never goes stale after either happens.
    StackView.onActivated: root.refreshPendingCounts()

    header: ToolBar {
        // Opaque background override, see AppSettingsPage.qml's header
        // for why (KDE's Breeze style bleeds the window behind Seabass
        // through an unstyled ToolBar).
        background: Rectangle { color: Theme.surface }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 12
            BackBreadcrumb {
                middleLabel: root.stickLabel
                title: "Clean-up and Housekeeping"
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
            cardTitle: "Duplicate Stats & Sync"
            cardSubtitle: "Space wasted, out-of-sync copies, sync metadata and cue points across them"
            cardIcon: "▣"
            enabled: root.hasRekordbox || root.hasEngine
            onClicked: root.duplicatesStatsRequested(root.stickLabel, root.rekordboxPath, root.enginePath)
        }
        ActionCard {
            cardTitle: "Clean Up Duplicates"
            cardSubtitle: "Remove redundant copies, keep the best one"
                + (root.hasOneLibrary ? " (also updates OneLibrary)" : "")
            cardIcon: "🧹"
            enabled: root.hasRekordbox || root.hasEngine
            onClicked: root.cleanupRequested(root.stickLabel, root.rekordboxPath, root.enginePath)
        }
        ActionCard {
            cardTitle: "Delete Orphaned Files"
            cardSubtitle: root.hasPendingDeletions
                ? "Free disk space: delete files earlier cleanups' database edits orphaned"
                : "Nothing orphaned right now. Every earlier cleanup's files are accounted for"
            cardIcon: "🗑"
            enabled: (root.hasRekordbox || root.hasEngine) && root.hasPendingDeletions
            onClicked: root.pendingDeletionsRequested(root.stickLabel, root.rekordboxPath, root.enginePath)
        }
        ActionCard {
            cardTitle: "Clean Up Stray Cues"
            cardSubtitle: "Remove memory cues sitting at 0:00, almost always accidental"
            cardIcon: "🧽"
            enabled: root.hasRekordbox || root.hasEngine
            onClicked: root.junkCueCleanupRequested(root.stickLabel, root.rekordboxPath, root.enginePath)
        }
        Item { Layout.fillHeight: true }
    }
}
