// MediaController::openFolder(): opening an ordinary directory as a
// library, the entry point that makes a restored stick backup (or any
// copy of a library that is not on removable media) reachable from the
// GUI at all.
//
// Runs the real controller, not a fake: openFolder() calls detect(),
// which runs the platform's real RemovableMediaLocator. That is
// deliberate -- whatever sticks happen to be plugged in, the assertions
// below are about the folder rows this test opened, found by path, so a
// machine with sticks and a machine with none both pass.
#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "scratch_path.hpp"
#include <cstdlib>

#include "gui/media_controller.hpp"
#include "infrastructure/paths/seabass_paths.hpp"

namespace fs = std::filesystem;
using seabass::gui::DetectedStickListModel;
using seabass::gui::MediaController;

namespace
{

void writeFile(const fs::path &path)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << "not a real catalog; scanMountedRoot only checks that it exists";
}

// A directory shaped like a mounted stick root: what a restored backup,
// or a copy of a library on an internal disk, actually looks like.
void makeStickShapedFolder(const fs::path &root, bool rekordbox, bool engine)
{
    fs::create_directories(root);
    if (rekordbox) {
        writeFile(root / "PIONEER" / "rekordbox" / "export.pdb");
    }
    if (engine) {
        writeFile(root / "Engine Library" / "Database2" / "m.db");
    }
}

