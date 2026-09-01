import QtQuick
import QtQuick.Shapes
import SeabassGui

// Flat, monochrome device pictogram -- drawn with plain Rectangles/Shape
// fills rather than a font glyph or emoji, so it's guaranteed to render
// as a single flat color with no font-fallback surprises (see the
// mount/unmount ToolButton's font.family fix for what that risk looks
// like in practice).
Item {
    id: root
    property color color: Theme.textMuted
    // BRAINSTORM.md: "show different icon for an SD card for improved
    // clarity" -- best-effort, see DetectedStick::isSdCard's own comment
    // for what "best-effort" means per platform.
    property bool isSdCard: false
    implicitWidth: 32
    implicitHeight: 32

    // USB-stick pictogram: a rounded body plus a small connector prong.
    Item {
        anchors.fill: parent
        visible: !root.isSdCard
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

    // SD-card pictogram: the standard card silhouette (a cut top-right
    // corner, held contacts-down) is what actually reads as "SD card"
    // at a glance rather than just "a smaller rectangle" -- not
    // achievable with axis-aligned Rectangles alone, hence the one
    // Shape in this app's whole icon set.
    Shape {
        anchors.fill: parent
        visible: root.isSdCard
        ShapePath {
            fillColor: root.color
            strokeColor: "transparent"
            startX: root.width * 0.14; startY: root.height * 0.08
            PathLine { x: root.width * 0.72; y: root.height * 0.08 }
            PathLine { x: root.width * 0.86; y: root.height * 0.24 }
            PathLine { x: root.width * 0.86; y: root.height * 0.92 }
            PathLine { x: root.width * 0.14; y: root.height * 0.92 }
            PathLine { x: root.width * 0.14; y: root.height * 0.08 }
        }
    }
}
