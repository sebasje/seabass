import QtQuick
import DjConvertGui

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
            ctx.fillStyle = String(Theme.textMuted);
            ctx.fillRect(0, h / 2 - 1, w, 2);
        } else {
            var barW = w / wf.length;
            for (var i = 0; i < wf.length; i++) {
                var col = wf[i];
                // Backwards-compatible with a plain 0..1 number (no band
                // split available) as well as a {low, mid, high} column.
                var low = typeof col === "number" ? col : col.low;
                var mid = typeof col === "number" ? col : col.mid;
                var high = typeof col === "number" ? col : col.high;
                var amplitude = Math.max(low, mid, high);
                var barH = Math.max(1, amplitude * h);
                var played = root.progress >= 0 && (i / wf.length) < root.progress;

                // Classic DJ-hardware coloring: bass in blue, mids in
                // green/yellow, highs in red/white, blended by each band's
                // relative strength rather than a single flat hue.
                var r = Math.min(255, Math.round(60 + high * 195));
                var g = Math.min(255, Math.round(60 + mid * 150 + high * 60));
                var b = Math.min(255, Math.round(90 + low * 165));
                var alpha = played ? 1.0 : 0.62;
                ctx.fillStyle = "rgba(" + r + "," + g + "," + b + "," + alpha + ")";
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
