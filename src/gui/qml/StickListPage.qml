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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Label {
                text: "USB sticks"
                font.bold: true
                font.pointSize: 16
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "Refresh"
                onClicked: root.mediaController.detect()
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

                property bool expanded: false

                ItemDelegate {
                    width: parent.width
                    onClicked: delegateRoot.expanded = !delegateRoot.expanded

                    contentItem: ColumnLayout {
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
                            Label { text: "rekordbox: " + (delegateRoot.hasRekordbox ? "yes" : "no") }
                            Label { text: "  Engine: " + (delegateRoot.hasEngine ? "yes" : "no") }
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
                            text: delegateRoot.mounted ? "Unmount" : "Mount"
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
                        Button {
                            text: "Browse rekordbox library"
                            enabled: delegateRoot.hasRekordbox
                            onClicked: root.browseRequested(delegateRoot.label, "rekordbox", delegateRoot.rekordboxPath, "")
                        }
                        Button {
                            text: "Browse Engine library"
                            enabled: delegateRoot.hasEngine
                            onClicked: root.browseRequested(delegateRoot.label, "engine", delegateRoot.enginePath, delegateRoot.rekordboxPath)
                        }
                    }
                    RowLayout {
                        spacing: 8
                        Button {
                            text: "Duplicate tracks (rekordbox)"
                            enabled: delegateRoot.hasRekordbox
                            onClicked: root.duplicatesRequested(delegateRoot.label, "rekordbox", delegateRoot.rekordboxPath)
                        }
                        Button {
                            text: "Duplicate tracks (Engine)"
                            enabled: delegateRoot.hasEngine
                            onClicked: root.duplicatesRequested(delegateRoot.label, "engine", delegateRoot.enginePath)
                        }
                        Button {
                            text: "Device settings"
                            enabled: delegateRoot.hasRekordbox
                            onClicked: root.settingsRequested(delegateRoot.label, delegateRoot.rekordboxPath)
                        }
                        Button { text: "Sync cues between formats (coming soon)"; enabled: false }
                        Button { text: "Manage backups (coming soon)"; enabled: false }
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
