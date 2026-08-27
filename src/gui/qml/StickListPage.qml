import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DjConvertGui

Item {
    id: root
    required property var mediaController
    required property var playbackController
    required property var appSettingsController
    signal browseRequested(string stickLabel, string rekordboxPath, string enginePath)
    signal duplicatesRequested(string stickLabel, string rekordboxPath, string enginePath)
    signal settingsRequested(string stickLabel, string pioneerRoot)
    signal syncRequested(string stickLabel, string rekordboxPath, string enginePath)
    signal appSettingsRequested()
    signal backupsRequested(string stickLabel, string rekordboxPath, string enginePath)
    signal localCueRequested(string stickLabel, string rekordboxPath, string enginePath)
    signal aboutRequested()

    // Keyed by devicePath (stable across mount state changes) rather than
    // stored per-delegate: mounting/unmounting refreshes the whole sticks
    // list (a full model reset), which destroys and recreates every
    // delegate -- per-delegate "expanded" state would collapse right back
    // on every mount/unmount click.
    //
    // Tracks which sticks are COLLAPSED (not expanded) -- inverted from the
    // more obvious "expanded set" so the default (nothing in it) means every
    // stick starts expanded without needing to know their device paths
    // ahead of time.
    property var collapsedDevicePaths: ({})

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
            ToolButton {
                text: "ⓘ"
                font.pointSize: 14
                ToolTip.visible: hovered
                ToolTip.text: "About Seabass"
                onClicked: root.aboutRequested()
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

            delegate: Frame {
                id: delegateRoot
                width: ListView.view.width

                required property string label
                required property string mountPoint
                required property string devicePath
                required property bool mounted
                required property bool hasRekordbox
                required property bool hasEngine
                required property string rekordboxPath
                required property string enginePath

                readonly property bool expanded: !root.collapsedDevicePaths[delegateRoot.devicePath]

                // The frame visually groups this stick's header and its
                // action cards into one card, so with several sticks
                // detected each one's controls read as clearly belonging
                // to it rather than blurring into the list.
                background: Rectangle {
                    color: "#1a1f24"
                    border.color: "#333a40"
                    radius: 4
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4

                ItemDelegate {
                    Layout.fillWidth: true
                    onClicked: {
                        var next = Object.assign({}, root.collapsedDevicePaths);
                        if (delegateRoot.expanded) {
                            next[delegateRoot.devicePath] = true;
                        } else {
                            delete next[delegateRoot.devicePath];
                        }
                        root.collapsedDevicePaths = next;
                    }

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
                    Layout.fillWidth: true
                    visible: delegateRoot.expanded

                    GridLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        Layout.bottomMargin: 8
                        columns: 3
                        columnSpacing: 12
                        rowSpacing: 12

                        component ActionCard: Button {
                            id: card
                            property string cardTitle
                            property string cardSubtitle
                            property string cardIcon
                            property string cardIconFont: "Noto Sans Symbols2"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 68
                            ToolTip.visible: hovered
                            ToolTip.text: cardSubtitle

                            contentItem: RowLayout {
                                spacing: 10
                                Label {
                                    text: card.cardIcon
                                    font.family: card.cardIconFont
                                    font.pixelSize: 26
                                    color: card.enabled ? "#8a97a0" : "#4a5157"
                                    Layout.preferredWidth: 30
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Label {
                                        text: card.cardTitle
                                        font.bold: true
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        text: card.cardSubtitle
                                        color: card.enabled ? "gray" : "#5a5a5a"
                                        font.pointSize: 9
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                        }

                        ActionCard {
                            cardTitle: "Browse Library"
                            cardSubtitle: "View tracks, playlists and cues"
                            cardIcon: "▤"
                            enabled: delegateRoot.hasRekordbox || delegateRoot.hasEngine
                            onClicked: root.browseRequested(delegateRoot.label, delegateRoot.rekordboxPath, delegateRoot.enginePath)
                        }
                        ActionCard {
                            cardTitle: "Duplicate Tracks"
                            cardSubtitle: "Find and consolidate repeated tracks"
                            cardIcon: "▣"
                            enabled: delegateRoot.hasRekordbox || delegateRoot.hasEngine
                            onClicked: root.duplicatesRequested(delegateRoot.label, delegateRoot.rekordboxPath, delegateRoot.enginePath)
                        }
                        ActionCard {
                            cardTitle: "Sync Cues Between Formats"
                            cardSubtitle: "Copy cues between Rekordbox and Engine"
                            cardIcon: "⇄"
                            cardIconFont: "Noto Sans Math"
                            enabled: delegateRoot.hasRekordbox && delegateRoot.hasEngine
                            onClicked: root.syncRequested(delegateRoot.label, delegateRoot.rekordboxPath, delegateRoot.enginePath)
                        }
                        ActionCard {
                            cardTitle: "Local Cue Backup"
                            cardSubtitle: "Back up or restore cues on this computer"
                            cardIcon: "💿"
                            enabled: delegateRoot.hasRekordbox || delegateRoot.hasEngine
                            onClicked: root.localCueRequested(delegateRoot.label, delegateRoot.rekordboxPath, delegateRoot.enginePath)
                        }
                        ActionCard {
                            cardTitle: "Manage Backups"
                            cardSubtitle: "List and clean up automatic write backups"
                            cardIcon: "🗄"
                            enabled: delegateRoot.hasRekordbox || delegateRoot.hasEngine
                            onClicked: root.backupsRequested(delegateRoot.label, delegateRoot.rekordboxPath, delegateRoot.enginePath)
                        }
                        ActionCard {
                            cardTitle: "Device Settings"
                            cardSubtitle: "View this stick's saved Rekordbox player settings"
                            cardIcon: "⚙"
                            cardIconFont: "Noto Sans Symbols"
                            enabled: delegateRoot.hasRekordbox
                            onClicked: root.settingsRequested(delegateRoot.label, delegateRoot.rekordboxPath)
                        }
                    }
                }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: parent.count === 0
                text: "No USB sticks detected. Insert one to get started."
                font.pointSize: 14
                color: "#999"
            }
        }
    }
}
