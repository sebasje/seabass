// SPDX-FileCopyrightText: 2026 Sebastian Kügler <sebas@kde.org>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

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
    // And it may be made narrower than its natural width.
    //
    // Without this the breadcrumb was the hard floor under every page
    // that has one. A RowLayout child cannot be laid out below its
    // implicit width unless a minimum says so, and a ColumnLayout gives
    // ALL its fill-width children the widest such floor among them --
    // so on Clean Up, whose title is long, one unshrinkable breadcrumb
    // pinned the entire header at 701px and every row in it overflowed
    // any window narrower than that, whatever those rows did about their
    // own sizing. Measured in tests/qml/tst_CleanupPage.qml: the filter
    // row was 701 wide at page widths of 960, 700, 520 and 380 alike.
    Layout.minimumWidth: 0

    component Crumb: AbstractButton {
        id: crumb
        enabled: root.backEnabled
        hoverEnabled: true
        // Each segment gives way in turn rather than the row refusing to
        // shrink. The label already elides; eliding needs to be allowed
        // to happen, which is what a zero minimum says.
        Layout.minimumWidth: 0
        Layout.maximumWidth: implicitWidth

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
        // The segment that gives way first. "Home > Hou... > Clean Up
        // Duplicates" tells a reader what page they are on; "Home >
        // Housekeeping > Clea..." tells them where it sits and leaves
        // them guessing what it is. The middle is also the one they can
        // most easily infer, being one click behind them.
        //
        // With a floor, though. Squeezed to zero it left "Home >  >
        // Clean Up Duplicates" -- a gap and a dangling separator, which
        // reads as a bug rather than as an abbreviation. A few
        // characters and an ellipsis still say a name was here.
        Layout.fillWidth: true
        Layout.minimumWidth: 64 * Theme.iconScale
        text: root.middleLabel
        onClicked: root.backRequested()
        ToolTip.text: root.backEnabled ? ("Back to " + root.middleLabel) : root.backDisabledTooltip
    }

    Sep {}

    // The page's own name. fillWidth as well, because measurement says
    // an item without it does not shrink here at all -- a minimum of 0
    // is not enough on its own, and the title kept its full 307px
    // inside a 345px row and simply hung out of it.
    //
    // Priority between the two shrinkable segments is expressed as
    // floors rather than as order: the middle gives way to 64 and the
    // title only to 120, so the middle is spent first and the page's own
    // name is still readable when it is.
    PageTitle {
        text: root.title
        elide: Text.ElideRight
        Layout.fillWidth: true
        Layout.minimumWidth: 120 * Theme.iconScale
    }
}
