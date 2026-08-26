import QtQuick
import QtQuick.Controls
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

    StackView {
        id: stackView
        anchors.fill: parent
        initialItem: stickListPageComponent
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
        ScanPage {}
    }

    Component {
        id: duplicatesPageComponent
        DuplicatesPage {}
    }
}
