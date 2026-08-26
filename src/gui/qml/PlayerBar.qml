import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
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

            Canvas {
                id: waveformCanvas
                Layout.fillWidth: true
                Layout.preferredHeight: 48

                property var waveformData: root.controller.waveform
                property var cueData: root.controller.cues
                property real trackDuration: root.controller.duration
                property real progress: trackDuration > 0 ? root.controller.position / trackDuration : 0

                onWaveformDataChanged: requestPaint()
                onCueDataChanged: requestPaint()
                onProgressChanged: requestPaint()
                onTrackDurationChanged: requestPaint()
                onWidthChanged: requestPaint()

                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    var w = width, h = height;
                    var wf = waveformData;

                    if (!wf || wf.length === 0) {
                        ctx.fillStyle = "#666";
                        ctx.fillRect(0, h / 2 - 1, w, 2);
                    } else {
                        var barW = w / wf.length;
                        for (var i = 0; i < wf.length; i++) {
                            var barH = Math.max(1, wf[i] * h);
                            var played = (i / wf.length) < progress;
                            ctx.fillStyle = played ? "#6cb6ff" : "#556070";
                            ctx.fillRect(i * barW, (h - barH) / 2, Math.max(1, barW - 1), barH);
                        }
                    }

                    if (cueData && trackDuration > 0) {
                        for (var j = 0; j < cueData.length; j++) {
                            var cue = cueData[j];
                            var x = (cue.positionMs / trackDuration) * w;
                            var color = (cue.color && cue.color.length > 0 && cue.color.charAt(0) === "#")
                                ? cue.color : "#ffcc00";
                            ctx.strokeStyle = color;
                            ctx.lineWidth = 2;
                            ctx.beginPath();
                            ctx.moveTo(x, 0);
                            ctx.lineTo(x, h);
                            ctx.stroke();
                            if (cue.kind === "hot") {
                                ctx.fillStyle = color;
                                ctx.fillRect(x, 0, 10, 10);
                                ctx.fillStyle = "#000";
                                ctx.font = "8px sans-serif";
                                ctx.fillText(String(cue.hotCueNumber), x + 2, 8);
                            }
                        }
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
