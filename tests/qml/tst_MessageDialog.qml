import QtQuick
import QtQuick.Controls
import QtTest
import SeabassGui

// The shared dialog frame and its ordinary-case preset, plus their first
// real user: the gate that warns, on entering edit mode, that this stick
// cannot hold a backup and undo will stop being portable.
TestCase {
    id: testCase
    name: "MessageDialog"
    width: 900
    height: 700
    visible: true
    when: windowShown

    readonly property real gb: 1073741824
    readonly property real mb: 1048576

    Component {
        id: messageComponent
        MessageDialog {}
    }

    Component {
        id: sessionComponent
        QtObject {
            property string libraryId: "EB9F-F032"
            property string stickLabel: "WHALESHARK"
            property bool dirty: false
            property bool writing: false
            property int pendingCount: 0
            signal saveFinished(var summary)
            signal lockRefused(var holder)
            function save() {}
            function discard() {}
        }
    }

    Component {
        id: registryComponent
        QtObject {
            property var session: null
            property bool quitAfterSave: false
            function openSession(id, label, rb, engine) { return session; }
            function closeSession(id) {}
            function removeLock(id) {}
        }
    }

    Component {
        id: hostComponent
        EditSessionHost { width: 800; height: 600 }
    }

    function findByObjectName(item, name) {
        if (!item) return null;
        if (item.objectName === name) return item;
        var kids = [];
        if (item.contentItem) kids.push(item.contentItem);
        if (item.footer) kids.push(item.footer);
        var children = item.children ? item.children : [];
        for (var i = 0; i < children.length; ++i) kids.push(children[i]);
        var resources = item.resources ? item.resources : [];
        for (var r = 0; r < resources.length; ++r) kids.push(resources[r]);
        for (var j = 0; j < kids.length; ++j) {
            var found = findByObjectName(kids[j], name);
            if (found) return found;
        }
        return null;
    }

    function makeHost(props) {
        var session = createTemporaryObject(sessionComponent, testCase);
        var registry = createTemporaryObject(registryComponent, testCase, {session: session});
        var base = {registry: registry, libraryId: "EB9F-F032", stickLabel: "WHALESHARK"};
        for (var k in props) {
            base[k] = props[k];
        }
        var host = createTemporaryObject(hostComponent, testCase, base);
        waitForRendering(host);
        return host;
    }

    // --- the frame --------------------------------------------------------

    function test_buttonsAreForcedToTheRight() {
        // Every Seabass dialog puts its buttons on the right, and the base
        // enforces it rather than trusting each footer to remember.
        var dialog = createTemporaryObject(messageComponent, testCase, {title: "x"});
        verify(dialog.footer !== null);
        compare(dialog.footer.alignment & Qt.AlignRight, Qt.AlignRight);
    }

    function test_severityPicksTheBadge_data() {
        return [
            {tag: "info", severity: SeabassDialog.Info, glyph: "i"},
            {tag: "warning", severity: SeabassDialog.Warning, glyph: "!"},
            {tag: "error", severity: SeabassDialog.Error, glyph: "✕"},
            {tag: "question", severity: SeabassDialog.Question, glyph: "?"},
        ];
    }

    function test_severityPicksTheBadge(data) {
        var dialog = createTemporaryObject(messageComponent, testCase,
                                           {title: "x", severity: data.severity});
        compare(dialog.severityGlyph, data.glyph);
        verify(findByObjectName(dialog, "severityBadge") !== null);
    }

    function test_destructiveMovesTheDefaultToCancel() {
        // Several of the dialogs this replaces had Delete on AcceptRole,
        // which made Return delete. The safe button takes the default.
        var dialog = createTemporaryObject(messageComponent, testCase, {
            title: "Delete This Backup?",
            headline: "This permanently deletes this one backup copy.",
            acceptText: "Delete",
            destructive: true
        });
        var accept = findByObjectName(dialog.footer, "acceptButton");
        var reject = findByObjectName(dialog.footer, "rejectButton");
        compare(accept.text, "Delete");
        compare(accept.highlighted, false);
        compare(reject.highlighted, true);

        // And the other way round for an ordinary confirmation.
        var plain = createTemporaryObject(messageComponent, testCase, {title: "y"});
        compare(findByObjectName(plain.footer, "acceptButton").highlighted, true);
        compare(findByObjectName(plain.footer, "rejectButton").highlighted, false);
    }

    function test_destructiveStillAccepts() {
        // DialogButtonBox turns DestructiveRole into rejected(), so the
        // preset has to raise accepted() itself. Guard that it does.
        var dialog = createTemporaryObject(messageComponent, testCase,
                                           {title: "x", acceptText: "Delete", destructive: true});
        var spy = signalSpy.createObject(testCase, {target: dialog, signalName: "accepted"});
        dialog.open();
        tryCompare(dialog, "opened", true);
        findByObjectName(dialog.footer, "acceptButton").clicked();
        compare(spy.count, 1);
    }

    function test_acknowledgementHidesCancel() {
        // Opened first: `visible` is inherited, so every button of a closed
        // dialog reports false and the assertion would pass for free.
        var dialog = createTemporaryObject(messageComponent, testCase,
                                           {title: "x", showReject: false});
        dialog.open();
        tryCompare(dialog, "opened", true);
        compare(findByObjectName(dialog.footer, "acceptButton").visible, true);
        compare(findByObjectName(dialog.footer, "rejectButton").visible, false);
    }

    Component {
        id: signalSpy
        SignalSpy {}
    }

    // --- its first user: the low-space gate -------------------------------

    function test_silentWhenTheNumbersAreUnknown() {
        // Every page that has not been taught to supply them yet.
        var host = makeHost({});
        compare(host.backupWouldGoLocal, false);
        compare(findByObjectName(host, "lowSpaceDialog").opened, false);
    }

    function test_silentWhenTheStickHasRoom() {
        var host = makeHost({
            stickBytesCapacity: 30 * testCase.gb,
            stickBytesFree: 20 * testCase.gb,
            backupBytesWorstCase: 318 * testCase.mb
        });
        compare(host.backupWouldGoLocal, false);
        compare(findByObjectName(host, "lowSpaceDialog").opened, false);
    }

    function test_asksOnEnteringEditModeWhenTheStickIsTight() {
        var host = makeHost({
            stickBytesCapacity: 30 * testCase.gb,
            stickBytesFree: 1.2 * testCase.gb,
            backupBytesWorstCase: 812 * testCase.mb
        });
        compare(host.backupWouldGoLocal, true);
        var dialog = findByObjectName(host, "lowSpaceDialog");
        tryCompare(dialog, "opened", true);

        // The numbers have to be in the message: "low on space" alone does
        // not let anyone decide.
        compare(dialog.title, "Not enough room on WHALESHARK");
        verify(dialog.headline.indexOf("812 MB") >= 0);
        verify(dialog.headline.indexOf("1.2 GB") >= 0);
        verify(dialog.detailText.indexOf("only") >= 0);
    }

    function test_headroomScalesWithABigStick() {
        // 2% of a 256 GB stick is 5.1 GB, so 3 GB free is tight there even
        // though it would be plenty on a 30 GB one.
        var host = makeHost({
            stickBytesCapacity: 256 * testCase.gb,
            stickBytesFree: 3 * testCase.gb,
            backupBytesWorstCase: 100 * testCase.mb
        });
        compare(host.backupWouldGoLocal, true);
    }

    function test_acceptingEditsAnyway() {
        var host = makeHost({
            stickBytesCapacity: 30 * testCase.gb,
            stickBytesFree: 1.2 * testCase.gb,
            backupBytesWorstCase: 812 * testCase.mb
        });
        var spy = signalSpy.createObject(testCase, {target: host, signalName: "backupLocationAccepted"});
        var dialog = findByObjectName(host, "lowSpaceDialog");
        tryCompare(dialog, "opened", true);
        findByObjectName(dialog.footer, "acceptButton").clicked();
        tryCompare(spy, "count", 1);
    }

    function test_cancellingBeforeAnythingIsStaged() {
        var host = makeHost({
            stickBytesCapacity: 30 * testCase.gb,
            stickBytesFree: 1.2 * testCase.gb,
            backupBytesWorstCase: 812 * testCase.mb
        });
        var spy = signalSpy.createObject(testCase, {target: host, signalName: "backupLocationDeclined"});
        var dialog = findByObjectName(host, "lowSpaceDialog");
        tryCompare(dialog, "opened", true);
        findByObjectName(dialog.footer, "rejectButton").clicked();
        tryCompare(spy, "count", 1);
        compare(host.dirty, false);
    }
}
