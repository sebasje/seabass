import QtQuick
import QtTest
import SeabassGui

// RestoreStickBackupPage.qml headless with a fake controller: drive
// selection rules, the analyze/restore calls it makes, and the confirm
// dialog's type-to-confirm gating.
TestCase {
    id: testCase
    name: "RestoreStickBackupPage"
    width: 900
    height: 900
    visible: true
    when: windowShown

    Component {
        id: pageComponent
        RestoreStickBackupPage { width: 880; height: 880 }
    }

    function makeDisk(overrides) {
        var disk = {
            label: "STICK",
            mountPoint: "/media/STICK",
            devicePath: "/dev/sdb1",
            wholeDiskPath: "/dev/sdb",
            capacityBytes: 64 * 1024 * 1024 * 1024,
            mounted: true,
            hasNoFilesystem: false,
            hasDjLibrary: false,
            usable: true,
            rootEntries: [],
        };
        for (var key in overrides) {
            disk[key] = overrides[key];
        }
        return disk;
    }

    function makeFakeController(disks, overrides) {
        var c = {
            disks: disks,
            archivePath: "/home/u/Seabass Backups/STICK.zip",
            defaultBackupDirectory: "/home/u/Seabass Backups",
            archiveInfo: {error: "", label: "STICK", identifier: "uuid", status: "complete",
                          createdAt: "2026-09-03T21:14:00", entries: 1161, bytes: 25 * 1024 * 1024 * 1024, rejectedCount: 0},
            preview: {filesToWrite: 14, filesUnchanged: 1147, bytesToWrite: 500 * 1024 * 1024, extras: 0,
                      targetHasEngineLibrary: false, freeBytes: 60 * 1024 * 1024 * 1024, enoughFreeSpace: true},
            result: {},
            busy: false,
            restoring: false,
            analyzing: false,
            phase: "",
            filesDone: 0,
            filesTotal: 0,
            bytesDone: 0,
            bytesTotal: 0,
            currentFile: "",
            errorMessage: "",
            statusMessage: "",
            analyzeCalls: [],
            lastRestore: null,
            refresh: function() {},
            analyze: function(mountPoint) { this.analyzeCalls.push(mountPoint); },
            restore: function(mountPoint, exact) { this.lastRestore = {mountPoint: mountPoint, exact: exact}; },
            cancel: function() {},
            archivePathForLabel: function(label) { return this.defaultBackupDirectory + "/" + label + ".zip"; },
        };
        for (var key in overrides) {
            c[key] = overrides[key];
        }
        return c;
    }

    function makePage(disks, controllerOverrides, pageProps) {
        var props = {controller: makeFakeController(disks, controllerOverrides || {})};
        for (var key in (pageProps || {})) {
            props[key] = pageProps[key];
        }
        return createTemporaryObject(pageComponent, testCase, props);
    }

    // The first *usable* drive is preselected, not merely the first one:
    // an unmounted or unformatted drive cannot receive a restore.
    function test_preselectsFirstUsableDriveAndAnalyzesIt() {
        var unmounted = makeDisk({label: "OLD", mountPoint: "", mounted: false, usable: false});
        var blank = makeDisk({label: "NEW", mountPoint: "", hasNoFilesystem: true, usable: false});
        var good = makeDisk({label: "STICK"});
        var page = makePage([unmounted, blank, good]);
        verify(page !== null);
        compare(page.selectedIndex, 2);
        compare(page.controller.analyzeCalls.length, 1);
        compare(page.controller.analyzeCalls[0], "/media/STICK");
    }

    function test_preselectedMountPointWinsAndArchiveIsTakenOver() {
        var a = makeDisk({label: "A", mountPoint: "/media/A"});
        var b = makeDisk({label: "B", mountPoint: "/media/B"});
        var page = makePage([a, b], {}, {preselectedMountPoint: "/media/B", preselectedArchivePath: "/x/B.zip"});
        compare(page.selectedIndex, 1);
        compare(page.controller.archivePath, "/x/B.zip");
        compare(page.controller.analyzeCalls[0], "/media/B");
    }

    function test_labelOnlyPreselectionDerivesTheArchivePath() {
        var page = makePage([makeDisk({})], {}, {preselectedLabel: "WHALESHARK2"});
        compare(page.controller.archivePath, "/home/u/Seabass Backups/WHALESHARK2.zip");
    }

    function test_restoreDisabledUntilAnalyzed() {
        var page = makePage([makeDisk({})], {preview: {}});
        compare(findChild(page, "openConfirmButton").enabled, false);
        var analyzed = makePage([makeDisk({})]);
        compare(findChild(analyzed, "openConfirmButton").enabled, true);
    }

    function test_restoreDisabledWithoutFreeSpace() {
        var page = makePage([makeDisk({})], {preview: {filesToWrite: 1, filesUnchanged: 0, bytesToWrite: 10, extras: 0,
                                                        targetHasEngineLibrary: false, freeBytes: 1, enoughFreeSpace: false}});
        compare(findChild(page, "openConfirmButton").enabled, false);
    }

    // A blank target with nothing at stake: no typing required.
    function test_confirmOnBlankTargetNeedsNoTypingAndCallsRestore() {
        var page = makePage([makeDisk({})]);
        var dialog = findChild(page, "confirmDialog");
        dialog.open();
        tryVerify(function() { return dialog.visible; });
        compare(findChild(page, "confirmField").visible, false);
        compare(findChild(page, "restoreAcceptButton").enabled, true);
        dialog.accept();
        verify(page.controller.lastRestore !== null);
        compare(page.controller.lastRestore.mountPoint, "/media/STICK");
        compare(page.controller.lastRestore.exact, false);
    }

    // A target that already holds a DJ library: the exact label must be
    // typed, like Format USB Stick.
    function test_confirmOnLibraryTargetRequiresTypedLabel() {
        var page = makePage([makeDisk({hasDjLibrary: true})]);
        var dialog = findChild(page, "confirmDialog");
        var accept = findChild(page, "restoreAcceptButton");
        var field = findChild(page, "confirmField");
        dialog.open();
        tryVerify(function() { return dialog.visible; });
        compare(field.visible, true);
        compare(accept.enabled, false);
        field.text = "stick";
        compare(accept.enabled, false);
        field.text = "STICK";
        compare(accept.enabled, true);
    }

    function test_exactRestorePassesTheFlagAndRequiresTyping() {
        var page = makePage([makeDisk({})]);
        var exact = findChild(page, "exactCheckBox");
        exact.toggle();
        compare(page.exact, true);
        var dialog = findChild(page, "confirmDialog");
        dialog.open();
        tryVerify(function() { return dialog.visible; });
        compare(findChild(page, "restoreAcceptButton").enabled, false);
        findChild(page, "confirmField").text = "STICK";
        dialog.accept();
        compare(page.controller.lastRestore.exact, true);
    }

    function test_busyControllerDisablesRestore() {
        var page = makePage([makeDisk({})], {busy: true, restoring: true, phase: "writing", filesDone: 3, filesTotal: 14,
                                             bytesDone: 100, bytesTotal: 1000});
        compare(findChild(page, "openConfirmButton").enabled, false);
        compare(findChild(page, "cancelRestoreButton").visible, true);
    }
}