int rowForMountPoint(const DetectedStickListModel &model, const std::string &mountPoint)
{
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.sticks()[static_cast<size_t>(row)].mountPoint == mountPoint) {
            return row;
        }
    }
    return -1;
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // Never touch the real user's settings: openFolder() persists the
    // opened-folder list through QSettings, and a test that wrote into
    // ~/.config would change what the actual app shows on next launch.
    const fs::path scratch = seabass::testing::scratchRoot() / "open-folder-test";
    fs::remove_all(scratch);
    fs::create_directories(scratch);
    if (std::getenv("SEABASS_HOME") == nullptr) {
        setenv("SEABASS_HOME", (scratch / "home").string().c_str(), 1);
    }
    // Redirects the store by path and format rather than by naming the
    // application: the controller opens QSettings("seabass", "seabass")
    // exactly as the real app does, and setting organizationName here
    // would hide a regression back to a default-constructed QSettings
    // (which resolves elsewhere, because this app sets no organization
    // or application name at all).
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QString::fromStdString((scratch / "settings").string()));
    QSettings::setDefaultFormat(QSettings::IniFormat);

    const fs::path both = scratch / "restored-backup";
    const fs::path rbOnly = scratch / "rekordbox-only";
    const fs::path empty = scratch / "just-a-folder";
    makeStickShapedFolder(both, true, true);
    makeStickShapedFolder(rbOnly, true, false);
    fs::create_directories(empty / "holiday photos");

    // A folder carrying both catalogs opens, and lands in the same list
    // and with the same paths every downstream page already expects.
    {
        MediaController controller;
        assert(controller.openFolder(QString::fromStdString(both.string())).isEmpty());
        const int row = rowForMountPoint(*controller.sticksModel(), both.string());
        assert(row >= 0);
        const auto &stick = controller.sticksModel()->sticks()[static_cast<size_t>(row)];
        assert(stick.isFolder);
        assert(stick.mounted);            // nothing to mount; it is readable now
        assert(stick.devicePath.empty());  // and nothing to eject or format
        assert(stick.rekordboxPath.has_value());
        assert(stick.enginePath.has_value());
        assert(*stick.rekordboxPath == (both / "PIONEER").string());
        assert(*stick.enginePath == (both / "Engine Library").string());
        assert(stick.label == "restored-backup");
        std::cout << "case 1 (folder with both catalogs opens) OK\n";
    }

    // One catalog is enough -- a rekordbox-only export is a normal stick.
    {
        MediaController controller;
        assert(controller.openFolder(QString::fromStdString(rbOnly.string())).isEmpty());
        const int row = rowForMountPoint(*controller.sticksModel(), rbOnly.string());
        assert(row >= 0);
        const auto &stick = controller.sticksModel()->sticks()[static_cast<size_t>(row)];
        assert(stick.rekordboxPath.has_value());
        assert(!stick.enginePath.has_value());
        std::cout << "case 2 (one catalog is enough) OK\n";
    }

    // A folder with no library is refused, with something a person can
    // act on -- and nothing is added to the list.
    {
        MediaController controller;
        const QString message = controller.openFolder(QString::fromStdString(empty.string()));
        assert(!message.isEmpty());
        assert(message.contains("PIONEER"));  // says which folder to pick instead
        assert(rowForMountPoint(*controller.sticksModel(), empty.string()) < 0);
        std::cout << "case 3 (folder with no library refused) OK\n";
    }

    // A path that does not exist at all is refused, not crashed on.
    {
        MediaController controller;
        assert(!controller.openFolder(QStringLiteral("/nonexistent/nowhere")).isEmpty());
        std::cout << "case 4 (missing path refused) OK\n";
    }

    // Opening the same folder twice leaves one row, not two.
    {
        MediaController controller;
        assert(controller.openFolder(QString::fromStdString(both.string())).isEmpty());
        assert(controller.openFolder(QString::fromStdString(both.string())).isEmpty());
        int count = 0;
        for (const auto &stick : controller.sticksModel()->sticks()) {
            count += (stick.mountPoint == both.string()) ? 1 : 0;
        }
        assert(count == 1);
        std::cout << "case 5 (re-opening does not duplicate) OK\n";
    }

    // Opened folders survive a restart: that is the whole point for a
    // restored backup someone comes back to tomorrow.
    {
        {
            MediaController controller;
            assert(controller.openFolder(QString::fromStdString(both.string())).isEmpty());
            assert(controller.openFolder(QString::fromStdString(rbOnly.string())).isEmpty());
        }
        MediaController restarted;
        assert(rowForMountPoint(*restarted.sticksModel(), both.string()) >= 0);
        assert(rowForMountPoint(*restarted.sticksModel(), rbOnly.string()) >= 0);

        // ...and closing one drops it, on disk too, without touching the
        // folder itself.
        restarted.closeFolder(QString::fromStdString(rbOnly.string()));
        assert(rowForMountPoint(*restarted.sticksModel(), rbOnly.string()) < 0);
        assert(fs::exists(rbOnly / "PIONEER" / "rekordbox" / "export.pdb"));

        MediaController afterClose;
        assert(rowForMountPoint(*afterClose.sticksModel(), rbOnly.string()) < 0);
        assert(rowForMountPoint(*afterClose.sticksModel(), both.string()) >= 0);
        std::cout << "case 6 (persisted across restarts, closable) OK\n";
    }

    // A folder whose library was deleted since it was opened stays listed
    // with no library, rather than silently vanishing: it can then be
    // seen and closed.
    {
        const fs::path vanishing = scratch / "goes-away";
        makeStickShapedFolder(vanishing, true, false);
        MediaController controller;
        assert(controller.openFolder(QString::fromStdString(vanishing.string())).isEmpty());
        fs::remove_all(vanishing / "PIONEER");
        controller.detect();
        const int row = rowForMountPoint(*controller.sticksModel(), vanishing.string());
        assert(row >= 0);
        assert(!controller.sticksModel()->sticks()[static_cast<size_t>(row)].rekordboxPath.has_value());
        std::cout << "case 7 (emptied folder stays listed) OK\n";
    }

    // A QML FolderDialog hands over a file:// URL, and that is what the
    // controller receives; it must not be stripped by hand.
    {
        MediaController controller;
        assert(controller.openFolder(QString::fromStdString("file://" + both.string())).isEmpty());
        assert(rowForMountPoint(*controller.sticksModel(), both.string()) >= 0);
        std::cout << "case 7b (file:// URL accepted) OK\n";
    }

    // A row opened with an explicit label keeps it across a restart: a
    // browsed backup's directory is a hash, and its label is the stick.
    {
        {
            MediaController controller;
            assert(controller.openFolder(QString::fromStdString(both.string()), "TOURSTICK").isEmpty());
        }
        MediaController restarted;
        const int row = rowForMountPoint(*restarted.sticksModel(), both.string());
        assert(row >= 0);
        assert(restarted.sticksModel()->sticks()[static_cast<size_t>(row)].label == "TOURSTICK");
        assert(restarted.sticksModel()->sticks()[static_cast<size_t>(row)].identity.label == "TOURSTICK");
        std::cout << "case 7c (label survives a restart) OK\n";
    }

    // A folder carrying the backup marker under the browse cache is
    // flagged read-only on every detect(), from the marker alone -- no
    // archive needed to decide it.
    {
        const fs::path browsed = seabass::infrastructure::paths::localBrowsedBackupsDir() / "browsed";
        makeStickShapedFolder(browsed, true, false);
        {
            std::ofstream marker(browsed / ".seabass-backup-source");
            marker << "/nonexistent/TOURSTICK.zip\n";
        }
        MediaController controller;
        assert(controller.openFolder(QString::fromStdString(browsed.string())).isEmpty());
        int row = rowForMountPoint(*controller.sticksModel(), browsed.string());
        assert(row >= 0);
        assert(controller.sticksModel()->sticks()[static_cast<size_t>(row)].isBrowsedBackup);
        row = rowForMountPoint(*controller.sticksModel(), both.string());
        assert(row < 0 || !controller.sticksModel()->sticks()[static_cast<size_t>(row)].isBrowsedBackup);
        std::cout << "case 7d (marker flags a browsed backup) OK\n";
    }

    // The same marker anywhere else is a stray file: a real stick that
    // was restored from a browse cache must not come up read-only.
    {
        const fs::path stray = scratch / "restored-onto-a-stick";
        makeStickShapedFolder(stray, true, false);
        {
            std::ofstream marker(stray / ".seabass-backup-source");
            marker << "/nonexistent/TOURSTICK.zip\n";
        }
        MediaController controller;
        assert(controller.openFolder(QString::fromStdString(stray.string())).isEmpty());
        const int row = rowForMountPoint(*controller.sticksModel(), stray.string());
        assert(row >= 0);
        assert(!controller.sticksModel()->sticks()[static_cast<size_t>(row)].isBrowsedBackup);
        std::cout << "case 7e (marker outside the browse cache is ignored) OK\n";
    }

    // The id a folder gets is stable for a path and different between
    // paths -- it keys the edit lock, so a collision would let two
    // different libraries be edited under one lock.
    {
        assert(MediaController::folderLibraryId("/a/b") == MediaController::folderLibraryId("/a/b"));
        assert(MediaController::folderLibraryId("/a/b") != MediaController::folderLibraryId("/a/c"));
        assert(MediaController::folderLibraryId("/a/b").rfind("folder-", 0) == 0);
        std::cout << "case 8 (folder library id) OK\n";
    }

    fs::remove_all(scratch);
    std::cout << "open_folder_test: all cases passed\n";
    return 0;
}
