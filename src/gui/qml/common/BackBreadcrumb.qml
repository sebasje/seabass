import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeabassGui

// Replaces the old "‹" ToolButton + separate PageTitle pair every
// section page's header used to duplicate. Up to three segments:
// "Home › [middle] › this page", where Home always jumps straight back
// to the StackView's very first item in one click (pop(null), not a
// single pop()) no matter how deep the current page sits -- the middle
// segment, when present, is one level up (the stick, for a page pushed
// directly from Home, or the hub page, for a page nested inside one).
// Every clickable segment uses Theme.rowHover/rowPressed -- the same
// tint tokens list rows already use -- rather than inventing its own
// hover color, so this is also the fix for hover feedback being
// inconsistent button-to-button across the app: one shared
// background/contentItem means every page's back affordance now hovers
// identically.
RowLayout {
    id: root
    // Empty omits the middle segment entirely (a page pushed directly
    // from Home with no stick/hub context of its own, e.g. Preferences).
    property string middleLabel: ""
    required property string title
    property bool backEnabled: true
    property string backDisabledTooltip: "Wait for the write to finish before leaving this page"
    signal homeRequested()
    signal backRequested()

    spacing: 4 * Theme.iconScale
    // The segments are hover pills with their own left padding, so the
    // text inside the first one starts that much further right than the
    // row does. Pulled back by exactly that, so a page's title lines up
    // with the body beneath it instead of sitting a pill's padding to
    // the right of it. Every header gets this without asking.
    Layout.leftMargin: -Theme.crumbTextInset

    component Crumb: AbstractButton {
        id: crumb
        enabled: root.backEnabled
        hoverEnabled: true

        ToolTip.visible: hovered

        leftPadding: 8 * Theme.iconScale
        rightPadding: 8 * Theme.iconScale
        topPadding: 4 * Theme.iconScale
        bottomPadding: 4 * Theme.iconScale

        background: Rectangle {
            radius: 4 * Theme.iconScale
            color: crumb.pressed ? Theme.rowPressed
                : crumb.hovered ? Theme.rowHover
                : "transparent"
        }
        contentItem: Label {
            text: crumb.text
            font.family: Theme.titleFamily
            font.weight: Theme.titleWeight
            font.pointSize: Theme.titleMedium
            color: Theme.textMuted
            opacity: crumb.enabled ? 1.0 : 0.5
            elide: Text.ElideRight
        }
    }

    component Sep: Label {
        text: "›"
        color: Theme.textMuted
        font.pointSize: Theme.titleMedium
    }

    Crumb {
        text: "Home"
        onClicked: root.homeRequested()
        ToolTip.text: root.backEnabled ? "Back to Home" : root.backDisabledTooltip
    }

    Sep { visible: root.middleLabel.length > 0 }

    Crumb {
        visible: root.middleLabel.length > 0
        text: root.middleLabel
        onClicked: root.backRequested()
        ToolTip.text: root.backEnabled ? ("Back to " + root.middleLabel) : root.backDisabledTooltip
    }

    Sep {}

    PageTitle {
        text: root.title
    }
}
