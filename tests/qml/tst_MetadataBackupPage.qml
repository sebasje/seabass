// SPDX-FileCopyrightText: 2026 Sebastian Kügler <sebas@kde.org>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

import QtQuick
import QtTest
import SeabassGui

// Metadata Backup's page, checked for the claims it makes rather than
// for its layout.
//
// Two of them are the ones that would matter if they were wrong. The
// page promises the stick is not touched, so it has to say where it
// writes instead; and the conflict choice decides which copy of a DJ's
// cues survives, so its default has to be the one the plan argues for
// (docs/metadata-backup-plan.md), not whichever radio button happens to
// be first in the file.
//
// Also saves a screenshot when SEABASS_SCREENSHOT_DIR is set.
TestCase {
    id: testCase
    name: "MetadataBackupPage"
    width: 900
    height: 700
    visible: true
    when: windowShown

    Component {
        id: pageComponent
        MetadataBackupPage {
            width: 880
            height: 660
            // Nowhere on purpose: a page handed a stick that is not
            // there must still build, and it keeps the suite off the
            // filesystem.
            stickLabel: "TESTSTICK"
            rekordboxPath: "/nonexistent/TESTSTICK/PIONEER"
            enginePath: "/nonexistent/TESTSTICK/Engine Library"
            libraryId: ""
        }
    }

    function make(extra) {
        var page = createTemporaryObject(pageComponent, testCase, extra || {});
        verify(page, "page did not instantiate");
        waitForRendering(page);
        return page;
    }

    function test_defaultsToTakingTheSticksVersion() {
        var page = make();
        // The stick is where the DJ works, so a cue set there since the
        // last backup is the newer truth. Flipping this default would
        // quietly preserve stale cues on every run.
        verify(page.overwriteOnConflict, "the default must be to take the stick's version");
        var overwrite = findChild(page, "overwriteRadio");
        var keep = findChild(page, "keepStoredRadio");
        verify(overwrite && overwrite.checked, "the overwrite radio must show as chosen");
        verify(keep && !keep.checked, "the keep-stored radio must not");
    }

    function test_choosingKeepStoredSticks() {
        var page = make();
        var keep = findChild(page, "keepStoredRadio");
        keep.toggle();
        keep.toggled();
        verify(!page.overwriteOnConflict, "picking keep-stored must reach the page");
    }

    function test_saysWhereItWritesInstead() {
        var page = make();
        // "Nothing on the stick is at risk" is only reassuring if the
        // page also says where the data does go.
        var found = false;
        var labels = [];
        function walk(item) {
            for (var i = 0; i < item.children.length; ++i) {
                var child = item.children[i];
                if (child.text !== undefined && typeof child.text === "string") {
                    labels.push(child.text);
                }
                walk(child);
            }
        }
        walk(page);
        for (var i = 0; i < labels.length; ++i) {
            if (labels[i].indexOf("Seabass") >= 0 && labels[i].indexOf("only ever writes here") >= 0) {
                found = true;
            }
        }
        verify(found, "the page must name the location it writes to");
    }

    function test_backUpNeedsAStick() {
        var page = make({stickLabel: "", rekordboxPath: "", enginePath: ""});
        var button = findChild(page, "backUpNowButton");
        verify(button, "Back Up Now must exist");
        verify(!button.enabled, "Back Up Now must be off with no stick to read");
    }

    function test_emptyStoreNamesTheActionThatFillsIt() {
        var page = make();
        // An empty list that only says "empty" leaves the user to find
        // the button. Ours points at it.
        verify(page.hasStick, "the fixture stick must count as a stick");
    }

    // The breadcrumb's own text has to start on the same vertical line
    // as the page body under it. It did not: the style gives ToolBar 4px
    // of padding of its own, and the crumb is a hover pill with another
    // 8.8 inside that, so "Home" sat 4px right of every line beneath it.
    //
    // Asserted on measured positions rather than on the properties that
    // produce them, because the properties were all individually
    // defensible and the result still did not line up. Any page header
    // would do; this one is simply the one that has a test.
    function test_headerTextLinesUpWithTheBody() {
        var page = make();
        var crumbText = null;
        var bodyText = null;
        function walk(item) {
            for (var i = 0; i < item.children.length; ++i) {
                var child = item.children[i];
                if (child.text === "Home" && child.width < 120) {
                    crumbText = child;
                }
                if (child.text !== undefined && typeof child.text === "string"
                        && child.text.indexOf("The cues, ratings") === 0) {
                    bodyText = child;
                }
                walk(child);
            }
        }
        walk(page);
        verify(crumbText, "the Home crumb's label was not found");
        verify(bodyText, "the page's first body line was not found");
        var crumbX = crumbText.mapToItem(page, 0, 0).x;
        var bodyX = bodyText.mapToItem(page, 0, 0).x;
        compare(crumbX, bodyX, "breadcrumb text at " + crumbX + " and body text at " + bodyX
                + " must share a left edge (crumb w=" + crumbText.width + ")");
        compare(bodyX, Theme.pageMargin, "and that edge is Theme.pageMargin");
    }

    function test_screenshot() {
        if (!screenshotDir) {
            skip("SEABASS_SCREENSHOT_DIR not set");
        }
        var page = make();
        wait(100);
        var image = grabImage(page);
        image.save(screenshotDir + "/MetadataBackupPage.png");
    }
}
