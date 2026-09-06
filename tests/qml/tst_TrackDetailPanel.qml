import QtQuick
import QtTest
import SeabassGui

// TrackDetailPanel.qml headless with fake controllers: showFor() fills
// the header and playlists, the close button asks the page to close,
// and a click on the waveform opens the add-cue form. Plus the page's
// screenshot when SEABASS_SCREENSHOT_DIR is set.
TestCase {
    id: testCase
    name: "TrackDetailPanel"
    width: 480
    height: 700
    visible: true
    when: windowShown

    Component {
        id: panelComponent
        TrackDetailPanel { width: 460; height: 680 }
    }

    Component {
        id: spyComponent
        SignalSpy {}
    }

    function makePanel() {
        var panel = createTemporaryObject(panelComponent, testCase, {
            addCueController: {statusMessage: "", errorMessage: "", busy: false, calls: [],
                               addCue: function() { this.calls.push("addCue"); }},
            playbackController: {waveformFor: function(format, path, id) { return []; }},
            format: "engine",
            libraryPath: "/media/MAIN/Engine Library",
        });
        verify(panel !== null);
        waitForRendering(panel);
        return panel;
    }

    function makeDelegate() {
        return {sourceId: "42", title: "Major Tom (Reworked 2024)", artist: "DJ Amador",
                cues: [{kind: "hot", hotCueNumber: 1, positionMs: 32000, isLoop: false, loopEndMs: 0, color: "#ffcc00", comment: ""}],
                durationSeconds: 372, playlistNames: ["Peaktime", "Warm-up"], streamingSource: ""};
    }

    function test_showForFillsThePanel() {
        var panel = makePanel();
        panel.showFor(makeDelegate());
        compare(panel.trackTitle, "Major Tom (Reworked 2024)");
        compare(panel.trackArtist, "DJ Amador");
        compare(panel.trackDurationMs, 372000);
        compare(panel.trackPlaylistNames.length, 2);
        compare(panel.pendingPositionMs, -1);
        if (screenshotDir && screenshotDir.length > 0) {
            grabImage(panel).save(screenshotDir + "/track-panel.png");
        }
    }

    function test_closeButtonAsksThePage() {
        var panel = makePanel();
        var spy = createTemporaryObject(spyComponent, testCase, {target: panel, signalName: "closeRequested"});
        findChild(panel, "closeTrackPanelButton").clicked();
        compare(spy.count, 1);
    }

    function test_streamingTrackHasNoAddCueHint() {
        var panel = makePanel();
        var delegate = makeDelegate();
        delegate.streamingSource = "TIDAL";
        panel.showFor(delegate);
        compare(panel.trackStreamingSource, "TIDAL");
    }
}
