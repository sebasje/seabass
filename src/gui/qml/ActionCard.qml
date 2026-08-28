import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DjConvertGui

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
            Label {
                text: card.cardTitle
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
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
