import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SeabassGui

// Writes a de-identified, structurally-real copy of one or both real
// libraries to a chosen folder: for regenerating this project's own
// committed test fixture, or for submitting a library to help test
// hardware/library shapes the maintainer doesn't have. Never sends
// anything anywhere itself -- only ever writes to the folder chosen
// below. Reachable from Settings (App Settings -> Experimental
// features), not an ActionCard: this is a maintainer/power-user tool,
// not a per-stick everyday action.
Page {
    id: root
    required property var mediaController

    AnonymizeLibraryController {
        id: controller
    }

    // Populated from mediaController.sticks -- only sticks actually
    // mounted with a rekordbox or Engine catalog Seabass could read are
    // candidates; nothing here is ever a manually-typed path.
    property var candidateSticks: []
    property int selectedStickIndex: -1
    readonly property var selectedStick: root.selectedStickIndex >= 0 && root.selectedStickIndex < root.candidateSticks.length
        ? root.candidateSticks[root.selectedStickIndex] : null

    property string outputDir: ""
    property int maxTracks: 0  // 0 = unlimited, see controller.run()'s own doc comment
    property var selectedHardware: ({})  // label -> true, for checked entries
    property string otherHardware: ""

    readonly property string hardwareText: {
        var parts = [];
        for (var key in root.selectedHardware) {
            if (root.selectedHardware[key]) {
                parts.push(key);
            }
        }
        if (root.otherHardware.trim().length > 0) {
            parts.push(root.otherHardware.trim());
        }
        return parts.join(", ");
    }

    // Grouped by vendor for display only -- a checkbox's selection key
    // (see selectedHardware/hardwareText below) is still just the bare
    // model name, so grouping never changes what ends up in MANIFEST.txt.
    readonly property var hardwareGroups: [
        { vendor: "Pioneer DJ / AlphaTheta", items: [
            "CDJ-3000", "CDJ-2000NXS2", "XDJ-RX3", "XDJ-XZ", "DJM-900NXS2", "DJM-750MK2", "DJM-A9",
        ] },
        { vendor: "Denon DJ / inMusic", items: [
            "Prime 4", "Prime 4+", "Prime GO", "SC5000", "SC6000", "SC-Live 4",
        ] },
    ]

    function refreshCandidates() {
        var list = [];
        var count = mediaController.sticks.rowCount();
        for (var i = 0; i < count; i++) {
            var row = mediaController.sticks.get(i);
            if (row.mounted && (row.hasRekordbox || row.hasEngine)) {
                list.push(row);
            }
        }
        root.candidateSticks = list;
        root.selectedStickIndex = list.length > 0 ? 0 : -1;
    }

    Component.onCompleted: {
        mediaController.detect();
        root.refreshCandidates();
    }

    Connections {
        target: mediaController.sticks
        function onModelReset() { root.refreshCandidates() }
    }

    FolderDialog {
        id: outputFolderDialog
        title: "Choose where to write the anonymized export"
        onAccepted: root.outputDir = selectedFolder.toString().replace(/^file:\/\//, "")
    }

    header: ToolBar {
        background: Rectangle { color: Theme.surface }
        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 12
            ToolButton {
                text: "‹"
                font.pointSize: Theme.fontHuge
                enabled: !controller.busy
                ToolTip.visible: hovered
                ToolTip.text: controller.busy ? "Wait for it to finish before leaving this page" : "Back"
                onClicked: root.StackView.view.pop()
            }
            PageTitle {
                text: "Export Anonymized Library"
            }
            Item { Layout.fillWidth: true }
            BusyIndicator { running: controller.busy; visible: controller.busy; implicitWidth: 20; implicitHeight: 20 }
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

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Nothing is sent anywhere automatically; this only writes files to the folder "
                        + "you choose below. Review them yourself before sending anything."
                }
                InfoButton {
                    explanationTitle: "What gets sent, and to whom?"
                    explanationText: "Nothing, automatically. This tool only writes files to the output folder "
                        + "you pick. If you'd like to help test Seabass, you review those files yourself, then "
                        + "attach the folder (zipped) to an email you send to sebas@kde.org. This data may be "
                        + "published as part of the project's test suite. If there's anything in the hardware "
                        + "or notes fields below you would not want published, leave it out here and mention "
                        + "it directly in your email instead."
                }
            }

            GroupBox {
                label: Subtitle { text: "Source library" }
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Label {
                        visible: root.candidateSticks.length === 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        text: "No rekordbox or Engine library detected on a connected, mounted stick. "
                            + "Insert one and reopen this page."
                    }

                    RowLayout {
                        visible: root.candidateSticks.length > 1
                        spacing: 8
                        Label { text: "Stick:" }
                        ComboBox {
                            Layout.fillWidth: true
                            model: root.candidateSticks.map((s) => s.label)
                            currentIndex: root.selectedStickIndex
                            onActivated: (index) => root.selectedStickIndex = index
                            enabled: !controller.busy
                        }
                    }

                    Label {
                        visible: root.selectedStick !== null
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: root.selectedStick
                            ? (root.selectedStick.label
                                + (root.selectedStick.hasRekordbox ? " · rekordbox" : "")
                                + (root.selectedStick.hasEngine ? " · Engine" : ""))
                            : ""
                    }
                }
            }

            GroupBox {
                label: Subtitle { text: "Output folder" }
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    Label {
                        text: "Created fresh; must not already exist and be non-empty."
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                    }
                    RowLayout {
                        spacing: 8
                        Label {
                            Layout.fillWidth: true
                            text: root.outputDir.length > 0 ? root.outputDir : "No folder chosen yet"
                            color: root.outputDir.length > 0 ? Theme.text : Theme.textMuted
                            elide: Text.ElideMiddle
                        }
                        Button {
                            text: "Choose…"
                            enabled: !controller.busy
                            onClicked: outputFolderDialog.open()
                        }
                    }

                    RowLayout {
                        spacing: 8
                        Label { text: "Max tracks:" }
                        SpinBox {
                            id: maxTracksSpin
                            from: 0
                            to: 999900
                            stepSize: 100
                            value: root.maxTracks
                            enabled: !controller.busy
                            onValueModified: root.maxTracks = value
                            textFromValue: (value) => value === 0 ? "Unlimited" : value.toString()
                            valueFromText: (text) => text === "Unlimited" ? 0 : parseInt(text)
                        }
                        Label {
                            text: "0 means every real track is included"
                            color: Theme.textMuted
                            font.pointSize: Theme.fontSmall
                        }
                        InfoButton {
                            explanationTitle: "What's kept, replaced, and removed"
                            explanationText: "Kept as-is: format, file size, bitrate, duration, BPM, key, "
                                + "hot/memory cue positions and colors, cue comments, rating, play count, "
                                + "last-played date, whether a track is a streaming-service track, playlist "
                                + "membership and position, and the low-resolution waveform preview this app "
                                + "actually uses.\n\n"
                                + "Replaced with placeholder text: title, artist, comment, cue comments, "
                                + "filenames, and playlist/folder names.\n\n"
                                + "Removed entirely: artwork images, the detailed color and scrolling "
                                + "waveform data rekordbox's own player UI uses (not read by this app), and "
                                + "original file paths."
                        }
                    }
                }
            }

            GroupBox {
                label: Subtitle { text: "For the submission (optional; saved into MANIFEST.txt as entered)" }
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Label { text: "Hardware you use:" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Repeater {
                            model: root.hardwareGroups
                            delegate: ColumnLayout {
                                id: groupDelegate
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 4

                                TableHeaderLabel { label: groupDelegate.modelData.vendor }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 12
                                    Repeater {
                                        model: groupDelegate.modelData.items
                                        delegate: CheckBox {
                                            required property string modelData
                                            text: modelData
                                            checked: !!root.selectedHardware[modelData]
                                            enabled: !controller.busy
                                            onToggled: {
                                                var updated = Object.assign({}, root.selectedHardware);
                                                updated[modelData] = checked;
                                                root.selectedHardware = updated;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Label { text: "Other hardware (not listed above):" }
                    TextField {
                        Layout.fillWidth: true
                        placeholderText: "e.g. a controller or mixer not in the list"
                        enabled: !controller.busy
                        onTextEdited: root.otherHardware = text
                    }

                    Label { text: "Anything you'd like tested:" }
                    TextArea {
                        id: notesField
                        Layout.fillWidth: true
                        Layout.preferredHeight: 60
                        wrapMode: Text.WordWrap
                        enabled: !controller.busy
                    }
                }
            }

            RowLayout {
                spacing: 12
                Button {
                    text: "Generate"
                    enabled: !controller.busy && root.selectedStick !== null && root.outputDir.length > 0
                    onClicked: controller.run(
                        root.selectedStick.hasRekordbox ? root.selectedStick.rekordboxPath : "",
                        root.selectedStick.hasEngine ? root.selectedStick.enginePath : "",
                        root.outputDir, root.maxTracks, root.hardwareText, notesField.text)
                }
            }

            Label {
                visible: controller.errorMessage.length > 0
                text: controller.errorMessage
                color: Theme.danger
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ColumnLayout {
                visible: controller.summaryText.length > 0
                Layout.fillWidth: true
                spacing: 8
                Label {
                    text: "Written to " + controller.outputDir
                    color: Theme.good
                    font.bold: true
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    text: controller.summaryText
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                RowLayout {
                    spacing: 12
                    Button {
                        text: "Open Folder"
                        onClicked: Qt.openUrlExternally("file://" + controller.outputDir)
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: submitText.implicitHeight + 16
                    color: Theme.warnBg
                    border.color: Theme.warnBorder
                    radius: 4
                    Label {
                        id: submitText
                        anchors.fill: parent
                        anchors.margins: 8
                        wrapMode: Text.WordWrap
                        color: Theme.warnText
                        text: "To help test Seabass, review the files above, then attach that folder "
                            + "(zipped) to an email to sebas@kde.org. Nothing has been sent yet; this is a "
                            + "manual step you do yourself."
                    }
                }

                GroupBox {
                    label: Subtitle { text: "MANIFEST.txt" }
                    Layout.fillWidth: true
                    ScrollView {
                        anchors.fill: parent
                        implicitHeight: 300
                        ScrollBar.vertical: BigScrollBar {}
                        TextArea {
                            readOnly: true
                            wrapMode: Text.WordWrap
                            text: controller.manifestText
                            font.family: "monospace"
                            font.pointSize: Theme.fontSmall
                        }
                    }
                }
            }
        }
    }

    BusyOverlay {
        anchors.fill: parent
        busy: controller.busy
        current: controller.progressCurrent
        total: controller.progressTotal
        label: controller.currentPhase.length > 0 ? controller.currentPhase + "..." : "Working..."
    }
}
