import QtQuick
import QtQuick.Controls
import SeabassGui

// The ScrollView every full-page form uses (margins, the big scroll bar,
// content as wide as the view), with one rule on top: it is only
// draggable or flickable when its content actually overflows. A page
// that fits was still grabbable and would slide around under the mouse
// with nowhere to go.
ScrollView {
    id: scroll
    contentWidth: availableWidth
    ScrollBar.vertical: BigScrollBar {}

    // ScrollView's contentItem is the Flickable it wraps the content in;
    // its `interactive` is what gates dragging and wheel flicks.
    Component.onCompleted: {
        if (scroll.contentItem && scroll.contentItem.hasOwnProperty("interactive")) {
            scroll.contentItem.interactive = Qt.binding(function() {
                return scroll.contentHeight > scroll.height + 1 || scroll.contentWidth > scroll.width + 1;
            });
        }
    }
}
