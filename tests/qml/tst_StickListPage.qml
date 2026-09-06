import QtQuick
import QtTest
import SeabassGui

// StickListPage.qml headless with fake controllers: which cards an empty
// stick and a library stick show once the backup advisor has spoken, and
// what the new clone / update cards request. Also the page's screenshot
// when SEABASS_SCREENSHOT_DIR is set.
TestCase {
    id: testCase
    name: "StickListPage"
    width: 1100
    height: 1000
    visible: true
    when: windowShown

    Component {
        id: pageComponent
        StickListPage { width: 1080; height: 980 }
    }

    function makeStick(overrides) {
        var s = {
            label: "MAIN", mountPoint: "/media/MAIN", devicePath: "/dev/sdb1", mounted: true,
            hasRekordbox: true, hasEngine: true, rekordboxPath: "/media/MAIN/PIONEER",
            enginePath: "/media/MAIN/Engine Library", isSdCard: false, libraryId: "lib-main",
        };
        for (var key in overrides) {
            s[key] = overrides[key];
        }
        return s;
    }

    function noSource() {
        return {kind: "none", label: "", mountPoint: "", backupPath: "", modifiedAt: "", enoughSpace: true, detail: "",
                rekordboxPath: "", enginePath: ""};
    }

    function makeAdvice(overrides) {
        var a = {state: "no-backups", matchedBy: "none", backupPath: "", backupLabel: "", backupCreatedAt: "",
                 trackOverlap: -1, cueOverlap: -1, detail: "No backup of this library yet.",
                 cloneSource: noSource(), updateSource: noSource(), diverged: false};
        for (var key in overrides) {
            a[key] = overrides[key];
        }
        return a;
    }

    function makePage(sticks, advice, overrides) {
        var props = {
            mediaController: {sticks: sticks, errorMessage: "", busy: false, busyDevicePath: "", calls: [],
                              mountStick: function(d) { this.calls.push("mount:" + d); },
                              unmountStick: function(d) { this.calls.push("unmount:" + d); }},
            playbackController: {stop: function() {}},
            appSettingsController: {experimentalFeaturesEnabled: true},
            backupAdvisor: {advice: advice, calls: [],
                            assess: function(l, m, r, e) { this.calls.push("assess:" + m); },
                            reassessAll: function() { this.calls.push("reassessAll"); },
                            forget: function(m) { this.calls.push("forget:" + m); }},
        };
        for (var key in (overrides || {})) {
            props[key] = overrides[key];
        }
        var page = createTemporaryObject(pageComponent, testCase, props);
        verify(page !== null);
        waitForRendering(page);
        return page;
    }

    // ActionCards are instantiated per delegate; find the one whose title
    // starts with `title` under the delegate for `mountPoint`.
    function findCard(page, mountPoint, title) {
        var found = null;
        function walk(item) {
            if (found !== null) return;
            if (item.cardTitle !== undefined && String(item.cardTitle).indexOf(title) === 0
                && delegateMount(item) === mountPoint) {
                found = item;
                return;
            }
            for (var i = 0; i < item.children.length; ++i) {
                walk(item.children[i]);
            }
        }
        function delegateMount(item) {
            var p = item;
            while (p) {
                if (p.mountPoint !== undefined && p.hasKnownLibrary !== undefined) return p.mountPoint;
                p = p.parent;
            }
            return "";
        }
        walk(page);
        return found;
    }

    function saveScreenshot(page, name) {
        if (!screenshotDir || screenshotDir.length === 0) return;
        var image = grabImage(page);
        image.save(screenshotDir + "/" + name + ".png");
    }

    function fakeEditRegistry(lockedIds) {
        return {
            lockedByOther: lockedIds, calls: [],
            refreshLocks: function() { this.calls.push("refresh"); },
            removeLock: function(id) { this.calls.push("remove:" + id); },
            lockHolder: function(id) { return {hostname: "studio-pc", pid: 4242, startedAtUtc: "2026-09-06T10:00:00Z"}; },
            libraryIdForPath: function(p) { return "lib-main"; },
        };
    }

    // Another instance holds the library's edit lock: every card that
    // would change the library is read-only and explains itself when
    // clicked; Browse stays a plain card.
    function test_readOnlyCardsWhileAnotherInstanceEdits() {
        var page = makePage([makeStick({})], {}, {editRegistry: fakeEditRegistry(["lib-main"])});
        var housekeeping = findCard(page, "/media/MAIN", "Housekeeping");
        var browse = findCard(page, "/media/MAIN", "Browse Library");
        verify(housekeeping !== null && browse !== null);
        compare(housekeeping.readOnly, true);
        compare(browse.readOnly, false);
        compare(findChild(housekeeping, "readOnlyBadge").visible, true);
        compare(findChild(browse, "readOnlyBadge").visible, false);

        var spy = createTemporaryObject(spyComponent, testCase, {target: page, signalName: "duplicateTracksHubRequested"});
        mouseClick(housekeeping);
        compare(spy.count, 0);
        var dialog = findChild(page, "lockedDialog");
        tryCompare(dialog, "opened", true);
        compare(dialog.libraryId, "lib-main");
        verify(findChild(dialog, "holderLabel").text.indexOf("studio-pc") >= 0);
        saveScreenshot(page, "stick-list-read-only");

        findChild(dialog, "removeLockButton").clicked();
        tryCompare(dialog, "opened", false);
        tryCompare(dialog, "visible", false);  // the modal overlay eats clicks until the exit is over
        compare(page.editRegistry.calls.indexOf("remove:lib-main") >= 0, true);

        // No lock: the same card opens the feature.
        page.editRegistry = fakeEditRegistry([]);
        compare(housekeeping.readOnly, false);
        mouseClick(housekeeping);
        compare(spy.count, 1);
    }

    function test_everyMountedStickIsAssessed() {
        var page = makePage([makeStick({}), makeStick({label: "SPARE", mountPoint: "/media/SPARE", devicePath: "/dev/sdc1",
                                                       hasRekordbox: false, hasEngine: false, rekordboxPath: "", enginePath: ""})], {});
        compare(page.backupAdvisor.calls.indexOf("assess:/media/MAIN") >= 0, true);
        compare(page.backupAdvisor.calls.indexOf("assess:/media/SPARE") >= 0, true);
    }

    function test_emptyStickOffersCloneFromThePeer() {
        var spare = makeStick({label: "SPARE", mountPoint: "/media/SPARE", devicePath: "/dev/sdc1",
                               hasRekordbox: false, hasEngine: false, rekordboxPath: "", enginePath: ""});
        var advice = {};
        advice["/media/MAIN"] = makeAdvice({state: "current", detail: "Backup is up to date."});
        advice["/media/SPARE"] = makeAdvice({state: "restore", backupPath: "/b/MAIN.zip", backupLabel: "MAIN",
            detail: "The newest backup can be restored onto this empty stick.",
            cloneSource: {kind: "stick", label: "MAIN", mountPoint: "/media/MAIN", backupPath: "", modifiedAt: "2026-09-06T10:00:00",
                          enoughSpace: true, detail: "Copy MAIN's library onto this stick.",
                          rekordboxPath: "/media/MAIN/PIONEER", enginePath: "/media/MAIN/Engine Library"}});
        var page = makePage([makeStick({}), spare], advice);
        var card = findCard(page, "/media/SPARE", "Create Backup USB Stick");
        verify(card !== null);
        compare(card.visible, true);
        compare(card.enabled, true);
        compare(card.cardSubtitle, "Copy MAIN's library onto this stick.");
        verify(findCard(page, "/media/MAIN", "Create Backup USB Stick").visible === false);
        verify(findCard(page, "/media/MAIN", "Update Stick") === null);

        var spy = createTemporaryObject(spyComponent, testCase, {target: page, signalName: "cloneStickRequested"});
        card.clicked();
        compare(spy.count, 1);
        compare(spy.signalArguments[0][0], "MAIN");
        compare(spy.signalArguments[0][1], "/media/MAIN/PIONEER");
        compare(spy.signalArguments[0][2], "/media/MAIN/Engine Library");
        compare(spy.signalArguments[0][3], "/media/SPARE");
        compare(spy.signalArguments[0][4], "SPARE");
        compare(spy.signalArguments[0][5], false);
        saveScreenshot(page, "stick-list-clone");
    }

    function test_cloneCardDisabledWithoutSpace() {
        var spare = makeStick({label: "SPARE", mountPoint: "/media/SPARE", devicePath: "/dev/sdc1",
                               hasRekordbox: false, hasEngine: false, rekordboxPath: "", enginePath: ""});
        var advice = {};
        advice["/media/SPARE"] = makeAdvice({cloneSource: {kind: "stick", label: "MAIN", mountPoint: "/media/MAIN", backupPath: "",
            modifiedAt: "", enoughSpace: false, detail: "Not enough space on this stick for MAIN's library.",
            rekordboxPath: "/media/MAIN/PIONEER", enginePath: ""}});
        var page = makePage([makeStick({}), spare], advice);
        var card = findCard(page, "/media/SPARE", "Create Backup USB Stick");
        compare(card.visible, true);
        compare(card.enabled, false);
    }

    function test_backupsCardOpensTheHubWithMountAndDevice() {
        var advice = {};
        advice["/media/MAIN"] = makeAdvice({state: "outdated", detail: "The library has changed since its last backup.",
            updateSource: {kind: "stick", label: "SPARE", mountPoint: "/media/SPARE", backupPath: "", modifiedAt: "",
                           enoughSpace: true, detail: "SPARE holds a newer copy of this library.", rekordboxPath: "", enginePath: ""}});
        var page = makePage([makeStick({})], advice);
        var card = findCard(page, "/media/MAIN", "Backups");
        verify(card !== null);
        verify(card.cardSubtitle.indexOf("Newer copy on SPARE") === 0);
        verify(findCard(page, "/media/MAIN", "Update Stick") === null);
        var spy = createTemporaryObject(spyComponent, testCase, {target: page, signalName: "backupsHubRequested"});
        card.clicked();
        compare(spy.count, 1);
        compare(spy.signalArguments[0][0], "MAIN");
        compare(spy.signalArguments[0][3], "/media/MAIN");
        compare(spy.signalArguments[0][4], "/dev/sdb1");
        saveScreenshot(page, "stick-list-update");
    }

    Component {
        id: spyComponent
        SignalSpy {}
    }
}
