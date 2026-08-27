import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DjConvertGui

// A Page, not a plain Item, specifically so it gets the same
// Material-style implicit background every other page in this app gets
// for free -- a plain Item has no background mechanism at all, which is
// exactly why this page (the very first one shown) kept rendering the
// Qt-default white despite the window/StackView-level background fixes
// applied elsewhere, while every `Page`-rooted page rendered correctly.
Page {
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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Label {
                text: "USB Sticks"
                font.bold: true
                font.pointSize: Theme.fontXLarge
            }
            Item { Layout.fillWidth: true }
            ToolButton {
                text: "ⓘ"
                font.pointSize: Theme.fontLarge
                ToolTip.visible: hovered
                ToolTip.text: "About Seabass"
                onClicked: root.aboutRequested()
            }
            ToolButton {
                text: "⚙"
                font.family: "Noto Sans Symbols"
                font.pointSize: Theme.fontLarge
                ToolTip.visible: hovered
                ToolTip.text: "App Settings"
                onClicked: root.appSettingsRequested()
            }
        }

        Label {
            visible: root.mediaController.errorMessage.length > 0
            text: root.mediaController.errorMessage
            color: Theme.danger
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.mediaController.sticks
            clip: true
            spacing: 4

            // A plain Rectangle, not a Frame -- Qt Quick Controls' Material
            // style gives Frame its own additional implicit chrome that a
            // custom `background:` assignment doesn't fully replace (kept
            // rendering a stray light margin around the real content no
            // matter what the override's own color was set to). Every
            // other grouping frame in this app (SyncPage.qml/
            // DuplicatesPage.qml's meta-track groups) already uses this
            // same plain-Rectangle pattern for exactly that reason.
            delegate: Rectangle {
                id: delegateRoot
                width: ListView.view.width
                height: contentColumn.implicitHeight + 24
                color: Theme.surface
                border.color: Theme.border
                radius: 4

                required property string label
                required property string mountPoint
                required property string devicePath
                required property bool mounted
                required property bool hasRekordbox
                required property bool hasEngine
                required property string rekordboxPath
                required property string enginePath

                ColumnLayout {
                    id: contentColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    // A plain Item + explicit MouseArea, not an ItemDelegate
                    // -- ItemDelegate's own hover/press background isn't
                    // reliably gated by `enabled` in this KDE-Breeze/Material
                    // style mashup (confirmed: `enabled: !mounted` still left
                    // the row hover-highlighting and accepting clicks once
                    // mounted). ScanPage.qml's track rows already hit the
                    // exact same class of Material-Control-chrome issue and
                    // settled on this same Rectangle+MouseArea sidestep --
                    // see its comment for the fuller story.
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: rowContent.implicitHeight

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -6
                            radius: 4
                            visible: !delegateRoot.mounted
                            color: rowMouseArea.pressed ? Theme.rowPressed
                                : rowMouseArea.containsMouse ? Theme.rowHover
                                : "transparent"
                        }

                        MouseArea {
                            id: rowMouseArea
                            anchors.fill: parent
                            enabled: !delegateRoot.mounted
                            hoverEnabled: !delegateRoot.mounted
                            ToolTip.visible: containsMouse
                            ToolTip.text: "Click to mount " + delegateRoot.label
                            onClicked: root.mediaController.mountStick(delegateRoot.devicePath)
                        }

                        RowLayout {
                            id: rowContent
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 12

                            UsbStickIcon {
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 40
                                Layout.alignment: Qt.AlignVCenter
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        text: delegateRoot.label
                                        color: Theme.text
                                        font.bold: true
                                        font.pointSize: Theme.baseFontPointSize * 1.2
                                    }
                                    Label {
                                        text: delegateRoot.mounted ? "" : "(not mounted)"
                                        color: Theme.textMuted
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                                Label {
                                    text: delegateRoot.mounted ? delegateRoot.mountPoint : delegateRoot.devicePath
                                    color: Theme.textMuted
                                    font.pointSize: Theme.baseFontPointSize * 0.9
                                }
                                RowLayout {
                                    visible: delegateRoot.mounted
                                    Label { text: "Rekordbox: " + (delegateRoot.hasRekordbox ? "yes" : "no"); color: Theme.text }
                                    Label { text: "  Engine: " + (delegateRoot.hasEngine ? "yes" : "no"); color: Theme.text }
                                }
                            }
                        }
                    }

                    ToolButton {
                        text: "⏏"
                        font.family: "Noto Sans Symbols2"
                        font.pointSize: Theme.fontHuge
                        // Rotating the eject glyph 180° to mean "mount"
                        // isn't a real convention -- it just reads as
                        // an upside-down (broken-looking) eject icon.
                        // Kept upright always; the tooltip (and now
                        // click-anywhere-on-the-row) carry the "mount"
                        // meaning instead.
                        Layout.preferredWidth: 48
                        Layout.preferredHeight: 48
                        Layout.alignment: Qt.AlignVCenter
                        ToolTip.visible: hovered
                        ToolTip.text: delegateRoot.mounted ? "Eject " + delegateRoot.label : "Mount " + delegateRoot.label
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

                ColumnLayout {
                    Layout.fillWidth: true

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

                            contentItem: RowLayout {
                                spacing: 10
                                Label {
                                    text: card.cardIcon
                                    font.family: card.cardIconFont
                                    font.pointSize: Theme.fontHuge
                                    color: card.enabled ? Theme.textMuted : Qt.darker(Theme.textMuted, 1.6)
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
                                        color: card.enabled ? Theme.textMuted : Qt.darker(Theme.textMuted, 1.6)
                                        font.pointSize: Theme.baseFontPointSize * 0.9
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
                font.pointSize: Theme.fontLarge
                color: Theme.textMuted
            }
        }
    }
}
