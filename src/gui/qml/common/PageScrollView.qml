import QtQuick
import QtQuick.Controls
import SeabassGui

// The scrollable container every full-page form uses (padding, the big
// scroll bar, content as wide as the view), with two rules on top: it is
// only draggable/flickable when its content actually overflows (a page
// that fits was still grabbable and would slide around under the mouse
// with nowhere to go), and BigScrollBar is paired with a plain Flickable
// rather than a Control-styled ScrollView.
//
// The Flickable choice isn't cosmetic -- see StickStatisticsPage.qml's and
// SyncPage.qml's own comments on the same fix. A platform style (KDE's
// org.kde.desktop on Linux; FluentWinUI3 on Windows, which has no
// ScrollView.qml of its own and falls back to Basic's) knows how to
// position a ScrollView's *own* default scrollbar, but doesn't know how
// to place a foreign BigScrollBar assigned over it -- it rendered
// unanchored, cramped in a corner instead of docked to the right edge and
// spanning the full height. Every other scrollable page in this app
// already avoids ScrollView for exactly this reason; this file used to be
// the one holdout, and the Full Stick Backup / Restore pages (which use
// it) were where the bug showed.
Flickable {
    id: root
    property real padding: 0
    default property alias content: contentContainer.data

    contentWidth: width
    contentHeight: contentContainer.childrenRect.height + 2 * root.padding
    // The `+ 1` matches the fudge every other Flickable in this app uses
    // for the same "content that just barely fits shouldn't still drag"
    // check -- without it, a page sized to the exact pixel flickers
    // between interactive and not.
    interactive: contentHeight > height + 1
    clip: true
    ScrollBar.vertical: BigScrollBar {}

    Item {
        id: contentContainer
        x: root.padding
        y: root.padding
        width: root.width - 2 * root.padding
    }
}
