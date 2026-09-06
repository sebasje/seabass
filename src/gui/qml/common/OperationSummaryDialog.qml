import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// "5 of 31 tracks written": what a save or a direct write operation
// actually did, shown after it finished, was cancelled, or failed. OK is
// the only way out; the page decides what happens after (stay, or go back
// to where the user came from).
//
// summary: {written, total, unit, cancelled, error} plus an optional
// verb ("written" by default; "deleted" for a prune).
Dialog {
    id: dialog
    property var summary: ({})

    readonly property int written: dialog.summary.written !== undefined ? dialog.summary.written : 0
    readonly property int total: dialog.summary.total !== undefined ? dialog.summary.total : 0
    readonly property string unit: dialog.summary.unit !== undefined ? dialog.summary.unit : "items"
    readonly property string verb: dialog.summary.verb !== undefined ? dialog.summary.verb : "written"
    readonly property bool cancelled: dialog.summary.cancelled === true
    readonly property string error: dialog.summary.error !== undefined ? dialog.summary.error : ""

    function show(newSummary) {
        dialog.summary = newSummary;
        dialog.open();
    }

    anchors.centerIn: parent
    modal: true
    closePolicy: Popup.NoAutoClose
    width: 460
    title: dialog.error.length > 0 ? "Stopped with an error"
        : dialog.cancelled ? "Cancelled"
        : "Done"

    footer: DialogButtonBox {
        Button {
            objectName: "okButton"
            text: "OK"
            highlighted: true
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }

    ColumnLayout {
        width: parent.width
        spacing: 8
        Label {
            objectName: "countLabel"
            color: Theme.text
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.family: Theme.titleFamily
            font.weight: Theme.cardTitleWeight
            font.pointSize: Theme.fontMedium
            text: dialog.written + " of " + dialog.total + " " + dialog.unit + " " + dialog.verb + "."
        }
        Label {
            objectName: "detailLabel"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: text.length > 0
            color: dialog.error.length > 0 ? Theme.danger : Theme.textMuted
            text: dialog.error.length > 0 ? "Then: " + dialog.error
                : dialog.cancelled ? "Stopped at your request. Everything up to here is complete; the rest was not touched."
                : ""
        }
    }
}
