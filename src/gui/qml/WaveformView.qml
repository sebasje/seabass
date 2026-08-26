import QtQuick

// Draws a waveform (as normalized 0..1 amplitude bars) with cue markers
// overlaid at their real position. Used both by the live PlayerBar (with
// playhead progress and click-to-seek) and by static per-track previews
// like the Duplicates page, where progress/seeking aren't relevant.
Canvas {
    id: root
    property var waveformData: []
    property var cueData: []
    property real trackDurationMs: 0
    // -1 disables the played/unplayed color split (a static preview).
    property real progress: -1

    signal seekRequested(real ratio)

    onWaveformDataChanged: requestPaint()
    onCueDataChanged: requestPaint()
    onProgressChanged: requestPaint()
    onTrackDurationMsChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

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
                var played = root.progress >= 0 && (i / wf.length) < root.progress;
                ctx.fillStyle = played ? "#6cb6ff" : "#556070";
                ctx.fillRect(i * barW, (h - barH) / 2, Math.max(1, barW - 1), barH);
            }
        }

        if (cueData && trackDurationMs > 0) {
            for (var j = 0; j < cueData.length; j++) {
                var cue = cueData[j];
                var x = (cue.positionMs / trackDurationMs) * w;
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
        enabled: root.trackDurationMs > 0
        onClicked: (mouse) => root.seekRequested(mouse.x / width)
    }
}
