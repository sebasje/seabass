import QtQuick
import QtQuick.Controls
import SeabassGui

// The ordinary case, on top of SeabassDialog's frame: a message, a verb,
// and a way out. This is what the hand-rolled `Dialog { title; footer;
// Label }` blocks scattered through the pages become.
//
//   MessageDialog {
//       severity: SeabassDialog.Warning
//       title: "Delete This Backup?"
//       headline: "This permanently deletes this one backup copy."
//       detailText: "It never touches the stick's live data."
//       acceptText: "Delete"
//       destructive: true
//       onAccepted: backupsController.deleteBackup(targetId)
//   }
//
// `destructive` is the important one. It marks the action button, and it
// moves the default to Cancel -- so Return cancels rather than deletes.
// Several of the dialogs this replaces had Delete on AcceptRole, which
// made the destructive action the default.
SeabassDialog {
    id: root

    property string acceptText: "OK"
    property string rejectText: "Cancel"
    // An acknowledgement rather than a decision: OK on its own.
    property bool showReject: true
    property bool destructive: false

    closePolicy: Popup.NoAutoClose

    // DialogButtonBox writes to its buttons' `highlighted` itself, which
    // breaks a binding declared on it -- both buttons come out unhighlighted
    // whatever the expression says. So the default button is assigned after
    // the box has had its way, and again whenever `destructive` changes.
    function applyDefaultButton() {
        acceptButton.highlighted = !root.destructive;
        rejectButton.highlighted = root.destructive;
    }
    onDestructiveChanged: root.applyDefaultButton()
    Component.onCompleted: root.applyDefaultButton()

    footer: DialogButtonBox {
        alignment: Qt.AlignRight | Qt.AlignVCenter

        Button {
            id: acceptButton
            objectName: "acceptButton"
            text: root.acceptText
            focus: !root.destructive
            // DialogButtonBox turns AcceptRole into accepted() by itself,
            // but DestructiveRole into rejected() -- so the destructive
            // path has to say what it means explicitly.
            DialogButtonBox.buttonRole: root.destructive ? DialogButtonBox.DestructiveRole
                                                         : DialogButtonBox.AcceptRole
            onClicked: {
                if (root.destructive) {
                    root.accept();
                }
            }
        }

        Button {
            id: rejectButton
            objectName: "rejectButton"
            visible: root.showReject
            text: root.rejectText
            focus: root.destructive
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }
}
