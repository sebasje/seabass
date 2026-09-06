import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// Another Seabass instance is editing this library. The safe button is
// the default; the other one is deliberately blunt about what it does.
Dialog {
    id: dialog
    property string libraryId: ""
    // {instanceId, hostname, pid, stickLabel, startedAtUtc}, possibly empty
    property var holder: ({})

    signal removeLockRequested()

    function openFor(id, holderMap) {
        dialog.libraryId = id;
        dialog.holder = holderMap || {};
        dialog.open();
    }

    readonly property string holderLine: {
        if (!dialog.holder || dialog.holder.hostname === undefined || dialog.holder.hostname === "") return "";
        var line = "Held by Seabass on " + dialog.holder.hostname;
        if (dialog.holder.pid !== undefined && dialog.holder.pid > 0) line += " (process " + dialog.holder.pid + ")";
        if (dialog.holder.startedAtUtc !== undefined && dialog.holder.startedAtUtc !== "") line += " since " + dialog.holder.startedAtUtc;
        return line + ".";
    }

    anchors.centerIn: parent
    modal: true
    closePolicy: Popup.NoAutoClose
    width: 520
    title: "This library is locked"

    footer: DialogButtonBox {
        Button {
            objectName: "removeLockButton"
            text: "Remove Lock"
            DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            onClicked: {
                dialog.close();
                dialog.removeLockRequested();
            }
        }
        Button {
            objectName: "staySafeButton"
            text: "Stay on the Safe Side"
            highlighted: true
            focus: true
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }

    ColumnLayout {
        width: parent.width
        spacing: 12
        Label {
            objectName: "messageLabel"
            color: Theme.text
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "This library is currently locked by another instance of Seabass. Finish or discard the work in "
                + "the other instance first.\n\nIf you are absolutely sure that you want to continue from here: "
                + "Fine. Your call, remove the lock. (Don't come complaining.)"
        }
        Label {
            objectName: "holderLabel"
            visible: dialog.holderLine.length > 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textMuted
            font.pointSize: Theme.fontSmall
            text: dialog.holderLine
        }
    }
}
