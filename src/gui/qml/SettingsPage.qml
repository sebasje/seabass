import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DjConvertGui

Page {
    id: root
    required property string stickLabel
    required property string pioneerRoot

    SettingsController {
        id: settingsController
    }

    Component.onCompleted: settingsController.load(root.pioneerRoot)

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            ToolButton {
                text: "‹"

                font.pixelSize: 22
                enabled: !settingsController.busy

                ToolTip.visible: hovered

                ToolTip.text: settingsController.busy ? "Wait for the write to finish before leaving this page" : "Back"
                onClicked: root.StackView.view.pop()
            }
            Label {
                text: root.stickLabel + " -- Device Settings"
                font.bold: true
                font.pointSize: 14
            }
            ToolButton {
                text: "ⓘ"
                font.pointSize: 12
                ToolTip.visible: hovered
                ToolTip.text: "These are the player/mixer preference files Rekordbox (Pioneer hardware) "
                    + "writes to the stick -- tempo range, quantize, auto cue level and similar. Denon "
                    + "Prime hardware reads Rekordbox USB drives natively for library/track/cue data, "
                    + "and some Prime units are reported to honor these same preference files too, "
                    + "though that isn't something Seabass can verify from the stick alone."
            }
            Item { Layout.fillWidth: true }
            BusyIndicator {
                visible: settingsController.busy
                running: settingsController.busy
                implicitWidth: 24
                implicitHeight: 24
            }
        }
    }

    Dialog {
        id: confirmFieldDialog
        property string targetFileName: ""
        property string targetLabel: ""
        property string oldValue: ""
        property string newValue: ""
        anchors.centerIn: parent
        modal: true
        title: "Change This Setting?"
        footer: DialogButtonBox {
            Button { text: "Save Setting"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: settingsController.setField(targetFileName, targetLabel, newValue)

        Label {
            text: "\"" + confirmFieldDialog.targetLabel + "\": " + confirmFieldDialog.oldValue
                + " -> " + confirmFieldDialog.newValue + "\n\n"
                + confirmFieldDialog.targetFileName + " is backed up first, and this only ever writes "
                + "a field Seabass fully understands the format of."
            wrapMode: Text.WordWrap
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 16
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 16

            Label {
                visible: settingsController.errorMessage.length > 0
                text: settingsController.errorMessage
                color: "#ff8080"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                visible: settingsController.statusMessage.length > 0
                text: settingsController.statusMessage
                color: "#8fce8f"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Repeater {
                model: settingsController.groups
                delegate: GroupBox {
                    id: groupBox
                    Layout.fillWidth: true
                    required property var modelData
                    title: modelData.title + " (" + modelData.fileName + ")"

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 4

                        Repeater {
                            model: groupBox.modelData.fields
                            delegate: RowLayout {
                                id: fieldRow
                                required property var modelData
                                Layout.fillWidth: true
                                Label { text: fieldRow.modelData.label + ":"; color: "gray"; Layout.preferredWidth: 180 }
                                ComboBox {
                                    id: valueCombo
                                    Layout.preferredWidth: 220
                                    model: fieldRow.modelData.options
                                    // Falls back to -1 (nothing selected) for a byte value outside
                                    // every known option -- e.g. "unknown (0x..)" -- rather than
                                    // guess, since that value isn't one setField() could write back.
                                    currentIndex: fieldRow.modelData.options.indexOf(fieldRow.modelData.value)
                                    enabled: fieldRow.modelData.options.length > 0 && !settingsController.busy
                                    onActivated: (index) => {
                                        var chosen = valueCombo.textAt(index);
                                        // Snap straight back -- nothing is actually selected until
                                        // the confirm dialog is accepted, at which point setField()
                                        // triggers a reload that re-binds this to the real new value.
                                        valueCombo.currentIndex = fieldRow.modelData.options.indexOf(fieldRow.modelData.value);
                                        if (chosen === fieldRow.modelData.value) {
                                            return;
                                        }
                                        confirmFieldDialog.targetFileName = groupBox.modelData.fileName;
                                        confirmFieldDialog.targetLabel = fieldRow.modelData.label;
                                        confirmFieldDialog.oldValue = fieldRow.modelData.value;
                                        confirmFieldDialog.newValue = chosen;
                                        confirmFieldDialog.open();
                                    }
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }
                }
            }
        }
    }
}
