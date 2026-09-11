// SPDX-FileCopyrightText: 2026 Sebastian Kügler <sebas@kde.org>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

import QtQuick
import QtTest
import SeabassGui

// Renders the brand SVG the way the app does (PreserveAspectFit in a
// square, then in the About page's 96 px box) and saves it when
// SEABASS_SCREENSHOT_DIR is set -- the only way to look at the icon
// without a desktop.
TestCase {
    id: testCase
    name: "Logo"
    width: 480
    height: 240
    visible: true
    when: windowShown

    Component {
        id: logoComponent
        Row {
            spacing: 16
            Rectangle {
                width: 200; height: 200; color: "#1b1e22"; border.color: "#3daee9"
                Image {
                    anchors.fill: parent
                    source: "qrc:/qt/qml/SeabassGui/src/gui/qml/icons/seabass_soundbass.svg"
                    fillMode: Image.PreserveAspectFit
                    sourceSize: Qt.size(200, 200)
                }
            }
            Rectangle {
                width: 96; height: 96; color: "#1b1e22"; border.color: "#3daee9"
                Image {
                    anchors.fill: parent
                    source: "qrc:/qt/qml/SeabassGui/src/gui/qml/icons/seabass_soundbass.svg"
                    fillMode: Image.PreserveAspectFit
                    sourceSize: Qt.size(96, 96)
                }
            }
            Rectangle {
                width: 32; height: 32; color: "#1b1e22"; border.color: "#3daee9"
                Image {
                    anchors.fill: parent
                    source: "qrc:/qt/qml/SeabassGui/src/gui/qml/icons/seabass_soundbass.svg"
                    fillMode: Image.PreserveAspectFit
                    sourceSize: Qt.size(32, 32)
                }
            }
        }
    }

    function test_renders() {
        var row = createTemporaryObject(logoComponent, testCase, {});
        verify(row !== null);
        waitForRendering(row);
        if (screenshotDir && screenshotDir.length > 0) {
            grabImage(row).save(screenshotDir + "/logo.png");
        }
    }
}
