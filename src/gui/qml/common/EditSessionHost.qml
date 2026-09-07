import QtQuick
import QtQuick.Controls
import SeabassGui

// Everything an edit page needs from edit mode, in one item placed over
// the page's content: the floating Save button, the leave guard, write
// progress, the summary, and the lock refusal. The page keeps its own
// layout and only routes its Back/Home through requestLeave().
//
//   EditSessionHost {
//       id: editHost
//       anchors.fill: parent
//       libraryId: EditSessionRegistry.libraryIdForPath(root.rekordboxPath || root.enginePath)
//       stickLabel: root.stickLabel
//       rekordboxPath: root.rekordboxPath; enginePath: root.enginePath
//   }
//   BackBreadcrumb { onBackRequested: editHost.requestLeave(() => root.StackView.view.pop()) ... }
//
// Opens the session for the page's lifetime (no lock is taken by that;
// the first staged change takes it) and closes it on destruction.
// `registry` and the session are untyped so tests can pass fakes.
Item {
    id: host
    // The C++ singleton in the app; tests (whose QML module has no C++
    // types) pass a fake, hence the typeof guard.
    property var registry: typeof EditSessionRegistry !== "undefined" ? EditSessionRegistry : null
    required property string libraryId
    property string stickLabel: ""
    property string rekordboxPath: ""
    property string enginePath: ""
    // Which editing page this is, matching PendingChange::owner(). One
    // library is edited by one page at a time; while another page holds
    // unsaved changes this one is read-only, and its own staging attempts
    // are refused in C++ regardless of what the UI does.
    property string feature: ""
    readonly property var session: internal.session
    readonly property string editorOwner: internal.session !== null && internal.session.editorOwner !== undefined
        ? internal.session.editorOwner : ""
    readonly property bool blockedByOtherPage: host.feature.length > 0 && host.editorOwner.length > 0
        && host.editorOwner !== host.feature
    readonly property bool dirty: internal.session !== null && internal.session.dirty === true
    readonly property bool writing: internal.session !== null && internal.session.writing === true

    z: 900  // above page content, below BusyOverlay (1000): a scan's scrim covers the Save button

    QtObject {
        id: internal
        property var session: null
        property var pendingLeave: null
        property bool leaveAfterSave: false

        function runPendingLeave() {
            var fn = internal.pendingLeave;
            internal.pendingLeave = null;
            internal.leaveAfterSave = false;
            if (fn) {
                fn();
            }
        }
    }

    Component.onCompleted: {
        if (host.libraryId.length > 0 && host.registry) {
            internal.session = host.registry.openSession(host.libraryId, host.stickLabel, host.rekordboxPath,
                                                         host.enginePath);
        }
    }
    Component.onDestruction: {
        if (internal.session !== null && host.registry) {
            host.registry.closeSession(host.libraryId);
        }
    }

    // Leave the page via leaveFn unless there is something to decide
    // first. Ignored while writing (the breadcrumb is disabled then too).
    function requestLeave(leaveFn) {
        if (host.writing) {
            return;
        }
        if (host.dirty) {
            internal.pendingLeave = leaveFn;
            unsavedDialog.open();
            return;
        }
        leaveFn();
    }

    SaveOverlayButton {
        objectName: "saveOverlay"
        session: host.session
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
    }

    UnsavedChangesDialog {
        id: unsavedDialog
        objectName: "unsavedDialog"
        pendingCount: host.session ? host.session.pendingCount : 0
        onSaveRequested: {
            internal.leaveAfterSave = true;
            host.session.save();
        }
        onDiscardRequested: {
            host.session.discard();
            internal.runPendingLeave();
        }
    }

    WriteProgressDialog {
        objectName: "writeProgressDialog"
        session: host.session
    }

    OperationSummaryDialog {
        id: summaryDialog
        objectName: "summaryDialog"
        onAccepted: {
            // Leaving was the goal and everything landed: go. Otherwise
            // (a plain Save, or a cancelled/failed one) stay on the page
            // with whatever is still staged.
            if (internal.leaveAfterSave && host.session && host.session.dirty !== true) {
                internal.runPendingLeave();
            } else {
                internal.pendingLeave = null;
                internal.leaveAfterSave = false;
            }
        }
    }

    LockedLibraryDialog {
        id: lockedDialog
        objectName: "lockedDialog"
        onRemoveLockRequested: host.registry.removeLock(host.libraryId)
    }

    // A second page tried to stage into a library another page is already
    // editing. Not a lock dialog: the other page is in this same window,
    // and the way out is to finish or discard there.
    Dialog {
        id: otherPageDialog
        objectName: "otherPageDialog"
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.NoAutoClose
        width: 520
        title: "Another page is editing this library"
        property string ownerName: ""
        footer: DialogButtonBox {
            Button {
                objectName: "understoodButton"
                text: "Understood"
                highlighted: true
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
        Label {
            objectName: "messageLabel"
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.text
            text: "You have unsaved changes on " + otherPageDialog.ownerName + " for this stick. Save or discard "
                + "them there before editing the same library from here.\n\nNothing was changed."
        }
    }

    Connections {
        target: host.session
        ignoreUnknownSignals: true
        function onSaveFinished(summary) {
            // On quit the window shows its own summary (see Main.qml).
            if (!(host.registry && host.registry.quitAfterSave === true)) {
                summaryDialog.show(summary);
            }
        }
        function onLockRefused(holder) {
            lockedDialog.openFor(host.libraryId, holder);
        }
        function onEditorConflict(owner, attempted) {
            otherPageDialog.ownerName = host.pageNameFor(owner);
            otherPageDialog.open();
        }
    }

    // PendingChange::owner() values, as the user knows the pages.
    function pageNameFor(owner) {
        switch (owner) {
        case "settings": return "Device Profile";
        case "sync": return "Sync Cue Points";
        case "library-health": return "Library Health";
        case "cleanup": return "Clean Up Duplicates";
        case "dup": return "Duplicate Stats & Sync";
        case "localcue": return "Local Cue Backup";
        case "addcue": return "Browse Library";
        default: return "another page";
        }
    }
}
