import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// Progress of a long file transfer (restore, clone): a strip of phase
// chips with the current one lit, a bar that is determinate only in the
// phases that have a byte total, then files / bytes / rate / ETA and the
// file being written. The page feeds it its controller's progress
// properties one by one so a plain JS stand-in works in tests.
Frame {
    id: frame

    property var phases: []              // phase ids in order, e.g. ["analyzing", "writing", "checking"]
    property string phase: ""            // the current one
    property var determinatePhases: []   // phases whose bytesTotal is meaningful; the bar sweeps in the others
    property var phaseLabel: function(id) { return id; }
    property double filesDone: 0
    property double filesTotal: 0
    property double bytesDone: 0
    property double bytesTotal: 0
    property double bytesPerSecond: 0
    property int etaSeconds: -1
    property string currentFile: ""
    property string cancelButtonObjectName: "cancelButton"
    property bool cancelEnabled: true
    signal cancelRequested()

    readonly property bool indeterminate: frame.determinatePhases.indexOf(frame.phase) < 0 || frame.bytesTotal <= 0

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: frame.phases
                delegate: Rectangle {
                    required property string modelData
                    readonly property bool current: modelData === frame.phase
                    radius: 3
                    color: current ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.15) : "transparent"
                    border.color: current ? Theme.accent : "transparent"
                    implicitWidth: phaseText.implicitWidth + 16
                    implicitHeight: phaseText.implicitHeight + 6
                    Label {
                        id: phaseText
                        anchors.centerIn: parent
                        text: frame.phaseLabel(parent.modelData)
                        color: parent.current ? Theme.accent : Theme.textMuted
                        font.bold: parent.current
                    }
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                objectName: frame.cancelButtonObjectName
                text: "Cancel"
                enabled: frame.cancelEnabled
                onClicked: frame.cancelRequested()
            }
        }
        ProgressBar {
            id: bar
            Layout.fillWidth: true
            Layout.preferredHeight: 16
            indeterminate: frame.indeterminate
            value: frame.bytesTotal > 0 ? frame.bytesDone / frame.bytesTotal : 0
            background: Rectangle { implicitHeight: 16; radius: 8; color: Theme.surface; border.color: Theme.borderSubtle }
            contentItem: Item {
                implicitHeight: 16
                clip: true
                Rectangle {
                    visible: !bar.indeterminate
                    height: parent.height
                    width: bar.visualPosition * parent.width
                    radius: 8
                    color: Theme.accent
                }
                Rectangle {
                    visible: bar.indeterminate
                    width: parent.width * 0.3
                    height: parent.height
                    radius: 8
                    color: Theme.accent
                    SequentialAnimation on x {
                        running: bar.indeterminate && bar.visible
                        loops: Animation.Infinite
                        NumberAnimation { from: -parent.width * 0.3; to: parent.width; duration: 1100; easing.type: Easing.InOutQuad }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                visible: frame.filesTotal > 0
                font.family: Theme.dataFamily
                text: frame.filesDone + " / " + frame.filesTotal + " files"
            }
            Label { visible: frame.filesTotal > 0; text: "·"; color: Theme.textMuted }
            Label {
                font.family: Theme.dataFamily
                text: Theme.humanBytes(frame.bytesDone) + (frame.bytesTotal > 0 ? " of " + Theme.humanBytes(frame.bytesTotal) : "")
            }
            Label { visible: frame.bytesPerSecond > 0; text: "·"; color: Theme.textMuted }
            Label {
                visible: frame.bytesPerSecond > 0
                font.family: Theme.dataFamily
                text: (frame.bytesPerSecond / (1024 * 1024)).toFixed(1) + " MiB/s"
            }
            Item { Layout.fillWidth: true }
            Label {
                visible: frame.etaSeconds >= 0
                color: Theme.textMuted
                text: Theme.humanDuration(frame.etaSeconds) + " remaining"
            }
        }
        Label {
            visible: frame.currentFile.length > 0
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            color: Theme.textMuted
            font.pointSize: Theme.fontSmall
            font.family: Theme.dataFamily
            text: frame.currentFile
        }
    }
}
