import QtQuick
import QtQuick.Controls
import SeabassGui

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
    // "rekordbox"/"engine"/"onelibrary" -- which catalog this track came
    // from, only used to decide whether the empty-waveform tooltip below
    // applies. Optional: callers that don't care about that tooltip (e.g.
    // PlayerBar, where a track is always actually loaded) can leave it
    // unset.
    property string format: ""

    signal seekRequested(real ratio)
    // Fires on every click alongside seekRequested -- callers that only
    // want click-to-seek (PlayerBar) simply don't connect to this one.
    // Added for the "Add Cue" picker, which needs an absolute ms position
    // rather than a 0..1 ratio.
    signal positionClicked(real positionMs)

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

        // Playhead -- previously only implied by the played/unplayed
        // color split above, which reads as "which bars are done" rather
        // than "exactly where is 'now'", especially at the 0.35 opacity
        // this view runs at as the library row backdrop. A white halo
        // under an accent-colored core line keeps it legible against
        // whatever mixed bar colors happen to sit behind it; the
        // downward-pointing triangle at the top gives it a distinct
        // silhouette even where the halo/line alone would blend in.
        if (root.progress >= 0 && w > 0) {
            var px = root.progress * w;
            ctx.save();
            ctx.shadowColor = "rgba(255,255,255,0.85)";
            ctx.shadowBlur = 6;
            ctx.strokeStyle = "#ffffff";
            ctx.lineWidth = 3;
            ctx.beginPath();
            ctx.moveTo(px, 0);
            ctx.lineTo(px, h);
            ctx.stroke();
            ctx.restore();

            ctx.strokeStyle = String(Theme.accent);
            ctx.lineWidth = 1.5;
            ctx.beginPath();
            ctx.moveTo(px, 0);
            ctx.lineTo(px, h);
            ctx.stroke();

            ctx.fillStyle = String(Theme.accent);
            ctx.beginPath();
            ctx.moveTo(px - 5, 0);
            ctx.lineTo(px + 5, 0);
            ctx.lineTo(px, 7);
            ctx.closePath();
            ctx.fill();
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.trackDurationMs > 0
        hoverEnabled: root.format === "engine" && (!root.waveformData || root.waveformData.length === 0)
        // Engine only generates a track's waveform preview the first time
        // Engine OS itself loads that track -- a track never opened on
        // real hardware yet has no preview to show here (not a bug), so
        // say so instead of leaving the flat placeholder line unexplained.
        ToolTip.visible: hoverEnabled && containsMouse
        ToolTip.text: "No waveform yet -- Engine OS generates this the first time the track is loaded on the hardware."
        onClicked: (mouse) => {
            root.seekRequested(mouse.x / width);
            root.positionClicked((mouse.x / width) * root.trackDurationMs);
        }
    }
}
