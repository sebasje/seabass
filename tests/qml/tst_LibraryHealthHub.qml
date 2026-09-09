import QtQuick
import QtQuick.Controls
import QtTest
import SeabassGui

// The Library Health hub: one card per check, each saying what it found in
// a sentence. Drives HealthCheckCard directly -- the hub itself needs a
// real LibraryConsistencyController, which needs a library on disk -- so
// what is covered here is the part that decides what the user reads.
TestCase {
    id: testCase
    name: "LibraryHealthHub"
    width: 800
    height: 600
    visible: true
    when: windowShown

    Component {
        id: cardComponent
        HealthCheckCard {}
    }

    function findByObjectName(item, name) {
        if (!item) return null;
        if (item.objectName === name) return item;
        var kids = item.children ? item.children : [];
        for (var i = 0; i < kids.length; ++i) {
            var found = findByObjectName(kids[i], name);
            if (found) return found;
        }
        return null;
    }

    function test_aCleanCheckStillGetsACard() {
        // "Nothing wrong here" is a result. A page that only lists problems
        // cannot distinguish a clean library from a check that never ran.
        var card = createTemporaryObject(cardComponent, testCase, {
            title: "Tracks and their files",
            summary: "Every track in every catalog on this stick points at a file that is really there.",
            ok: true
        });
        compare(findByObjectName(card, "checkTitle").text, "Tracks and their files");
        verify(findByObjectName(card, "checkSummary").text.length > 0);
        // No action offered when there is nothing to act on.
        compare(findByObjectName(card, "checkAction").parent.visible, false);
    }

    function test_aFindingOffersItsOneAction() {
        var card = createTemporaryObject(cardComponent, testCase, {
            title: "Memory cues at 0:00",
            summary: "27 memory cues sit at 0:00.",
            ok: false,
            actionLabel: "Review these cues"
        });
        var action = findByObjectName(card, "checkAction");
        compare(action.text, "Review these cues");
        compare(action.enabled, true);

        var fired = 0;
        card.actionRequested.connect(function() { fired++; });
        action.clicked();
        compare(fired, 1);
    }

    function test_aBlockedActionSaysWhy() {
        // Rather than a button that silently does nothing -- the case the
        // format-divergence spec runs into, where Seabass can remove a row
        // but cannot yet add one.
        var card = createTemporaryObject(cardComponent, testCase, {
            title: "Library formats in step",
            summary: "483 tracks exist in OneLibrary but not in rekordbox.",
            ok: false,
            actionLabel: "Add them to rekordbox",
            actionEnabled: false,
            actionDisabledReason: "Seabass cannot add rows to export.pdb yet."
        });
        compare(findByObjectName(card, "checkAction").enabled, false);
        var reason = findByObjectName(card, "checkActionReason");
        compare(reason.visible, true);
        verify(reason.text.indexOf("cannot add rows") >= 0);
    }

    function test_screenshotOfTheCardStates() {
        if (!screenshotDir || screenshotDir.length === 0) {
            skip("SEABASS_SCREENSHOT_DIR not set");
        }
        var states = [
            {name: "health-card-clean", props: {title: "Tracks and their files",
                summary: "Every track in every catalog on this stick points at a file that is really there.",
                ok: true}},
            {name: "health-card-finding", props: {title: "Memory cues at 0:00",
                summary: "27 memory cues sit at 0:00. These are almost always accidental -- a stray press "
                       + "while the track was at the start -- rather than something you placed on purpose.",
                ok: false, actionLabel: "Review these cues"}},
            {name: "health-card-running", props: {title: "Tracks and their files",
                summary: "Checking every row in every catalog against the files on the stick (rekordbox)...",
                running: true}}
        ];
        for (var i = 0; i < states.length; ++i) {
            var card = createTemporaryObject(cardComponent, testCase, states[i].props);
            card.width = 640;
            waitForRendering(card);
            grabImage(card).save(screenshotDir + "/" + states[i].name + ".png");
        }
    }
}
