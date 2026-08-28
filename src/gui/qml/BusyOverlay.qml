import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DjConvertGui

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
    }
}
