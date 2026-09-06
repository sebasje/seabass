import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// The report after a restore or clone: the controller's status or error
// line, a one-line count, the Engine database check, and problems counted
// rather than listed until asked for (a stick yanked mid-write used to
// produce one error per remaining file, a wall of text with nothing to
// do about it). `result` is the controller's result map: filesWritten,
// filesUnchanged, directoriesCreated, extrasRemoved, databaseChecked,
// missingTracks, rejected, writeErrors, warnings.
Frame {
    id: resultFrame

    property var result: ({})
    property string errorMessage: ""
    property string statusMessage: ""
    property bool busy: false
    property string startOverTooltip: ""
    signal startOverRequested()

    visible: resultFrame.result.filesWritten !== undefined
    readonly property var problems: (resultFrame.result.rejected || []).concat(resultFrame.result.writeErrors || []).concat(resultFrame.result.warnings || [])
    property bool showProblems: false

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            Label { font.bold: true; text: "Result" }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "startOverButton"
                text: "Start Over"
                flat: true
                enabled: !resultFrame.busy
                ToolTip.visible: hovered && resultFrame.startOverTooltip.length > 0
                ToolTip.text: resultFrame.startOverTooltip
                onClicked: {
                    resultFrame.showProblems = false;
                    resultFrame.startOverRequested();
                }
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: resultFrame.errorMessage.length > 0 ? Theme.danger : Theme.good
            text: resultFrame.errorMessage.length > 0 ? resultFrame.errorMessage : resultFrame.statusMessage
        }
        Label {
            font.family: Theme.dataFamily
            text: resultFrame.result.filesWritten + " written · " + resultFrame.result.filesUnchanged + " unchanged · "
                + resultFrame.result.directoriesCreated + " folders created · " + resultFrame.result.extrasRemoved + " removed"
        }
        Label {
            visible: resultFrame.result.databaseChecked === true
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: (resultFrame.result.missingTracks || []).length === 0 ? Theme.good : Theme.danger
            text: (resultFrame.result.missingTracks || []).length === 0
                ? "Engine database opens and every track it references is present."
                : "Engine database opens, but " + resultFrame.result.missingTracks.length + " referenced track(s) are missing:"
        }
        Repeater {
            model: (resultFrame.result.missingTracks || []).slice(0, 20)
            delegate: Label { required property string modelData; Layout.leftMargin: 16; font.family: Theme.dataFamily; font.pointSize: Theme.fontSmall; color: Theme.danger; text: modelData }
        }
        RowLayout {
            visible: resultFrame.problems.length > 0
            spacing: 8
            Label {
                objectName: "problemCountLabel"
                color: Theme.conflictText
                text: resultFrame.problems.length + (resultFrame.problems.length === 1 ? " problem" : " problems")
            }
            Button {
                objectName: "toggleProblemsButton"
                flat: true
                text: resultFrame.showProblems ? "Hide details" : "Show details"
                onClicked: resultFrame.showProblems = !resultFrame.showProblems
            }
        }
        Repeater {
            objectName: "problemList"
            model: resultFrame.showProblems ? resultFrame.problems.slice(0, 200) : []
            delegate: Label { required property string modelData; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pointSize: Theme.fontSmall; color: Theme.conflictText; text: modelData }
        }
        Label {
            visible: resultFrame.showProblems && resultFrame.problems.length > 200
            color: Theme.textMuted
            font.pointSize: Theme.fontSmall
            text: "and " + (resultFrame.problems.length - 200) + " more"
        }
    }
}
