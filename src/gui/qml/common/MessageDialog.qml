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
    // Ported dialogs keep whatever objectName their tests already look for,
    // the way TypedConfirmDialog does.
    property string acceptObjectName: "acceptButton"
    property string rejectObjectName: "rejectButton"

    // Escape cancels, a click outside does not. The dialogs this replaces
    // mostly took Qt's default, which dismissed them on an outside click --
    // easy to do by accident when the answer matters.
    closePolicy: Popup.CloseOnEscape

    // DialogButtonBox writes to its buttons' `highlighted` itself, which
    // breaks a binding declared on it -- both buttons come out unhighlighted
    // whatever the expression says. So the default button is assigned rather
    // than bound, and re-assigned on open, because the box writes to it again
    // every time the dialog is shown. Caught by a screenshot: the property
    // read correctly right after construction and was gone by the time
    // anyone could see it.
    function applyDefaultButton() {
        acceptButton.highlighted = !root.destructive;
        rejectButton.highlighted = root.destructive;
    }
    onDestructiveChanged: root.applyDefaultButton()
    Component.onCompleted: root.applyDefaultButton()
    onOpened: root.applyDefaultButton()

    footer: DialogButtonBox {
        alignment: Qt.AlignRight | Qt.AlignVCenter

        Button {
            id: acceptButton
            objectName: root.acceptObjectName
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
            objectName: root.rejectObjectName
            visible: root.showReject
            text: root.rejectText
            focus: root.destructive
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }
}
