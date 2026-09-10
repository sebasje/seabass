import QtQuick
import QtTest
import SeabassGui

// Restore Metadata's page. Its default is the one claim on it that could
// do real damage if it were wrong: this page writes to the stick, and
// the store may be older than what is on there.
//
// Also saves a screenshot when SEABASS_SCREENSHOT_DIR is set.
TestCase {
    id: testCase
    name: "MetadataRestorePage"
    width: 900
    height: 700
    visible: true
    when: windowShown

    Component {
        id: pageComponent
        MetadataRestorePage {
            width: 880
            height: 660
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

    function test_defaultsToKeepingWhatIsOnTheStick() {
        var page = make();
        // The opposite default from Metadata Backup, deliberately. A
        // stick may have been re-cued since the store was filled, and
        // replacing last night's work with last month's is the worst
        // thing this page could do.
        verify(!page.overwriteConflicts, "the default must be to keep the stick's cues");
        var keep = findChild(page, "keepStickRadio");
        var replace = findChild(page, "replaceFromStoreRadio");
        verify(keep && keep.checked, "the keep radio must show as chosen");
        verify(replace && !replace.checked, "the replace radio must not");
    }

    function test_stageAllIsOffWithNothingToStage() {
        var page = make();
        var button = findChild(page, "stageAllButton");
        verify(button, "Stage All must exist");
        // Nothing scanned against a stick that is not there, so there is
        // nothing on the list and the button must not invite a press.
        verify(!button.enabled, "Stage All must be off with an empty list");
    }

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
                        && child.text.indexOf("Tracks on ") === 0) {
                    bodyText = child;
                }
                walk(child);
            }
        }
        walk(page);
        verify(crumbText, "the Home crumb's label was not found");
        verify(bodyText, "the page's first body line was not found");
        compare(crumbText.mapToItem(page, 0, 0).x, bodyText.mapToItem(page, 0, 0).x,
                "breadcrumb text and body text must share a left edge");
    }

    function test_screenshot() {
        if (!screenshotDir) {
            skip("SEABASS_SCREENSHOT_DIR not set");
        }
        var page = make();
        wait(100);
        var image = grabImage(page);
        image.save(screenshotDir + "/MetadataRestorePage.png");
    }
}
