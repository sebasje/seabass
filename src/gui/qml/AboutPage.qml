import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DjConvertGui

Page {
    id: root

    header: ToolBar {
        // Opaque background override -- see AppSettingsPage.qml's header
        // for why (KDE's Breeze style bleeds the window behind Seabass
        // through an unstyled ToolBar).
        background: Rectangle { color: Theme.surface }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            ToolButton {
                text: "‹"

                font.pointSize: Theme.fontHuge

                ToolTip.visible: hovered

                ToolTip.text: "Back"
                onClicked: root.StackView.view.pop()
            }
            Label {
                text: "About"
                font.bold: true
                font.pointSize: Theme.fontLarge
            }
            Item { Layout.fillWidth: true }
        }
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight + 64
        clip: true

        ColumnLayout {
            id: content
            x: Math.max(32, (parent.width - width) / 2)
            width: Math.min(parent.width - 64, 640)
            y: 32
            spacing: 20

            Image {
                source: "qrc:/qt/qml/DjConvertGui/qml/icons/seabass_soundbass.svg"
                Layout.preferredWidth: 96
                Layout.preferredHeight: 96
                Layout.alignment: Qt.AlignHCenter
                fillMode: Image.PreserveAspectFit
            }

            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 2
                Label {
                    text: "Seabass"
                    font.bold: true
                    font.pointSize: Theme.baseFontPointSize * 2.6
                    Layout.alignment: Qt.AlignHCenter
                }
                Label {
                    text: "DJ USB Stick Management"
                    font.pointSize: Theme.baseFontPointSize * 1.3
                    color: Qt.lighter(Theme.accent, 1.3)
                    Layout.alignment: Qt.AlignHCenter
                }
            }

            Label {
                text: "Seabass reads and writes the DJ data already on a Rekordbox or Denon Engine "
                    + "USB stick -- cue points, playlists, and the metadata your DJ software already "
                    + "computed -- so you can inspect it, fix it up, and keep the two formats in sync."
                wrapMode: Text.WordWrap
                font.pointSize: Theme.baseFontPointSize * 1.1
                Layout.fillWidth: true
            }

            ColumnLayout {
                spacing: 6
                Layout.fillWidth: true
                Label { text: "What Seabass does"; font.bold: true; font.pointSize: Theme.fontMedium }
                Label {
                    text: "• Browses tracks, playlists and cue points on Rekordbox and Engine sticks\n"
                        + "• Finds duplicate tracks and consolidates their cue points onto every copy\n"
                        + "• Syncs hot cues between the Rekordbox and Engine copies of the same stick\n"
                        + "• Lists and cleans up the automatic backups made before every write\n"
                        + "• Backs up cues to this computer and restores them if a stick's cues are lost\n"
                        + "• Shows a stick's saved Rekordbox player settings"
                    wrapMode: Text.WordWrap
                    font.pointSize: Theme.baseFontPointSize * 1.1
                    color: Theme.textMuted
                    Layout.fillWidth: true
                }
            }

            ColumnLayout {
                spacing: 6
                Layout.fillWidth: true
                Label { text: "The three library catalogs"; font.bold: true; font.pointSize: Theme.fontMedium }
                Label {
                    text: "A stick can carry up to three separate, independently-maintained catalogs of "
                        + "the same tracks -- Browse Library can switch between whichever ones are present:"
                    wrapMode: Text.WordWrap
                    font.pointSize: Theme.baseFontPointSize * 1.1
                    color: Theme.textMuted
                    Layout.fillWidth: true
                }
                Label {
                    text: "• Engine OS -- Denon Engine DJ's own library (m.db). What Denon/inMusic "
                        + "hardware (SC5000, Prime series, ...) reads directly.\n"
                        + "• DeviceLibrary -- Rekordbox's classic per-stick export (export.pdb). What "
                        + "CDJs and XDJs read directly; every rekordbox export has this.\n"
                        + "• OneLibrary -- Rekordbox 7's newer unified library (exportLibrary.db, also "
                        + "called \"Device Library Plus\"). Mirrors DeviceLibrary's tracks in a richer "
                        + "schema, kept in sync by Rekordbox itself; not present on every export."
                    wrapMode: Text.WordWrap
                    font.pointSize: Theme.baseFontPointSize * 1.1
                    color: Theme.textMuted
                    Layout.fillWidth: true
                }
                Label {
                    text: "OneLibrary is browse-only in Seabass for now -- merging duplicates and adding "
                        + "cues only work against DeviceLibrary and Engine OS."
                    wrapMode: Text.WordWrap
                    font.pointSize: Theme.baseFontPointSize
                    font.italic: true
                    color: Theme.textMuted
                    Layout.fillWidth: true
                }
            }

            ColumnLayout {
                spacing: 6
                Layout.fillWidth: true
                Label { text: "What Seabass doesn't do"; font.bold: true; font.pointSize: Theme.fontMedium }
                Label {
                    text: "Seabass never analyzes audio. Beatgridding, BPM/key detection, and waveform "
                        + "analysis all have to happen in Rekordbox or Engine DJ software first -- "
                        + "Seabass only ever reads and moves around the results of that analysis, "
                        + "never recomputes it."
                    wrapMode: Text.WordWrap
                    font.pointSize: Theme.baseFontPointSize * 1.1
                    color: Theme.textMuted
                    Layout.fillWidth: true
                }
            }

            Label {
                text: "Rekordbox is a trademark of AlphaTheta Corporation. Engine DJ is a trademark of "
                    + "inMusic Brands, Inc. Seabass is an independent, unofficial tool and is not "
                    + "affiliated with, endorsed by, or sponsored by either company."
                wrapMode: Text.WordWrap
                font.pointSize: Theme.fontTiny
                color: Theme.textMuted
                Layout.fillWidth: true
                Layout.topMargin: 8
            }

            Label {
                text: "Also known as djconvert on the command line."
                font.pointSize: Theme.baseFontPointSize * 0.9
                color: Theme.textMuted
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 8
            }
        }
    }
}
