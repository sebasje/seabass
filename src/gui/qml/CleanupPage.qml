import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

Page {
    id: root
    required property string stickLabel
    required property string rekordboxPath
    required property string enginePath
    required property var appSettingsController
    required property var playbackController

    readonly property bool hasRekordbox: rekordboxPath.length > 0
    readonly property bool hasEngine: enginePath.length > 0
    readonly property string format: {
        var pref = appSettingsController.preferredFormat;
        if (pref === "engine" && hasEngine) return "engine";
        if (pref === "rekordbox" && hasRekordbox) return "rekordbox";
        return hasEngine ? "engine" : "rekordbox";
    }
    function currentPath() {
        return root.format === "engine" ? root.enginePath : root.rekordboxPath;
    }
    function formatLabel(format) { return FormatLabels.label(format); }
    readonly property bool hasOneLibrary: root.hasRekordbox && cleanupController.hasOneLibrary(root.rekordboxPath)

    CleanupController {
        id: cleanupController
    }

    Component.onCompleted: cleanupController.scan(root.format, root.currentPath())
    onFormatChanged: cleanupController.scan(root.format, root.currentPath())

    function formatDuration(ms) {
        var totalSeconds = Math.round(ms / 1000);
        var m = Math.floor(totalSeconds / 60);
        var s = totalSeconds % 60;
        return m + ":" + (s < 10 ? "0" : "") + s;
    }

    header: ToolBar {
        // Opaque background override, see AppSettingsPage.qml's header
        // for why (KDE's Breeze style bleeds the window behind Seabass
        // through an unstyled ToolBar). This page is the one that made the
        // bug visible: with real "pending deletions" content behind a
        // translucent header, System Settings text bled through the top
        // strip of the window.
        background: Rectangle { color: Theme.surface }

        implicitHeight: headerLayout.implicitHeight + 20

        ColumnLayout {
            id: headerLayout
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                BackBreadcrumb {
                    middleLabel: "Clean-up and Housekeeping"
                    title: "Clean Up Duplicates"
                    backEnabled: !cleanupController.writing
                    onHomeRequested: root.StackView.view.pop(null)
                    onBackRequested: root.StackView.view.pop()
                }
                Item { Layout.fillWidth: true }
                LibrarySourceToggle {
                    current: root.format
                    hasRekordbox: root.hasRekordbox
                    hasEngine: root.hasEngine
                    hasOneLibrary: root.hasOneLibrary
                    onSourceRequested: (value) => root.appSettingsController.preferredFormat = value
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                TextField {
                    id: searchField
                    placeholderText: "Search title or artist..."
                    Layout.preferredWidth: 220
                    onTextChanged: cleanupController.search(text)
                }
                Label {
                    text: plansListView.count + " duplicate group(s) found"
                    color: Theme.textMuted
                }
                Label {
                    visible: plansListView.count > 0
                    text: "(" + cleanupController.totalWastedBytesHuman + " total if every copy kept only one file)"
                    color: Theme.textMuted
                }
                Item { Layout.fillWidth: true }
                Label {
                    visible: cleanupController.includedCount > 0
                    text: cleanupController.includedCount + " group(s) selected"
                    color: Theme.textMuted
                }
            }

            // A Flow, not a RowLayout: on a narrower window these four
            // buttons plus the toggle above no longer all fit on one
            // line, and a RowLayout just lets the trailing ones overflow
            // past the header's edge instead of wrapping onto a second
            // line the way this does.
            Flow {
                Layout.fillWidth: true
                spacing: 8
                Button {
                    text: "Select All"
                    enabled: !cleanupController.busy && plansListView.count > 0
                    onClicked: confirmSelectAllDialog.open()
                }
                Button {
                    text: "Deselect All"
                    enabled: !cleanupController.busy && plansListView.count > 0
                    onClicked: cleanupController.setAllIncluded(false)
                }
                Button {
                    text: "Clean Up Selected"
                    enabled: !cleanupController.busy && cleanupController.includedCount > 0
                    onClicked: confirmCleanupDialog.open()
                }
                Button {
                    text: "Undo"
                    visible: cleanupController.canUndo
                    enabled: !cleanupController.busy
                    ToolTip.visible: hovered
                    ToolTip.text: "Revert the last cleanup: restores every file it touched to what it was before"
                    onClicked: cleanupController.undoLastOperation()
                }
            }
        }
    }

    Dialog {
        id: confirmSelectAllDialog
        anchors.centerIn: parent
        modal: true
        width: 480
        title: "Select All " + plansListView.count + " Duplicate Group(s)?"
        footer: DialogButtonBox {
            Button { text: "Select All"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: cleanupController.setAllIncluded(true)

        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: "This marks all " + plansListView.count + " currently listed duplicate group(s) - "
                + cleanupController.totalWastedBytesHuman + " total if every copy kept only one file - "
                + "for the next \"Clean Up Selected\" click, including groups excluded by default because "
                + "their copies differ in quality (marked with ⚠ below).\n\n"
                + (searchField.text.length > 0
                    ? "Your search (\"" + searchField.text + "\") is currently narrowing this list. Clear it "
                        + "first if you meant to select across your whole library, or leave it as-is to select "
                        + "only these matching groups."
                    : "No search filter is active, so this selects every duplicate group found across your "
                        + "whole library.")
        }
    }

    Dialog {
        id: confirmCleanupDialog
        anchors.centerIn: parent
        modal: true
        width: 480
        title: "Clean Up " + cleanupController.includedCount + " Duplicate Group(s)?"
        footer: DialogButtonBox {
            Button { text: "Clean Up"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: cleanupController.apply()

        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: "For each selected group, every copy except the one kept will be removed from the library: "
                + "its hot/memory cues are merged onto the surviving copy first (nothing is lost), and any "
                + "playlist it belonged to is updated to reference the surviving copy instead.\n\n"
                + "This does NOT delete the removed copies' audio files. Their library entries are removed "
                + "and they're recorded for you to review and delete separately.\n\n"
                + "Everything touched is backed up first and can be undone."
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        StickWriteWarning {
            visible: cleanupController.writing
            text: "Cleaning up duplicates. Do not remove the stick until this finishes."
        }

        Label {
            visible: cleanupController.errorMessage.length > 0
            text: cleanupController.errorMessage
            color: Theme.danger
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            visible: cleanupController.statusMessage.length > 0
            text: cleanupController.statusMessage
            color: Theme.good
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        ListView {
            id: plansListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: cleanupController.plans
            spacing: 4

            ScrollBar.vertical: BigScrollBar {}

            delegate: Column {
                id: delegateRoot
                width: ListView.view.width
                spacing: 4

                required property int index
                required property var survivor
                required property var toRemove
                required property bool differs
                required property bool hasUnpreservableDataAtRisk
                required property string wastedBytesHuman
                required property int newCueCount
                required property bool included

                property bool expanded: false

                ItemDelegate {
                    width: parent.width
                    hoverEnabled: true
                    onClicked: delegateRoot.expanded = !delegateRoot.expanded

                    contentItem: ColumnLayout {
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            CheckBox {
                                checked: delegateRoot.included
                                onToggled: cleanupController.setIncluded(delegateRoot.index, checked)
                                ToolTip.visible: hovered
                                ToolTip.text: "Include this group in the next cleanup"
                            }
                            Label {
                                text: "Keeps: " + delegateRoot.survivor.title + " - " + delegateRoot.survivor.artist
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.preferredWidth: 320
                            }
                            Label {
                                text: "(removes " + delegateRoot.toRemove.length + " cop"
                                    + (delegateRoot.toRemove.length === 1 ? "y" : "ies") + ")"
                                color: Theme.textMuted
                            }
                            StatusBadge {
                                label: "ⓘ what's conserved"
                                badgeColor: Theme.textMuted
                                tooltipText: "Conserved: cues (merged, never lost) and playlist membership on both formats. "
                                    + "Every playlist a removed copy was in now points at the kept copy instead. "
                                    + "If the kept copy is missing bpm, musical key"
                                    + (root.format === "engine" ? "" : ", or artwork")
                                    + " and another copy has it, that's filled in too"
                                    + (root.format === "engine"
                                        ? " (artwork isn't included: Engine's library format has no way to write it back)."
                                        : ".")
                                    + (root.hasOneLibrary
                                        ? " Also mirrored into OneLibrary (exportLibrary.db), including removing the "
                                          + "duplicate's own OneLibrary row."
                                        : "")
                                    + " Not conserved: rating, comment, play count, and last-played date on the "
                                    + "removed copies aren't copied over."
                            }
                            StatusBadge {
                                visible: delegateRoot.differs
                                label: "⚠ copies differ"
                                badgeColor: Theme.conflictText
                                tooltipText: "These copies differ in quality and length. The higher-bitrate copy isn't the "
                                    + "longest one. This may be intentional (e.g. a shorter edit kept for specific "
                                    + "hardware), so this group is excluded by default. Check it above to include it anyway."
                            }
                            StatusBadge {
                                visible: delegateRoot.hasUnpreservableDataAtRisk
                                label: "⚠ data would be lost"
                                badgeColor: Theme.conflictText
                                tooltipText: "These copies have different rating, comment, play count, or last-played data, "
                                    + "none of which carries over to the kept copy. Removing the others would permanently "
                                    + "lose whichever values didn't happen to land on the kept copy, so this group is "
                                    + "excluded by default. Check it above to include it anyway."
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: delegateRoot.expanded ? "▾" : "▸"
                                font.pointSize: Theme.fontHuge
                                font.bold: true
                                color: Theme.textMuted
                            }
                        }
                        Label {
                            text: delegateRoot.wastedBytesHuman + " freed"
                                + (delegateRoot.newCueCount > 0 ? "; " + delegateRoot.newCueCount + " cue(s) merged onto the survivor" : "")
                            color: Theme.textMuted
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    visible: delegateRoot.expanded
                    height: delegateRoot.expanded ? groupColumn.implicitHeight + 16 : 0
                    color: Theme.groupBackground
                    border.color: Theme.borderSubtle
                    radius: 4

                    ColumnLayout {
                        id: groupColumn
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8

                        // Richer per-copy view (waveform + real cues, not
                        // just bitrate/duration/size) so it's directly
                        // visible -- not just claimed in the text above --
                        // that a removed copy's cues really do end up on
                        // the kept one. Status pill makes which is which
                        // impossible to miss at a glance.
                        TrackWaveformCard {
                            track: delegateRoot.survivor
                            formatLabelText: root.formatLabel(delegateRoot.survivor.side) + " - "
                                + (delegateRoot.survivor.bitrate > 0 ? delegateRoot.survivor.bitrate + " kbps, " : "")
                                + root.formatDuration(delegateRoot.survivor.durationMs) + ", "
                                + delegateRoot.survivor.sizeHuman
                            statusBadgeText: "KEEPING"
                            statusBadgeBg: Theme.groupBackground
                            statusBadgeBorder: Theme.good
                            statusBadgeTextColor: Theme.good
                            playbackController: root.playbackController
                            playbackPath: root.currentPath()
                        }

                        Repeater {
                            model: delegateRoot.toRemove
                            delegate: TrackWaveformCard {
                                required property var modelData
                                track: modelData
                                formatLabelText: root.formatLabel(modelData.side) + " - "
                                    + (modelData.bitrate > 0 ? modelData.bitrate + " kbps, " : "")
                                    + root.formatDuration(modelData.durationMs) + ", " + modelData.sizeHuman
                                statusBadgeText: "REMOVING"
                                statusBadgeBg: Theme.dangerBg
                                statusBadgeBorder: Theme.dangerBorder
                                statusBadgeTextColor: Theme.dangerText
                                playbackController: root.playbackController
                                playbackPath: root.currentPath()
                            }
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: plansListView.count === 0 && !cleanupController.busy
                text: "No duplicate tracks with a removable copy found."
                color: Theme.textMuted
            }
        }
    }

    BusyOverlay {
        anchors.fill: parent
        busy: cleanupController.busy
        current: cleanupController.scanCurrent
        total: cleanupController.scanTotal
        label: cleanupController.writing ? "Cleaning up duplicates..." : "Scanning for duplicates..."
    }
}
