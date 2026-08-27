import QtQuick

// Flat, monochrome USB-stick pictogram (a rounded body plus a small
// connector prong) -- drawn with plain Rectangles rather than a font
// glyph or emoji, so it's guaranteed to render as a single flat color
// with no font-fallback surprises (see the mount/unmount ToolButton's
// font.family fix for what that risk looks like in practice).
Item {
    id: root
    property color color: "#94a1a8"
    implicitWidth: 32
    implicitHeight: 32

    Rectangle {
        width: parent.width * 0.30
        height: parent.height * 0.22
        radius: width * 0.2
        color: root.color
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
    }
    Rectangle {
        width: parent.width * 0.64
        height: parent.height * 0.7
        radius: width * 0.18
        color: root.color
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
    }
}
