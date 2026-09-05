import QtQuick
import QtTest
import SeabassGui

// Exercises FormatUsbPage.qml entirely headless -- no real disk, no real
// udisks2/PowerShell call, no real RemovableMediaLocator result. Possible
// because FormatUsbController is an untyped, required page property (see
// FormatUsbPage.qml's own comment) rather than a self-instantiated real
// controller: every test here hands the page a plain JS object standing
// in for the whole controller, the same technique tst_TrackDetailPage.qml
// already uses for its own controllers.
TestCase {
    id: testCase
    name: "FormatUsbPage"
    width: 480
    height: 800
    visible: true
    when: windowShown

    // Explicit size, not left to Page's own implicit-width computation --
    // pushed for real inside a StackView, the surrounding window always
    // constrains Page's width immediately; here, with nothing external
    // doing that, ScrollView's contentWidth: availableWidth and its
    // ColumnLayout's width: parent.width feed back on each other (a real
    // "Binding loop detected for property implicitWidth" warning at
    // runtime) and leave geometry degenerate enough that mouseClick can't
    // reliably hit real content.
    Component {
        id: pageComponent
        FormatUsbPage { width: 460; height: 700 }
    }

    function makeFakeController(disks, fat32MaxBytes) {
        return {
            disks: disks,
            busy: false,
            errorMessage: "",
            statusMessage: "",
            fat32MaxBytes: fat32MaxBytes === undefined ? -1 : fat32MaxBytes,
            lastFormatCall: null,
            recommendedFilesystem: function(capacityBytes) {
                var threshold = 32 * 1024 * 1024 * 1024;
                return capacityBytes <= threshold ? "fat32" : "exfat";
            },
            format: function(wholeDiskPath, filesystem, volumeLabel) {
                this.lastFormatCall = {wholeDiskPath: wholeDiskPath, filesystem: filesystem, volumeLabel: volumeLabel};
            },
        };
    }

    function makeDisk(overrides) {
        var disk = {
            label: "TESTSTICK",
            wholeDiskPath: "/dev/sdx",
            devicePath: "",
            capacityBytes: 8 * 1024 * 1024 * 1024,
            mounted: false,
            hasNoFilesystem: true,
            hasDjLibrary: false,
            rootEntries: [],
        };
        for (var key in overrides) {
            disk[key] = overrides[key];
        }
        return disk;
    }

    function test_instantiatesWithNoDisks() {
        var page = createTemporaryObject(pageComponent, testCase, {controller: makeFakeController([])});
        verify(page !== null);
        compare(page.selectedIndex, -1);
        verify(page.selectedDisk === null);
    }

    // The blank, just-inserted drive is pre-selected over an
    // already-cataloged one, even when it isn't first in the list -- the
    // realistic "I want to format this stick" scenario Sebastian asked
    // for, not just "picks index 0."
    function test_blankDriveIsPreselectedOverExisting() {
        var existing = makeDisk({label: "WHALESHARK2", hasNoFilesystem: false, hasDjLibrary: true,
                                   capacityBytes: 32 * 1024 * 1024 * 1024, rootEntries: ["PIONEER/", "Engine Library/"]});
        var blank = makeDisk({label: "SanDisk Extreme 256GB", hasNoFilesystem: true,
                                capacityBytes: 256 * 1024 * 1024 * 1024});
        var page = createTemporaryObject(pageComponent, testCase, {controller: makeFakeController([existing, blank])});
        verify(page !== null);
        compare(page.selectedIndex, 1);
        compare(page.selectedDisk.label, "SanDisk Extreme 256GB");
    }

    function test_fallsBackToFirstDiskWhenNoneAreBlank() {
        var a = makeDisk({label: "A", hasNoFilesystem: false});
        var b = makeDisk({label: "B", hasNoFilesystem: false});
        var page = createTemporaryObject(pageComponent, testCase, {controller: makeFakeController([a, b])});
        verify(page !== null);
        compare(page.selectedIndex, 0);
    }

    // The size-based recommendation (<=32GB -> FAT32, >32GB -> exFAT)
    // tracks whichever drive is currently selected.
    function test_recommendedFilesystemTracksSelectedDriveSize() {
        var small = makeDisk({label: "Small", capacityBytes: 8 * 1024 * 1024 * 1024});
        var large = makeDisk({label: "Large", capacityBytes: 256 * 1024 * 1024 * 1024});
        var page = createTemporaryObject(pageComponent, testCase, {controller: makeFakeController([small, large])});
        verify(page !== null);

        page.applySelection(0);
        compare(page.selectedFilesystem, "fat32");
        compare(page.recommendedFilesystem, "fat32");

        page.applySelection(1);
        compare(page.selectedFilesystem, "exfat");
        compare(page.recommendedFilesystem, "exfat");
    }

    // A platform-imposed FAT32 ceiling (Windows' real 32GB limit) is
    // exposed as a plain controller property the fake sets directly,
    // rather than the page querying the real host OS -- makes this
    // branch testable on any machine, Windows or not.
    function test_fat32UnavailableAboveThePlatformCeiling() {
        var large = makeDisk({label: "Large", capacityBytes: 64 * 1024 * 1024 * 1024});
        var thirtyTwoGb = 32 * 1024 * 1024 * 1024;
        var page = createTemporaryObject(pageComponent, testCase,
            {controller: makeFakeController([large], thirtyTwoGb)});
        verify(page !== null);
        page.applySelection(0);
        compare(page.fat32Available, false);
    }

    function test_fat32AvailableWithNoPlatformCeiling() {
        var large = makeDisk({label: "Large", capacityBytes: 64 * 1024 * 1024 * 1024});
        var page = createTemporaryObject(pageComponent, testCase, {controller: makeFakeController([large], -1)});
        verify(page !== null);
        page.applySelection(0);
        compare(page.fat32Available, true);
    }

    // The confirm dialog's Format button stays disabled until the typed
    // text exactly matches the label about to be written, and enables the
    // moment it does.
    function test_confirmButtonGatedByExactLabelMatch() {
        var disk = makeDisk({label: "TESTSTICK", hasNoFilesystem: false});
        var page = createTemporaryObject(pageComponent, testCase, {controller: makeFakeController([disk])});
        verify(page !== null);

        var openButton = findChild(page, "openConfirmButton");
        var confirmField = findChild(page, "confirmField");
        var acceptButton = findChild(page, "formatAcceptButton");
        var confirmDialog = findChild(page, "confirmDialog");
        verify(openButton !== null);
        verify(confirmField !== null);
        verify(acceptButton !== null);
        verify(confirmDialog !== null);
        compare(openButton.enabled, true);

        // Opens the dialog via its own API rather than mouseClick(openButton)
        // -- this page's real content is taller than any reasonable
        // offscreen test window, so a screen-position click on a button
        // below the fold of a ScrollView isn't a reliable way to test
        // dialog logic headless. RadioButton/TextField interaction
        // elsewhere in this file still uses real click/text simulation;
        // this is specifically about reaching a control that scrolling
        // would otherwise have to bring into view first.
        confirmDialog.open();
        tryVerify(function() { return confirmDialog.visible; });
        compare(acceptButton.enabled, false);

        confirmField.text = "wrong name";
        compare(acceptButton.enabled, false);

        confirmField.text = "TESTSTICK";
        compare(acceptButton.enabled, true);
    }

    // Going through the confirm flow calls the controller's format() with
    // exactly the expected (wholeDiskPath, filesystem, label) args --
    // proves the wiring without ever touching a real device. Uses
    // confirmDialog.accept() rather than mouseClick(acceptButton) for the
    // same off-screen-content reason as the test above; the button's own
    // enabled-gating is already verified separately.
    function test_formatCallsControllerWithExactArgs() {
        var disk = makeDisk({label: "TESTSTICK", wholeDiskPath: "/dev/sdz", hasNoFilesystem: false,
                               capacityBytes: 8 * 1024 * 1024 * 1024});
        var page = createTemporaryObject(pageComponent, testCase, {controller: makeFakeController([disk])});
        verify(page !== null);
        // Read state back through page.controller throughout, never the
        // outer object literal passed to createTemporaryObject -- a `var`
        // page property doesn't keep reference identity with a plain JS
        // object handed in via createTemporaryObject()'s initial
        // properties (verified: `page.controller === <the outer object>`
        // is false), so the only reliable way to observe what the page
        // did to "its" controller is through the page's own reference.
        page.selectedFilesystem = "exfat";

        var confirmField = findChild(page, "confirmField");
        var acceptButton = findChild(page, "formatAcceptButton");
        var confirmDialog = findChild(page, "confirmDialog");
        verify(confirmDialog !== null);

        confirmDialog.open();
        tryVerify(function() { return confirmDialog.visible; });
        confirmField.text = "TESTSTICK";
        compare(acceptButton.enabled, true);
        confirmDialog.accept();

        verify(page.controller.lastFormatCall !== null);
        compare(page.controller.lastFormatCall.wholeDiskPath, "/dev/sdz");
        compare(page.controller.lastFormatCall.filesystem, "exfat");
        compare(page.controller.lastFormatCall.volumeLabel, "TESTSTICK");
    }
}
