import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

Page {
    id: root
    required property var appSettingsController

    signal anonymizeLibraryRequested()

    header: ToolBar {
        // Explicit opaque background. KDE's platform theme integration
        // resolves ToolBar to its own "org.kde.breeze" style regardless of
        // this app's Material palette (see main.cpp's exportMaterialPalette()
        // comment for the fuller story of why that isn't forced away
        // globally), and Breeze's own ToolBar background can render
        // translucent/blurred, letting whatever window is behind Seabass
        // show through the header. A plain opaque Rectangle sidesteps
        // whichever style actually resolves, same fix class as
        // StickListPage.qml's Frame-vs-Rectangle precedent.
        background: Rectangle { color: Theme.surface }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            ToolButton {
                text: "‹"

                font.pointSize: Theme.fontHuge

                ToolTip.visible: hovered

                ToolTip.text: "Back"
                onClicked: root.StackView.view.pop()
            }
            PageTitle {
                text: "App Settings"
            }
            Item { Layout.fillWidth: true }
        }
    }

    ColumnLayout {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 24
        spacing: 12

        Subtitle {
            text: "Theme"
        }
        ButtonGroup { id: themeGroup }
        RadioButton {
            text: "Dark"
            ButtonGroup.group: themeGroup
            checked: !root.appSettingsController.useSystemTheme
            onCheckedChanged: if (checked) root.appSettingsController.useSystemTheme = false
        }
        RadioButton {
            text: "Match System Theme"
            ButtonGroup.group: themeGroup
            checked: root.appSettingsController.useSystemTheme
            onCheckedChanged: if (checked) root.appSettingsController.useSystemTheme = true
        }

        Subtitle {
            text: "Musical key notation"
            Layout.topMargin: 12
        }
        RowLayout {
            spacing: 12
            ButtonGroup { id: keyNotationGroup }
            RadioButton {
                text: "Camelot (e.g. 6A)"
                ButtonGroup.group: keyNotationGroup
                checked: root.appSettingsController.keyNotation !== "traditional"
                onCheckedChanged: if (checked) root.appSettingsController.keyNotation = "camelot"
            }
            RadioButton {
                text: "Traditional (e.g. F♯m)"
                ButtonGroup.group: keyNotationGroup
                checked: root.appSettingsController.keyNotation === "traditional"
                onCheckedChanged: if (checked) root.appSettingsController.keyNotation = "traditional"
            }
            // Live preview, not just a description -- the two notations
            // read differently enough (a color-coded wheel position vs.
            // an actual note name) that seeing one example update as you
            // switch is clearer than describing the difference in text.
            KeyBadge {
                keyName: "F#m"
                notation: root.appSettingsController.keyNotation
                Layout.leftMargin: 8
            }
        }
        Label {
            text: "Applies to every key badge in Browse Library and Library Statistics. Either way, the\n"
                + "badge's color always comes from the same underlying Camelot wheel position -- only the\n"
                + "printed label changes."
            color: Theme.textMuted
            font.pointSize: Theme.fontSmall
        }

        Subtitle {
            text: "Streaming tracks"
            Layout.topMargin: 12
        }
        CheckBox {
            text: "Hide tracks from streaming services"
            checked: root.appSettingsController.hideStreamingTracks
            onToggled: root.appSettingsController.hideStreamingTracks = checked
        }
        Label {
            text: "Streaming-linked tracks (e.g. TIDAL, via Engine DJ) have no local file on the stick:\n"
                + "Seabass never plays, merges, syncs, or cleans them up regardless of this setting. This\n"
                + "only controls whether they show up in Browse Library at all."
            color: Theme.textMuted
            font.pointSize: Theme.fontSmall
        }

        // Absent entirely in a build compiled with SEABASS_EXPERIMENTAL
        // off -- see docs/experimental-features.md and
        // AppSettingsController::experimentalBuildSupported()'s own doc
        // comment for why that's a real "stable-only build" rather than
        // just a hidden toggle.
        ColumnLayout {
            visible: root.appSettingsController.experimentalBuildSupported
            Layout.topMargin: 12
            Layout.fillWidth: true
            spacing: 8

            Subtitle {
                text: "Experimental features"
            }

            // Always visible in this section, not just when checked --
            // read the caution before opting in, same idea as
            // RekordboxRunningWarning.qml but warning- not danger-toned.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: warningRow.implicitHeight + 16
                color: Theme.warnBg
                border.color: Theme.warnBorder
                radius: 4

                RowLayout {
                    id: warningRow
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    Label {
                        text: "⚠"
                        font.family: "Noto Sans Symbols2"
                        font.pointSize: Theme.fontMedium
                        color: Theme.warnIcon
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.warnText
                        text: "Experimental features are newer, less-tested parts of Seabass; they may be "
                            + "unstable, incomplete, or change without notice."
                    }
                }
            }

            CheckBox {
                text: "Enable experimental features"
                checked: root.appSettingsController.experimentalFeaturesEnabled
                onToggled: root.appSettingsController.experimentalFeaturesEnabled = checked
            }

            // Same gating ActionCard.qml uses for an experimental
            // feature (visible: !experimental || experimentalFeaturesEnabled)
            // -- hidden unless the toggle above is actually on, not just
            // whether this is an experimental-capable build. A plain
            // option here rather than an ActionCard on the main screen:
            // this is a maintainer/power-user tool (regenerating the
            // project's own test fixture, or submitting a library to
            // help test hardware Sebas doesn't have), not a per-stick
            // everyday action -- see docs/testing.md.
            Button {
                visible: root.appSettingsController.experimentalFeaturesEnabled
                Layout.topMargin: 8
                text: "Export Anonymized Library for Testing…"
                onClicked: root.anonymizeLibraryRequested()
            }
        }
    }
}
