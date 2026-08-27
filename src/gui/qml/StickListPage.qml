import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var mediaController
    required property var playbackController
    signal browseRequested(string stickLabel, string format, string path, string siblingRekordboxPath)
    signal duplicatesRequested(string stickLabel, string format, string path)
    signal settingsRequested(string stickLabel, string pioneerRoot)
    signal syncRequested(string stickLabel, string rekordboxPath, string enginePath)
    signal appSettingsRequested()

    // Keyed by devicePath (stable across mount state changes) rather than
    // stored per-delegate: mounting/unmounting refreshes the whole sticks
    // list (a full model reset), which destroys and recreates every
    // delegate -- per-delegate "expanded" state would collapse right back
    // on every mount/unmount click.
    property string expandedDevicePath: ""

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Label {
                text: "USB Sticks"
                font.bold: true
                font.pointSize: 16
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "Refresh"
                ToolTip.visible: hovered
                ToolTip.text: "Re-scan for USB sticks"
                onClicked: root.mediaController.detect()
            }
            ToolButton {
                text: "⚙"
                font.family: "Noto Sans Symbols"
                font.pointSize: 14
                ToolTip.visible: hovered
                ToolTip.text: "App Settings"
                onClicked: root.appSettingsRequested()
            }
        }

        Label {
            visible: root.mediaController.errorMessage.length > 0
            text: root.mediaController.errorMessage
            color: "red"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.mediaController.sticks
            clip: true
            spacing: 4

            delegate: Column {
                id: delegateRoot
                width: ListView.view.width
                spacing: 4

                required property string label
                required property string mountPoint
                required property string devicePath
                required property bool mounted
                required property bool hasRekordbox
                required property bool hasEngine
                required property string rekordboxPath
                required property string enginePath

                readonly property bool expanded: root.expandedDevicePath === delegateRoot.devicePath

                ItemDelegate {
                    width: parent.width
                    onClicked: root.expandedDevicePath = delegateRoot.expanded ? "" : delegateRoot.devicePath

                    contentItem: RowLayout {
                        spacing: 12

                        UsbStickIcon {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            Layout.alignment: Qt.AlignVCenter
                            color: "#8a97a0"
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: delegateRoot.label
                                    font.bold: true
                                    font.pointSize: 12
                                }
                                Label {
                                    text: delegateRoot.mounted ? "" : "(not mounted)"
                                    color: "gray"
                                }
                                Item { Layout.fillWidth: true }
                                Label {
                                    text: delegateRoot.expanded ? "▾" : "▸"
                                    color: "gray"
                                }
                            }
                            Label {
                                text: delegateRoot.mounted ? delegateRoot.mountPoint : delegateRoot.devicePath
                                color: "gray"
                                font.pointSize: 9
                            }
                            RowLayout {
                                visible: delegateRoot.mounted
                                Label { text: "Rekordbox: " + (delegateRoot.hasRekordbox ? "yes" : "no") }
                                Label { text: "  Engine: " + (delegateRoot.hasEngine ? "yes" : "no") }
                            }
                        }

                        ToolButton {
                            text: "⏏"
                            font.family: "Noto Sans Symbols2"
                            font.pixelSize: 26
                            rotation: delegateRoot.mounted ? 0 : 180
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: 48
                            Layout.alignment: Qt.AlignVCenter
                            ToolTip.visible: hovered
                            ToolTip.text: delegateRoot.mounted ? "Unmount " + delegateRoot.label : "Mount " + delegateRoot.label
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
                }

                ColumnLayout {
                    width: parent.width
                    visible: delegateRoot.expanded
                    spacing: 8

                    RowLayout {
                        spacing: 8
                        Button {
                            text: "Browse Rekordbox Library"
                            enabled: delegateRoot.hasRekordbox
                            ToolTip.visible: hovered
                            ToolTip.text: "Browse tracks and playlists on the Rekordbox side of this stick"
                            onClicked: root.browseRequested(delegateRoot.label, "rekordbox", delegateRoot.rekordboxPath, "")
                        }
                        Button {
                            text: "Browse Engine Library"
                            enabled: delegateRoot.hasEngine
                            ToolTip.visible: hovered
                            ToolTip.text: "Browse tracks and playlists on the Engine side of this stick"
                            onClicked: root.browseRequested(delegateRoot.label, "engine", delegateRoot.enginePath, delegateRoot.rekordboxPath)
                        }
                    }
                    RowLayout {
                        spacing: 8
                        Button {
                            text: "Duplicate Tracks (Rekordbox)"
                            enabled: delegateRoot.hasRekordbox
                            ToolTip.visible: hovered
                            ToolTip.text: "Find tracks that appear more than once and consolidate their cue points"
                            onClicked: root.duplicatesRequested(delegateRoot.label, "rekordbox", delegateRoot.rekordboxPath)
                        }
                        Button {
                            text: "Duplicate Tracks (Engine)"
                            enabled: delegateRoot.hasEngine
                            ToolTip.visible: hovered
                            ToolTip.text: "Find tracks that appear more than once and consolidate their cue points"
                            onClicked: root.duplicatesRequested(delegateRoot.label, "engine", delegateRoot.enginePath)
                        }
                        Button {
                            text: "Device Settings"
                            enabled: delegateRoot.hasRekordbox
                            ToolTip.visible: hovered
                            ToolTip.text: "View this stick's saved Rekordbox player/device settings"
                            onClicked: root.settingsRequested(delegateRoot.label, delegateRoot.rekordboxPath)
                        }
                        Button {
                            text: "Sync Cues Between Formats"
                            enabled: delegateRoot.hasRekordbox && delegateRoot.hasEngine
                            ToolTip.visible: hovered
                            ToolTip.text: "Copy cue points between Rekordbox and Engine for tracks present on both sides"
                            onClicked: root.syncRequested(delegateRoot.label, delegateRoot.rekordboxPath, delegateRoot.enginePath)
                        }
                        Button {
                            text: "Manage Backups (Coming Soon)"
                            enabled: false
                            ToolTip.visible: hovered
                            ToolTip.text: "List and clean up automatic backups made before cue writes"
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: parent.count === 0
                text: "No USB sticks detected. Insert one -- this list updates automatically."
                color: "gray"
            }
        }
    }
}
