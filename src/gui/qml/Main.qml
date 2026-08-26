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

    Material.theme: Material.Dark
    Material.accent: Material.DeepOrange
    Material.primary: Material.Grey

    MediaController {
        id: mediaCtrl
    }

    PlaybackController {
        id: playbackCtrl
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

    Component {
        id: stickListPageComponent
        StickListPage {
            mediaController: mediaCtrl
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
}
