import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// The shared frame every Seabass dialog wears, in the shape KDE's
// KMessageBox settled on: a severity badge on the left, the question in
// the title bar, the consequence first and the reassurance under it, and
// the buttons on the right named by their verb.
//
// This carries chrome only. It has no buttons of its own and no idea what
// the dialog is for -- MessageDialog sits on top of it for the ordinary
// cases, and the purpose-built dialogs (StickRemoved, LockedLibrary,
// UnsavedChanges...) derive from it directly and keep their own logic and
// their own footer.
//
//   SeabassDialog {
//       severity: SeabassDialog.Warning
//       title: "USB stick removed"
//       headline: "WHALESHARK has been removed while editing."
//       detailText: "Either discard changes or plug the stick back in."
//       footer: DialogButtonBox { ... }
//   }
//
// Anything declared as a child is placed under the message, indented to
// the text column so it lines up rather than sitting under the badge.
Dialog {
    id: root

    enum Severity {
        Info,
        Warning,
        Error,
        Question
    }

    property int severity: SeabassDialog.Info
    // The consequence, in one sentence. The title bar carries the question.
    property string headline: ""
    // The qualifier under it, quieter. Optional.
    property string detailText: ""
    // A progress dialog has no severity to report; everything else does.
    property bool showBadge: true
    // Only ever offered on a question -- never on a destructive action and
    // never on an error, where "do not tell me again" is not a safe answer.
    property bool dontAskVisible: false
    property alias dontAskChecked: dontAskBox.checked

    // Re-based dialogs keep whatever names their tests already look for.
    property string messageObjectName: "messageLabel"
    property string detailObjectName: "messageDetailLabel"

    default property alias extraContent: extraColumn.data

    readonly property color severityColor: {
        switch (root.severity) {
        case SeabassDialog.Warning: return Theme.warnIcon;
        case SeabassDialog.Error: return Theme.danger;
        case SeabassDialog.Question: return Theme.accent;
        default: return Theme.info;
        }
    }
    readonly property string severityGlyph: {
        switch (root.severity) {
        case SeabassDialog.Warning: return "!";
        case SeabassDialog.Error: return "✕";
        case SeabassDialog.Question: return "?";
        default: return "i";
        }
    }

    anchors.centerIn: parent
    modal: true
    width: 520

    // Every Seabass dialog puts its buttons on the right. Enforced here
    // rather than left to each footer, so a derived dialog that supplies
    // its own DialogButtonBox cannot quietly get it wrong.
    function alignFooter() {
        if (root.footer && root.footer.alignment !== undefined) {
            root.footer.alignment = Qt.AlignRight | Qt.AlignVCenter;
        }
    }
    onFooterChanged: root.alignFooter()
    Component.onCompleted: root.alignFooter()

    RowLayout {
        width: parent.width
        spacing: 14

        Rectangle {
            objectName: "severityBadge"
            visible: root.showBadge
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: Theme.iconSizeSmall
            Layout.preferredHeight: Theme.iconSizeSmall
            radius: width / 2
            color: Qt.rgba(root.severityColor.r, root.severityColor.g, root.severityColor.b, 0.14)
            border.width: 1
            border.color: Qt.rgba(root.severityColor.r, root.severityColor.g, root.severityColor.b, 0.45)

            Label {
                anchors.centerIn: parent
                text: root.severityGlyph
                color: root.severityColor
                font.bold: true
                font.pointSize: Theme.fontMedium
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                objectName: root.messageObjectName
                Layout.fillWidth: true
                visible: root.headline.length > 0
                text: root.headline
                color: Theme.text
                wrapMode: Text.WordWrap
            }

            // Selectable: an Error dialog's detail is exactly the string
            // someone needs to quote back, and Label cannot be selected.
            SelectableText {
                objectName: root.detailObjectName
                visible: root.detailText.length > 0
                text: root.detailText
                color: Theme.textMuted
                font.pointSize: Theme.fontSmall
            }

            ColumnLayout {
                id: extraColumn
                Layout.fillWidth: true
                spacing: 8
            }

            CheckBox {
                id: dontAskBox
                objectName: "dontAskCheckBox"
                visible: root.dontAskVisible
                text: "Do not ask again"
                font.pointSize: Theme.fontSmall
            }
        }
    }
}
