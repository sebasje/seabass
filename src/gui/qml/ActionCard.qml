import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// A big icon+title+subtitle menu button -- the tappable tile used on
// StickListPage and both hub pages (DuplicatesHubPage, BackupsHubPage) to
// navigate to a specific feature page. Extracted from StickListPage.qml
// (where it originated as an inline `component`) once a second page needed
// the exact same tile.
Button {
    id: card
    property string cardTitle
    property string cardSubtitle
    property string cardIcon
    property string cardIconFont: "Noto Sans Symbols2"
    // See docs/experimental-features.md. A card marked experimental stays
    // hidden until experimentalFeaturesEnabled is on -- set both from the
    // page embedding this card (experimental: true, experimentalFeaturesEnabled:
    // root.appSettingsController.experimentalFeaturesEnabled), not just
    // the first; leaving the second at its false default would hide the
    // card unconditionally regardless of the user's own setting.
    property bool experimental: false
    property bool experimentalFeaturesEnabled: false
    visible: !experimental || experimentalFeaturesEnabled
    Layout.fillWidth: true
    Layout.preferredHeight: 68

    contentItem: RowLayout {
        spacing: 10
        Label {
            text: card.cardIcon
            font.family: card.cardIconFont
            font.pointSize: Theme.fontHuge
            color: card.enabled ? Theme.textMuted : Qt.darker(Theme.textMuted, 1.6)
            Layout.preferredWidth: 30
            horizontalAlignment: Text.AlignHCenter
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Label {
                    text: card.cardTitle
                    font.bold: true
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                // Warning-toned (not the muted/neutral badge idiom used
                // elsewhere, e.g. the streaming-source badge) -- this one's
                // meant to read as a caution, not just a label.
                Rectangle {
                    visible: card.experimental
                    radius: 3
                    color: Theme.warnBg
                    border.color: Theme.warnBorder
                    implicitWidth: experimentalBadgeText.implicitWidth + 8
                    implicitHeight: experimentalBadgeText.implicitHeight + 4
                    Label {
                        id: experimentalBadgeText
                        anchors.centerIn: parent
                        text: "EXPERIMENTAL"
                        font.pointSize: Theme.fontTiny
                        font.bold: true
                        color: Theme.warnText
                    }
                }
            }
            Label {
                text: card.cardSubtitle
                color: card.enabled ? Theme.textMuted : Qt.darker(Theme.textMuted, 1.6)
                font.pointSize: Theme.baseFontPointSize * 0.9
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }
}
