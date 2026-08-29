import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DjConvertGui

// USB Stick Statistics: filesystem/hardware facts, per-catalog library
// stats, a Filelight-style disk usage breakdown, and a local read-speed
// benchmark with history. Read-only, like Browse Library -- this page
// never writes anything to the stick.
Page {
    id: root
    required property string stickLabel
    required property string rekordboxPath
    required property string enginePath

    StickStatisticsController {
        id: controller
    }

    property string currentSource: root.rekordboxPath.length > 0 ? "rekordbox"
        : (root.enginePath.length > 0 ? "engine" : "onelibrary")
    // Drill-down stack for the treemap: root category view first, pushed
    // into when a box with children is clicked.
    property var treemapStack: []
    readonly property var currentTreemapNode: treemapStack.length > 0 ? treemapStack[treemapStack.length - 1] : null

    function humanBytes(bytes) {
        if (!bytes || bytes <= 0) return "0 B";
        var units = ["B", "KiB", "MiB", "GiB", "TiB"];
        var value = bytes;
        var unitIndex = 0;
        while (value >= 1024 && unitIndex < units.length - 1) {
            value /= 1024;
            unitIndex++;
        }
        return value.toFixed(unitIndex === 0 ? 0 : 1) + " " + units[unitIndex];
    }

    function statsForSource(source) {
        if (source === "engine") return controller.engineStats;
        if (source === "onelibrary") return controller.oneLibraryStats;
        return controller.rekordboxStats;
    }

    function rescan() {
        root.treemapStack = [];
        controller.scan(root.stickLabel, root.rekordboxPath, root.enginePath);
    }

    Connections {
        target: controller
        function onResultsChanged() {
            if (controller.diskUsage && controller.diskUsage.root) {
                root.treemapStack = [controller.diskUsage.root];
            }
        }
    }

    Component.onCompleted: rescan()

    header: ToolBar {
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
                text: root.stickLabel + " - Stick Statistics"
                font.bold: true
                font.pointSize: Theme.fontLarge
            }
            Item { Layout.fillWidth: true }
            BusyIndicator { running: controller.busy; visible: controller.busy; implicitWidth: 20; implicitHeight: 20 }
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 16
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 16

            Label {
                visible: controller.errorMessage.length > 0
                text: controller.errorMessage
                color: Theme.danger
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            // -- Filesystem & capacity --------------------------------
            GroupBox {
                title: "Filesystem"
                Layout.fillWidth: true
                visible: Object.keys(controller.filesystemInfo).length > 0

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Label {
                            text: controller.filesystemInfo.displayName || "Unknown"
                            font.bold: true
                            font.pointSize: Theme.fontLarge
                        }
                        Rectangle {
                            radius: 3
                            color: controller.filesystemInfo.recommendedForDjHardware ? Theme.warnBg : Theme.dangerBg
                            border.color: controller.filesystemInfo.recommendedForDjHardware ? Theme.warnBorder : Theme.dangerBorder
                            implicitWidth: recLabel.implicitWidth + 12
                            implicitHeight: recLabel.implicitHeight + 6
                            Label {
                                id: recLabel
                                anchors.centerIn: parent
                                text: controller.filesystemInfo.recommendedForDjHardware
                                    ? "Good for DJ hardware" : "Not typical for DJ hardware"
                                font.pointSize: Theme.fontTiny
                                font.bold: true
                                color: controller.filesystemInfo.recommendedForDjHardware ? Theme.warnText : Theme.dangerText
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: controller.filesystemInfo.usbSpeedLabel || "USB speed unknown"
                            color: Theme.textMuted
                        }
                    }

                    Label {
                        text: "Max file size: " + (controller.filesystemInfo.maxFileSize || "Unknown")
                        color: Theme.textMuted
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: controller.filesystemInfo.hardwareNotes || ""
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 18
                            radius: 4
                            color: Theme.groupBackground
                            border.color: Theme.borderSubtle
                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                radius: 4
                                width: parent.width * (controller.filesystemInfo.totalBytes > 0
                                    ? (1 - controller.filesystemInfo.freeBytes / controller.filesystemInfo.totalBytes) : 0)
                                color: Theme.accent
                            }
                        }
                        Label {
                            text: root.humanBytes(controller.filesystemInfo.totalBytes - controller.filesystemInfo.freeBytes)
                                + " used of " + root.humanBytes(controller.filesystemInfo.totalBytes)
                            color: Theme.textMuted
                            font.pointSize: Theme.fontSmall
                        }
                    }
                }
            }

            // -- Library statistics ------------------------------------
            GroupBox {
                title: "Library Statistics"
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    LibrarySourceToggle {
                        current: root.currentSource
                        hasRekordbox: Object.keys(controller.rekordboxStats).length > 0
                        hasEngine: Object.keys(controller.engineStats).length > 0
                        hasOneLibrary: Object.keys(controller.oneLibraryStats).length > 0
                        onSourceRequested: (value) => root.currentSource = value
                    }

                    ColumnLayout {
                        id: statsSection
                        Layout.fillWidth: true
                        spacing: 10
                        visible: Object.keys(root.statsForSource(root.currentSource)).length > 0

                        readonly property var stats: root.statsForSource(root.currentSource)

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 4
                            columnSpacing: 16
                            rowSpacing: 8

                            component StatTile: ColumnLayout {
                                property string label
                                property string value
                                spacing: 0
                                Label { text: value; font.bold: true; font.pointSize: Theme.fontLarge }
                                Label { text: label; color: Theme.textMuted; font.pointSize: Theme.fontSmall }
                            }

                            StatTile { label: "Tracks"; value: statsSection.stats.trackCount || 0 }
                            StatTile { label: "Playlists"; value: statsSection.stats.playlistCount || 0 }
                            StatTile { label: "Cue points"; value: statsSection.stats.totalCuePoints || 0 }
                            StatTile {
                                label: "Hot / memory cues"
                                value: (statsSection.stats.hotCueCount || 0) + " / " + (statsSection.stats.memoryCueCount || 0)
                            }
                            StatTile { label: "Rated tracks"; value: statsSection.stats.ratedTrackCount || 0 }
                            StatTile { label: "Commented tracks"; value: statsSection.stats.commentedTrackCount || 0 }
                            StatTile { label: "Streaming tracks"; value: statsSection.stats.streamingTrackCount || 0 }
                        }

                        RowLayout {
                            visible: Object.keys(root.statsForSource(root.currentSource).streamingTracksByService || {}).length > 0
                            spacing: 8
                            Label { text: "Streaming services:"; color: Theme.textMuted }
                            Repeater {
                                model: Object.keys(root.statsForSource(root.currentSource).streamingTracksByService || {})
                                delegate: Label {
                                    required property string modelData
                                    text: modelData + " (" + root.statsForSource(root.currentSource)
                                        .streamingTracksByService[modelData] + ")"
                                    font.bold: true
                                }
                            }
                        }

                        component DistributionSection: ColumnLayout {
                            property string title
                            property var entries  // [{label, count}], already sorted
                            Layout.fillWidth: true
                            spacing: 4
                            visible: entries.length > 0
                            Label { text: title; font.bold: true; Layout.topMargin: 8 }
                            Repeater {
                                model: entries
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Label { text: modelData.label; Layout.preferredWidth: 90; elide: Text.ElideRight }
                                    Rectangle {
                                        Layout.fillWidth: true
                                        implicitHeight: 14
                                        radius: 3
                                        color: Theme.groupBackground
                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            radius: 3
                                            width: parent.width * (entries[0].count > 0 ? modelData.count / entries[0].count : 0)
                                            color: Theme.accent
                                        }
                                    }
                                    Label { text: modelData.count; Layout.preferredWidth: 36; horizontalAlignment: Text.AlignRight }
                                }
                            }
                        }

                        DistributionSection {
                            Layout.fillWidth: true
                            title: "Tracks per key"
                            entries: {
                                var stats = root.statsForSource(root.currentSource);
                                var keys = stats.tracksPerKey || {};
                                var list = [];
                                for (var k in keys) list.push({label: k, count: keys[k]});
                                list.sort((a, b) => b.count - a.count);
                                return list.slice(0, 12);
                            }
                        }

                        DistributionSection {
                            Layout.fillWidth: true
                            title: "File formats"
                            entries: {
                                var stats = root.statsForSource(root.currentSource);
                                var formats = stats.tracksPerFileFormat || {};
                                var list = [];
                                for (var f in formats) list.push({label: f, count: formats[f]});
                                list.sort((a, b) => b.count - a.count);
                                return list;
                            }
                        }

                        DistributionSection {
                            Layout.fillWidth: true
                            title: "BPM distribution"
                            entries: {
                                var stats = root.statsForSource(root.currentSource);
                                var buckets = stats.bpmDistribution || [];
                                var list = [];
                                for (var i = 0; i < buckets.length; i++) {
                                    list.push({label: buckets[i].rangeStart + "-" + (buckets[i].rangeStart + 9), count: buckets[i].count});
                                }
                                list.sort((a, b) => b.count - a.count);
                                return list;
                            }
                        }
                    }
                }
            }

            // -- Disk usage ---------------------------------------------
            GroupBox {
                title: "Disk Usage"
                Layout.fillWidth: true
                Layout.preferredHeight: 420

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Button {
                            text: "◂ Back"
                            enabled: root.treemapStack.length > 1
                            onClicked: root.treemapStack = root.treemapStack.slice(0, root.treemapStack.length - 1)
                        }
                        Label {
                            text: root.currentTreemapNode ? root.currentTreemapNode.label : ""
                            font.bold: true
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: controller.diskUsage.usedBytes !== undefined
                                ? (root.humanBytes(controller.diskUsage.usedBytes) + " used, "
                                    + root.humanBytes(controller.diskUsage.freeBytes) + " free")
                                : ""
                            color: Theme.textMuted
                        }
                    }

                    TreemapView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        node: root.currentTreemapNode
                        onBoxClicked: (childNode) => root.treemapStack = root.treemapStack.concat([childNode])
                    }
                }
            }

            // -- Speed benchmark -----------------------------------------
            GroupBox {
                title: "Read-Speed Benchmark"
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Button {
                            text: controller.benchmarkRunning ? "Running..." : "Run Benchmark"
                            enabled: !controller.benchmarkRunning && !controller.busy
                            onClicked: controller.runBenchmark()
                        }
                        BusyIndicator {
                            running: controller.benchmarkRunning
                            visible: controller.benchmarkRunning
                            implicitWidth: 20
                            implicitHeight: 20
                        }
                        Label {
                            visible: controller.benchmarkErrorMessage.length > 0
                            text: controller.benchmarkErrorMessage
                            color: Theme.danger
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pointSize: Theme.fontSmall
                        text: "Reads real sample files already on this stick to measure MiB/s, then computes a "
                            + "comparative-only score (not an absolute/manufacturer number) so sticks tested on "
                            + "this computer can be ranked against each other over time."
                    }

                    Label {
                        text: "History for this stick (" + controller.benchmarkHistory.length + " run(s))"
                        font.bold: true
                        Layout.topMargin: 8
                    }
                    Repeater {
                        model: controller.benchmarkHistory
                        delegate: Frame {
                            required property var modelData
                            Layout.fillWidth: true
                            contentItem: RowLayout {
                                spacing: 12
                                Label { text: modelData.ranAt; color: Theme.textMuted; font.pointSize: Theme.fontSmall }
                                Label { text: "Score: " + modelData.score; font.bold: true }
                                Label { text: modelData.usbSpeedLabel; color: Theme.textMuted; font.pointSize: Theme.fontSmall }
                                Label {
                                    text: "DB " + modelData.databaseReadMbps.toFixed(1) + " MiB/s, Audio "
                                        + modelData.audioReadMbps.toFixed(1) + " MiB/s"
                                    color: Theme.textMuted
                                    font.pointSize: Theme.fontSmall
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }
                    Label {
                        visible: controller.benchmarkHistory.length === 0
                        text: "No benchmark runs yet for this stick."
                        color: Theme.textMuted
                    }
                }
            }
        }
    }
}
