import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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

        Button {
            text: root.controller.playing ? "⏸" : "▶"
            onClicked: root.controller.togglePlay()
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

            Canvas {
                id: waveformCanvas
                Layout.fillWidth: true
                Layout.preferredHeight: 48

                property var waveformData: root.controller.waveform
                property real progress: root.controller.duration > 0
                    ? root.controller.position / root.controller.duration : 0

                onWaveformDataChanged: requestPaint()
                onProgressChanged: requestPaint()
                onWidthChanged: requestPaint()

                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    var w = width, h = height;
                    var wf = waveformData;

                    if (!wf || wf.length === 0) {
                        ctx.fillStyle = "#666";
                        ctx.fillRect(0, h / 2 - 1, w, 2);
                        return;
                    }

                    var barW = w / wf.length;
                    for (var i = 0; i < wf.length; i++) {
                        var barH = Math.max(1, wf[i] * h);
                        var played = (i / wf.length) < progress;
                        ctx.fillStyle = played ? "#6cb6ff" : "#556070";
                        ctx.fillRect(i * barW, (h - barH) / 2, Math.max(1, barW - 1), barH);
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: (mouse) => {
                        if (root.controller.duration > 0) {
                            root.controller.seek((mouse.x / width) * root.controller.duration);
                        }
                    }
                }
            }
        }

        Button {
            text: "✕"
            onClicked: root.controller.stop()
        }
    }
}
