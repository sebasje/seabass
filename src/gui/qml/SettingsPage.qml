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
                text: "< Back"
                onClicked: root.StackView.view.pop()
            }
            Label {
                text: root.stickLabel + " -- Device Settings"
                font.bold: true
                font.pointSize: 14
            }
            Item { Layout.fillWidth: true }
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

            Repeater {
                model: settingsController.groups
                delegate: GroupBox {
                    Layout.fillWidth: true
                    required property var modelData
                    title: modelData.title + " (" + modelData.fileName + ")"

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 4

                        Repeater {
                            model: modelData.fields
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Label { text: modelData.label + ":"; color: "gray"; Layout.preferredWidth: 180 }
                                Label { text: modelData.value; font.bold: true }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }
                }
            }
        }
    }
}
