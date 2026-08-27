import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import DjConvertGui

ApplicationWindow {
    id: window
    width: 1100
    height: 720
    visible: true
    title: "djconvert"

    // "seabass" palette (see the app icon/watermark's source artifact):
    // Current is the accent, Abyss is the app-bar primary -- both fixed
    // brand colors, deliberately not theme-dependent (see Theme.qml).
    Material.theme: appSettingsCtrl.useSystemTheme ? Material.System : Material.Dark
    Material.accent: Theme.accent
    Material.primary: Theme.primary
    color: Theme.background

    MediaController {
        id: mediaCtrl
    }

    PlaybackController {
        id: playbackCtrl
    }

    AppSettingsController {
        id: appSettingsCtrl
    }

    RekordboxGuardController {
        id: rekordboxGuardCtrl
    }

    // Pushes the resolved Material colors + the theme toggle into the
    // Theme singleton -- a pure-QML singleton has no place in the visual
    // tree of its own, so it can't read the Material attached properties
    // itself (they're resolved relative to an Item's ancestors); `window`
    // here is the one Item that actually has Material.theme set, so its
    // resolved values are correct and just get copied across as data.
    Binding { target: Theme; property: "useSystemTheme"; value: appSettingsCtrl.useSystemTheme }
    Binding { target: Theme; property: "materialBackground"; value: window.Material.background }
    Binding { target: Theme; property: "materialForeground"; value: window.Material.foreground }
    Binding { target: Theme; property: "materialDivider"; value: window.Material.dividerColor }

    // Explicit, guaranteed background fill -- the Material style's own
    // window-background handling doesn't reliably respect a plain
    // `color:` on ApplicationWindow (observed: it kept rendering the
    // Qt default white regardless), so paint it ourselves instead of
    // depending on that interaction.
    Rectangle {
        anchors.fill: parent
        color: Theme.background
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RekordboxRunningWarning {
            visible: rekordboxGuardCtrl.rekordboxRunning
        }

        StackView {
            id: stackView
            Layout.fillWidth: true
            Layout.fillHeight: true
            initialItem: stickListPageComponent
        }

        PlayerBar {
            Layout.fillWidth: true
            visible: playbackCtrl.hasTrack
            controller: playbackCtrl
        }
    }

    // Large background watermark -- the "Sound Bass" app mark, anchored to
    // the bottom-right corner. Sits on top of the content (so it's visible
    // regardless of which page's opaque background is underneath) but at
    // low opacity and with no mouse handling of its own, so it never
    // competes with or blocks the real UI.
    Image {
        source: "qrc:/qt/qml/DjConvertGui/qml/icons/seabass_soundbass.svg"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: -width * 0.08
        width: Math.min(window.width, window.height) * 0.75
        height: width
        opacity: 0.10
        fillMode: Image.PreserveAspectFit
        smooth: true
    }

    Component {
        id: stickListPageComponent
        StickListPage {
            mediaController: mediaCtrl
            playbackController: playbackCtrl
            appSettingsController: appSettingsCtrl
            onBrowseRequested: (stickLabel, rekordboxPath, enginePath) => stackView.push(scanPageComponent, {
                stickLabel: stickLabel,
                rekordboxPath: rekordboxPath,
                enginePath: enginePath,
            })
            onDuplicatesRequested: (stickLabel, rekordboxPath, enginePath) => stackView.push(duplicatesPageComponent, {
                stickLabel: stickLabel,
                rekordboxPath: rekordboxPath,
                enginePath: enginePath,
            })
            onSettingsRequested: (stickLabel, pioneerRoot) => stackView.push(settingsPageComponent, {
                stickLabel: stickLabel,
                pioneerRoot: pioneerRoot,
            })
            onSyncRequested: (stickLabel, rekordboxPath, enginePath) => stackView.push(syncPageComponent, {
                stickLabel: stickLabel,
                rekordboxPath: rekordboxPath,
                enginePath: enginePath,
            })
            onAppSettingsRequested: stackView.push(appSettingsPageComponent)
            onBackupsRequested: (stickLabel, rekordboxPath, enginePath) => stackView.push(backupsPageComponent, {
                stickLabel: stickLabel,
                rekordboxPath: rekordboxPath,
                enginePath: enginePath,
            })
            onLocalCueRequested: (stickLabel, rekordboxPath, enginePath) => stackView.push(localCuePageComponent, {
                stickLabel: stickLabel,
                rekordboxPath: rekordboxPath,
                enginePath: enginePath,
            })
            onAboutRequested: stackView.push(aboutPageComponent)
        }
    }

    Component {
        id: scanPageComponent
        ScanPage {
            playbackController: playbackCtrl
            appSettingsController: appSettingsCtrl
        }
    }

    Component {
        id: duplicatesPageComponent
        DuplicatesPage {
            playbackController: playbackCtrl
            appSettingsController: appSettingsCtrl
        }
    }

    Component {
        id: settingsPageComponent
        SettingsPage {}
    }

    Component {
        id: syncPageComponent
        SyncPage {
            playbackController: playbackCtrl
        }
    }

    Component {
        id: appSettingsPageComponent
        AppSettingsPage {
            appSettingsController: appSettingsCtrl
        }
    }

    Component {
        id: backupsPageComponent
        BackupsPage {}
    }

    Component {
        id: localCuePageComponent
        LocalCuePage {
            appSettingsController: appSettingsCtrl
        }
    }

    Component {
        id: aboutPageComponent
        AboutPage {}
    }
}
