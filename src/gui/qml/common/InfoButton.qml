import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import SeabassGui

// A small "?" button that opens a popup with an explanation, in place of
// pointing someone at a file in the source tree they don't have (e.g. a
// docs/*.md) -- anything a person running the built app needs to
// understand should be explained inside the app itself.
ToolButton {
    id: root
    required property string explanationTitle
    required property string explanationText

    text: "?"
    implicitWidth: 28
    implicitHeight: 28

    ToolTip.visible: hovered
    ToolTip.text: "More info"

    onClicked: popup.open()

    Popup {
        id: popup

        // Centred on the button, then pulled back inside the window if
        // that would push it past an edge. This button is usually the
        // last thing on a header row, i.e. hard against the window's
        // right edge, where centring alone put most of the text outside
        // the window -- silently clipped, with no scrollbar and no hint
        // that anything was missing.
        readonly property int edgeMargin: 8

        // Positioned imperatively when it is about to show, not by a
        // binding on the window's size.
        //
        // Two earlier attempts bound x/width to root.Window.width and
        // then to Overlay.overlay.width. Both are null while the Popup
        // is being constructed, and neither notifies when it later
        // becomes real, so the binding kept its first value: the
        // arithmetic stayed NaN, Qt ignored it, and the popup went on
        // running off the right edge exactly as before. Both times it
        // looked fixed in the source and was not -- caught on a
        // screenshot, not by reading it. By onAboutToShow the popup has
        // a window, so the numbers are simply there to be read.
        function placeInsideWindow() {
            var overlay = Overlay.overlay;
            var w = overlay ? overlay.width : (root.Window.width || 0);
            var h = overlay ? overlay.height : (root.Window.height || 0);
            if (w > 0) {
                width = Math.min(420, w - 2 * edgeMargin);
            }
            var centered = Math.round((root.width - width) / 2);
            var sceneLeft = root.mapToItem(null, centered, 0).x;
            if (w > 0) {
                var maxLeft = w - width - edgeMargin;
                if (sceneLeft > maxLeft) {
                    centered -= (sceneLeft - maxLeft);
                    sceneLeft = maxLeft;
                }
                if (sceneLeft < edgeMargin) {
                    centered += (edgeMargin - sceneLeft);
                }
            }
            x = centered;
            var below = root.mapToItem(null, 0, root.height).y;
            maxHeight = h > 0 ? Math.max(120, h - below - edgeMargin) : 600;
        }

        property real maxHeight: 600
        onAboutToShow: placeInsideWindow()

        y: root.height

        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surface
            border.color: Theme.textMuted
            radius: 6
        }

        // Everything here addresses its siblings by id, never through
        // parent/parent.parent: the Popup's parent chain does not exist
        // yet while its contentItem is being constructed, and reaching
        // through it made object creation fail -- which main.cpp turns
        // into a silent exit(-1), no window and no QML error printed.
        contentItem: ScrollView {
            id: scroller
            clip: true
            contentWidth: availableWidth
            implicitHeight: Math.min(column.implicitHeight, popup.maxHeight)
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                id: column
                width: scroller.availableWidth
                spacing: 8

                Label {
                    text: root.explanationTitle
                    font.bold: true
                    font.pointSize: Theme.fontMedium
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    text: root.explanationText
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
    }
}
