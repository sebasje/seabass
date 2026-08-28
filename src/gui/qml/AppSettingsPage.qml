import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DjConvertGui

Page {
    id: root
    required property var appSettingsController

    header: ToolBar {
        // Explicit opaque background -- KDE's platform theme integration
        // resolves ToolBar to its own "org.kde.breeze" style regardless of
        // this app's Material palette (see main.cpp's exportMaterialPalette()
        // comment for the fuller story of why that isn't forced away
        // globally), and Breeze's own ToolBar background can render
        // translucent/blurred, letting whatever window is behind Seabass
        // show through the header. A plain opaque Rectangle sidesteps
        // whichever style actually resolves, same fix class as
        // StickListPage.qml's Frame-vs-Rectangle precedent.
        background: Rectangle { color: Theme.surface }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            ToolButton {
                text: "‹"

                font.pointSize: Theme.fontHuge

                ToolTip.visible: hovered

                ToolTip.text: "Back"
                onClicked: root.StackView.view.pop()
            }
            Label {
                text: "App Settings"
                font.bold: true
                font.pointSize: Theme.fontLarge
            }
            Item { Layout.fillWidth: true }
        }
    }

    ColumnLayout {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 24
        spacing: 12

        Label {
            text: "Theme"
            font.bold: true
        }
        ButtonGroup { id: themeGroup }
        RadioButton {
            text: "Dark"
            ButtonGroup.group: themeGroup
            checked: !root.appSettingsController.useSystemTheme
            onCheckedChanged: if (checked) root.appSettingsController.useSystemTheme = false
        }
        RadioButton {
            text: "Match System Theme"
            ButtonGroup.group: themeGroup
            checked: root.appSettingsController.useSystemTheme
            onCheckedChanged: if (checked) root.appSettingsController.useSystemTheme = true
        }
    }
}
