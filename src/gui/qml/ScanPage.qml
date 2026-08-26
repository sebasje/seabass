import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DjConvertGui

Page {
    id: root
    required property string format
    required property string path
    required property string stickLabel
    required property var playbackController

    ScanController {
        id: scanController
    }

    Component.onCompleted: scanController.scan(root.format, root.path)

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            ToolButton {
                text: "< Back"
                onClicked: root.StackView.view.pop()
            }
            Label {
                text: root.stickLabel + " -- " + (root.format === "rekordbox" ? "rekordbox" : "Engine") + " library"
                font.bold: true
                font.pointSize: 14
            }
            Item { Layout.fillWidth: true }
            BusyIndicator {
                running: scanController.busy
                visible: scanController.busy
                implicitWidth: 24
                implicitHeight: 24
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        Label {
            visible: scanController.errorMessage.length > 0
            text: scanController.errorMessage
            color: "red"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: ""; Layout.preferredWidth: 32 }
            Label { text: "Title"; font.bold: true; Layout.preferredWidth: 260 }
            Label { text: "Artist"; font.bold: true; Layout.preferredWidth: 200 }
            Label { text: "Duration"; font.bold: true; Layout.preferredWidth: 80 }
            Label { text: "Cues"; font.bold: true; Layout.preferredWidth: 60 }
            Label { text: "Plays"; font.bold: true; Layout.preferredWidth: 60 }
        }

        ListView {
            id: trackListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: scanController.tracks

            delegate: Item {
                width: ListView.view.width
                height: 28
                required property string sourceId
                required property string title
                required property string artist
                required property double durationSeconds
                required property int cueCount
                required property int playCount
                required property string filePath

                RowLayout {
                    anchors.fill: parent
                    ToolButton {
                        text: "▶"
                        Layout.preferredWidth: 32
                        enabled: filePath.length > 0
                        onClicked: root.playbackController.load(root.format, root.path, sourceId, filePath, title, artist)
                    }
                    Label { text: title; Layout.preferredWidth: 260; elide: Text.ElideRight }
                    Label { text: artist; Layout.preferredWidth: 200; elide: Text.ElideRight }
                    Label {
                        Layout.preferredWidth: 80
                        text: {
                            var total = Math.round(durationSeconds);
                            var m = Math.floor(total / 60);
                            var s = total % 60;
                            return m + ":" + (s < 10 ? "0" : "") + s;
                        }
                    }
                    Label { text: cueCount; Layout.preferredWidth: 60 }
                    Label { text: playCount >= 0 ? playCount : "--"; Layout.preferredWidth: 60 }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: trackListView.count === 0 && !scanController.busy
                text: "No tracks found."
                color: "gray"
            }
        }

        Label {
            text: trackListView.count + " tracks"
            color: "gray"
        }
    }
}
