import QtQuick
import QtTest
import SeabassGui

// Every page in the SeabassGui module must at least compile as a
// component. A page nobody instantiates in a test can otherwise ship
// with a structural QML error (a dialog body that landed inside a
// Connections block was found this way) that only the real app hits.
TestCase {
    id: testCase
    name: "PagesCompile"

    readonly property var pages: [
        "StickListPage", "DuplicatesHubPage", "BackupsHubPage", "ScanPage", "TrackDetailPage",
        "DuplicatesPage", "CleanupPage", "PendingDeletionsPage", "JunkCuePage", "LibraryConsistencyPage",
        "StickStatisticsPage", "EngineLibraryCreatorPage", "SyncPage", "BackupsPage", "LocalCuePage",
        "AboutPage", "DonationPage", "SettingsPage", "AppSettingsPage", "AnonymizeLibraryPage",
        "MatchingPage", "FormatUsbPage", "StickBackupPage", "RestoreStickBackupPage", "CloneStickPage",
        "Main",
    ]

    function test_everyPageCompiles() {
        var failures = [];
        for (var i = 0; i < pages.length; ++i) {
            var component = Qt.createComponent("qrc:/qt/qml/SeabassGui/src/gui/qml/" + pages[i] + ".qml");
            if (component.status === Component.Error) {
                failures.push(pages[i] + ": " + component.errorString());
            }
            component.destroy();
        }
        compare(failures.length, 0, failures.join("\n"));
    }
}
