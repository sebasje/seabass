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
    // Current is the accent, Abyss is the app-bar primary.
    readonly property color paletteCurrent: "#3daee9"
    readonly property color paletteAbyss: "#123a52"

    Material.theme: appSettingsCtrl.useSystemTheme ? Material.System : Material.Dark
    Material.accent: paletteCurrent
    Material.primary: paletteAbyss

    MediaController {
        id: mediaCtrl
    }

    PlaybackController {
        id: playbackCtrl
    }

    AppSettingsController {
        id: appSettingsCtrl
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

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
        width: Math.min(window.width, window.height) * 0.25
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
            onBrowseRequested: (stickLabel, format, path, siblingRekordboxPath) => stackView.push(scanPageComponent, {
                stickLabel: stickLabel,
                format: format,
                path: path,
                siblingRekordboxPath: siblingRekordboxPath,
            })
            onDuplicatesRequested: (stickLabel, format, path) => stackView.push(duplicatesPageComponent, {
                stickLabel: stickLabel,
                format: format,
                path: path,
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
        }
    }

    Component {
        id: scanPageComponent
        ScanPage {
            playbackController: playbackCtrl
        }
    }

    Component {
        id: duplicatesPageComponent
        DuplicatesPage {
            playbackController: playbackCtrl
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
}
