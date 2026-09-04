import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

Page {
    id: root
    required property var appSettingsController
    // Scaled by Theme.iconScale, not a literal pixel count -- same
    // pt-to-px conversion KeyBadge.qml uses for its own sizing, so this
    // tracks the system font size (and therefore stays sharp on a retina
    // display, which is just a higher effective DPI) instead of a fixed
    // pixel count that only looks right at today's default font size.
    readonly property real settingIndent: 20 * Theme.iconScale

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
            BackBreadcrumb {
                title: "Preferences"
                onHomeRequested: root.StackView.view.pop(null)
            }
            Item { Layout.fillWidth: true }
        }
    }

    // Group header flush left, then one option per row indented beneath
    // it -- a two-column grid (label column vs. field column) has to
    // align every row's label against every other row's, which falls
    // apart the moment one field needs more than one control (the key
    // notation row's two radios plus a preview badge next to a plain
    // single checkbox). A header-then-indented-rows shape has no shared
    // axis to keep aligned: every group is independent.
    ColumnLayout {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 24 * Theme.iconScale
        spacing: 24 * Theme.iconScale

        ColumnLayout {
            spacing: 6 * Theme.iconScale
            Subtitle { text: "Theme" }
            ButtonGroup { id: themeGroup }
            RadioButton {
                Layout.leftMargin: root.settingIndent
                text: "Dark"
                ButtonGroup.group: themeGroup
                checked: !root.appSettingsController.useSystemTheme
                onCheckedChanged: if (checked) root.appSettingsController.useSystemTheme = false
            }
            RadioButton {
                Layout.leftMargin: root.settingIndent
                text: "Match System Theme"
                ButtonGroup.group: themeGroup
                checked: root.appSettingsController.useSystemTheme
                onCheckedChanged: if (checked) root.appSettingsController.useSystemTheme = true
            }
        }

        ColumnLayout {
            spacing: 6 * Theme.iconScale
            // Explanation lives in a tooltip (hover either radio button)
            // rather than as a permanent line of text below -- it's
            // background detail worth having on hand, not something that
            // needs to compete for space with the setting itself.
            readonly property string keyNotationExplanation:
                "Applies to every key badge in Browse Library and Library Statistics. Either way, the "
                + "badge's color always comes from the same underlying Camelot wheel position; only the "
                + "printed label changes."
            Subtitle { text: "Musical key notation" }
            ButtonGroup { id: keyNotationGroup }
            RadioButton {
                Layout.leftMargin: root.settingIndent
                text: "Camelot (e.g. 6A)"
                ButtonGroup.group: keyNotationGroup
                checked: root.appSettingsController.keyNotation !== "traditional"
                onCheckedChanged: if (checked) root.appSettingsController.keyNotation = "camelot"
                ToolTip.visible: hovered
                ToolTip.text: parent.keyNotationExplanation
            }
            RadioButton {
                Layout.leftMargin: root.settingIndent
                text: "Traditional (e.g. F♯m)"
                ButtonGroup.group: keyNotationGroup
                checked: root.appSettingsController.keyNotation === "traditional"
                onCheckedChanged: if (checked) root.appSettingsController.keyNotation = "traditional"
                ToolTip.visible: hovered
                ToolTip.text: parent.keyNotationExplanation
            }
            // Live preview, not just a description -- the two notations
            // read differently enough (a color-coded wheel position vs.
            // an actual note name) that seeing one example update as you
            // switch is clearer than describing the difference in text.
            RowLayout {
                Layout.leftMargin: root.settingIndent
                spacing: 8 * Theme.iconScale
                Label { text: "Preview:"; color: Theme.textMuted }
                KeyBadge {
                    keyName: "F#m"
                    notation: root.appSettingsController.keyNotation
                }
            }
        }

        ColumnLayout {
            spacing: 6 * Theme.iconScale
            Subtitle { text: "Streaming tracks" }
            CheckBox {
                Layout.leftMargin: root.settingIndent
                text: "Hide tracks from streaming services"
                checked: root.appSettingsController.hideStreamingTracks
                onToggled: root.appSettingsController.hideStreamingTracks = checked
                ToolTip.visible: hovered
                ToolTip.text: "Streaming-linked tracks (e.g. TIDAL, via Engine DJ) have no local file on the "
                    + "stick: Seabass never plays, merges, syncs, or cleans them up regardless of this "
                    + "setting. This only controls whether they show up in Browse Library at all."
            }
        }

        // Absent entirely in a build compiled with SEABASS_EXPERIMENTAL
        // off -- see docs/experimental-features.md and
        // AppSettingsController::experimentalBuildSupported()'s own doc
        // comment for why that's a real "stable-only build" rather than
        // just a hidden toggle.
        ColumnLayout {
            visible: root.appSettingsController.experimentalBuildSupported
            spacing: 6 * Theme.iconScale
            Subtitle { text: "Experimental features" }
            CheckBox {
                Layout.leftMargin: root.settingIndent
                text: "Enable experimental features"
                checked: root.appSettingsController.experimentalFeaturesEnabled
                onToggled: root.appSettingsController.experimentalFeaturesEnabled = checked
                ToolTip.visible: hovered
                ToolTip.text: "Experimental features are newer, less-tested parts of Seabass; they may be "
                    + "unstable, incomplete, or change without notice."
            }

            // Same gating ActionCard.qml uses for an experimental feature
            // (visible: !experimental || experimentalFeaturesEnabled) --
            // hidden unless the toggle above is actually on, not just
            // whether this is an experimental-capable build. A plain
            // option here rather than an ActionCard on the main screen:
            // this is a maintainer/power-user tool (regenerating the
            // project's own test fixture, or submitting a library to
            // help test hardware Sebas doesn't have), not a per-stick
            // everyday action -- see docs/testing.md.
            Button {
                Layout.leftMargin: root.settingIndent
                visible: root.appSettingsController.experimentalFeaturesEnabled
                text: "Export Anonymized Library for Testing…"
                onClicked: root.anonymizeLibraryRequested()
            }
        }
    }
}
