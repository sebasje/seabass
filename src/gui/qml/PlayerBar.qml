import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import DjConvertGui

Frame {
    id: root
    required property var controller

    function formatTime(ms) {
        var totalSec = Math.max(0, Math.floor(ms / 1000));
        var m = Math.floor(totalSec / 60);
        var s = totalSec % 60;
        return m + ":" + (s < 10 ? "0" : "") + s;
    }

    RowLayout {
        anchors.fill: parent
        spacing: 12

        // A big, backlit-looking transport button, styled after a hardware
        // DJ controller's play button rather than a flat UI button.
        Rectangle {
            id: playButton
            Layout.preferredWidth: 64
            Layout.preferredHeight: 64
            radius: width / 2
            color: playMouseArea.pressed ? Qt.darker(Material.accent, 1.4) : Material.accent
            border.color: "#1a1a1a"
            border.width: 2

            Text {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: root.controller.playing ? 0 : 2
                text: root.controller.playing ? "⏸" : "▶"
                font.pixelSize: 26
                color: "white"
            }

            MouseArea {
                id: playMouseArea
                anchors.fill: parent
                onClicked: root.controller.togglePlay()
            }
        }

        Image {
            Layout.preferredWidth: 64
            Layout.preferredHeight: 64
            fillMode: Image.PreserveAspectFit
            visible: root.controller.artworkPath.length > 0
            source: root.controller.artworkPath.length > 0 ? "file://" + root.controller.artworkPath : ""
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: root.controller.title + (root.controller.artist.length > 0 ? "  -- " + root.controller.artist : "")
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    text: root.formatTime(root.controller.position) + " / " + root.formatTime(root.controller.duration)
                    color: "gray"
                }
            }

            Label {
                visible: root.controller.errorMessage.length > 0
                text: root.controller.errorMessage
                color: "red"
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            WaveformView {
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                waveformData: root.controller.waveform
                cueData: root.controller.cues
                trackDurationMs: root.controller.duration
                progress: trackDurationMs > 0 ? root.controller.position / trackDurationMs : 0
                onSeekRequested: (ratio) => root.controller.seek(ratio * root.controller.duration)
            }
        }

        Button {
            text: "✕"
            onClicked: root.controller.stop()
        }
    }
}
