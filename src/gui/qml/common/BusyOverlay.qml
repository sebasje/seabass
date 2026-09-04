import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// A big, centered "this is busy" overlay for a page's content area --
// used in place of the small progress bar that used to live tucked into
// the header's toolbar. Every action that drives `busy` on these pages
// already disables the header's own buttons while busy, so the rest of
// the page underneath is genuinely unusable for the duration anyway; a
// small corner indicator was easy to miss for exactly the state where the
// user is most likely to be waiting and wondering if anything's happening.
// Deliberately does not cover the header itself (only anchors.fill's the
// content area it's placed in) -- the header's own disabled-but-visible
// controls (search, format toggle, "Back") already communicate that
// state, and covering them too would hide the one thing (Back, if
// enabled) still meaningful mid-write.
Item {
    id: root
    required property bool busy
    property int current: 0
    property int total: 0
    property string label: "Working..."

    visible: root.busy
    z: 1000

    // Estimated-remaining-time support. Deliberately withheld for the
    // first 10 seconds -- an ETA computed from only a couple of ticks'
    // worth of progress is noise, not a useful number, and would just
    // flicker/jump around for the many scans that finish in a couple of
    // seconds anyway.
    property real _startTimeMs: 0
    property real _nowMs: 0
    readonly property real _elapsedSeconds: root.busy ? (root._nowMs - root._startTimeMs) / 1000 : 0
    readonly property string etaText: {
        if (root._elapsedSeconds < 10 || root.total <= 0 || root.current <= 0 || root.current >= root.total) {
            return "";
        }
        var rate = root.current / root._elapsedSeconds;
        if (rate <= 0) return "";
        var remainingSeconds = Math.max(0, (root.total - root.current) / rate);
        if (remainingSeconds < 60) {
            return "~" + Math.round(remainingSeconds) + "s remaining";
        }
        var minutes = Math.floor(remainingSeconds / 60);
        var seconds = Math.round(remainingSeconds % 60);
        return "~" + minutes + "m " + seconds + "s remaining";
    }

    onBusyChanged: {
        if (root.busy) {
            root._startTimeMs = Date.now();
            root._nowMs = root._startTimeMs;
            etaTimer.restart();
        } else {
            etaTimer.stop();
        }
    }

    Timer {
        id: etaTimer
        interval: 1000
        repeat: true
        onTriggered: root._nowMs = Date.now()
    }

    // Dims rather than fully hides -- makes clear the same content is
    // still there and will return, not that the page has been replaced.
    Rectangle {
        anchors.fill: parent
        color: Theme.background
        opacity: 0.72
    }

    MouseArea {
        // Absorbs clicks/hover so nothing scrollable/clickable underneath
        // reacts while this is up, even for controls that don't otherwise
        // bind their own `enabled` to busy.
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        onClicked: {}
    }

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 12

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: root.label
            font.bold: true
            font.pointSize: Theme.fontLarge
        }
        ProgressBar {
            id: progressBar
            Layout.preferredWidth: 320
            Layout.preferredHeight: 16
            Layout.alignment: Qt.AlignHCenter
            indeterminate: root.total === 0
            value: root.total > 0 ? root.current / root.total : 0

            // Custom track/fill, not just a taller ProgressBar -- Material's
            // own background/contentItem delegates are fixed-height (4px)
            // internally and don't scale up just because the control's own
            // height does, so a plain height override alone stays thin.
            background: Rectangle {
                implicitHeight: 16
                radius: height / 2
                color: Theme.surface
                border.color: Theme.borderSubtle
            }
            contentItem: Item {
                implicitHeight: 16
                clip: true

                Rectangle {
                    id: fill
                    visible: !progressBar.indeterminate
                    height: parent.height
                    width: progressBar.visualPosition * parent.width
                    radius: height / 2
                    color: Theme.accent
                }

                // A simple back-and-forth sweep -- ProgressBar's own
                // indeterminate visualPosition isn't meaningful to bind to,
                // so this animates a fixed-width segment across the track
                // directly instead.
                Rectangle {
                    visible: progressBar.indeterminate
                    width: parent.width * 0.3
                    height: parent.height
                    radius: height / 2
                    color: Theme.accent

                    SequentialAnimation on x {
                        running: progressBar.indeterminate
                        loops: Animation.Infinite
                        NumberAnimation { from: -parent.width * 0.3; to: parent.width; duration: 1100; easing.type: Easing.InOutQuad }
                    }
                }
            }
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            visible: root.total > 0
            text: root.current + " / " + root.total
            color: Theme.textMuted
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            visible: root.etaText.length > 0
            text: root.etaText
            color: Theme.textMuted
            font.pointSize: Theme.fontSmall
        }
    }
}
