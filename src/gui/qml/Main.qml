import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DjConvertGui

ApplicationWindow {
    id: window
    width: 1000
    height: 700
    visible: true
    title: "djconvert"

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
            onBrowseRequested: (stickLabel, format, path) => stackView.push(scanPageComponent, {
                stickLabel: stickLabel,
                format: format,
                path: path,
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
        DuplicatesPage {}
    }
}
