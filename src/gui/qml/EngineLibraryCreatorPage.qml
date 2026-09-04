import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// Creates a brand-new Engine Library from this stick's existing rekordbox
// export -- for a stick (or SD card) that only has a rekordbox Device
// Library and no Engine Library at all, so Denon Engine OS hardware
// (e.g. Prime GO+) has something native to browse. Experimental: this is
// the first feature in this app that fabricates an entire new database
// from scratch rather than modifying an existing, already-recognized
// one -- see docs/experimental-features.md.
Page {
    id: root
    required property string stickLabel
    required property string rekordboxPath

    EngineLibraryCreatorController {
        id: controller
    }

    // 0=V1, 1=V2, 2=V3 -- see infrastructure::engine::EngineSchemaGeneration.
    // Real hardware/firmware compatibility per generation is genuinely
    // unverified by this project; exposed as a real choice so a mismatch
    // can be fixed by trying another generation, not a new build.
    property int schemaGeneration: 1

    header: ToolBar {
        background: Rectangle { color: Theme.surface }
        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 12
            BackBreadcrumb {
                middleLabel: root.stickLabel
                title: "Create Engine Library"
                backEnabled: !controller.busy
                backDisabledTooltip: "Wait for creation to finish before leaving this page"
                onHomeRequested: root.StackView.view.pop(null)
                onBackRequested: root.StackView.view.pop()
            }
            Item { Layout.fillWidth: true }
            BusyIndicator { running: controller.busy; visible: controller.busy; implicitWidth: 20; implicitHeight: 20 }
        }
    }

    Dialog {
        id: confirmDialog
        anchors.centerIn: parent
        modal: true
        width: 480
        title: "Create Engine Library?"
        footer: DialogButtonBox {
            Button { text: "Create"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: controller.create(root.rekordboxPath, root.schemaGeneration)

        ColumnLayout {
            width: parent.width
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "This creates a brand new \"Engine Library\" folder on this stick from the current "
                    + "DeviceLibrary export. It does not touch DeviceLibrary's own data at all."
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.conflictText
                text: "Experimental: this is the first feature in Seabass that builds a whole new database "
                    + "from scratch rather than editing one Engine itself already created. It has been "
                    + "verified by creating a library and reading it back with this app's own reader, but "
                    + "never tested on real Denon hardware. Verify carefully on your unit before trusting it "
                    + "for a gig."
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

            GroupBox {
                label: Subtitle { text: "What's included" }
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "Title, artist, BPM, key, duration, bitrate, rating, comment, hot cues, memory "
                            + "cue, and a simple approximate beatgrid computed from BPM and duration (assumes "
                            + "the track starts exactly on a beat, not always true, but a reasonable "
                            + "stand-in absent a real per-beat grid)."
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        text: "Not included yet: cover art, a real per-beat grid, waveform data, and "
                            + "playlists. Tracks with no resolved local file (including streaming tracks) "
                            + "are skipped."
                    }
                }
            }

            GroupBox {
                label: Subtitle { text: "Engine schema generation" }
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4
                    ButtonGroup { id: schemaGroup }
                    RadioButton {
                        text: "Engine OS 1.x (older standalone hardware)"
                        ButtonGroup.group: schemaGroup
                        checked: root.schemaGeneration === 0
                        onToggled: if (checked) root.schemaGeneration = 0
                    }
                    RadioButton {
                        text: "Engine OS 2.x"
                        ButtonGroup.group: schemaGroup
                        checked: root.schemaGeneration === 1
                        onToggled: if (checked) root.schemaGeneration = 1
                    }
                    RadioButton {
                        text: "Engine OS 3.x / newest Engine DJ desktop"
                        ButtonGroup.group: schemaGroup
                        checked: root.schemaGeneration === 2
                        onToggled: if (checked) root.schemaGeneration = 2
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                        text: "Not sure which your unit needs? 2.x is a reasonable first try; if the "
                            + "Denon unit doesn't recognize the result, delete the \"Engine Library\" folder "
                            + "and try a different generation."
                    }
                }
            }

            RowLayout {
                spacing: 12
                Button {
                    text: "Create Engine Library"
                    enabled: !controller.busy
                    onClicked: confirmDialog.open()
                }
            }

            Label {
                visible: controller.errorMessage.length > 0
                text: controller.errorMessage
                color: Theme.danger
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                visible: controller.statusMessage.length > 0
                text: controller.statusMessage
                color: Theme.good
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    BusyOverlay {
        anchors.fill: parent
        busy: controller.busy
        current: controller.scanCurrent
        total: controller.scanTotal
        label: controller.currentPhase.length > 0 ? controller.currentPhase + "..." : "Working..."
    }
}
