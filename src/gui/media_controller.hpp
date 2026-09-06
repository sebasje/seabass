#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QSet>
#include <QStringList>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>

#include <memory>

#include "application/ports/removable_media_locator.hpp"
#include "application/ports/removable_media_monitor.hpp"
#include "application/ports/removable_media_mounter.hpp"

namespace seabass::gui
{

// Read-only Qt list model over the sticks MediaController last detected.
// One row per DetectedStick.
class DetectedStickListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Populated by MediaController; not constructible from QML")

public:
    enum Roles {
        LabelRole = Qt::UserRole + 1,
        MountPointRole,
        DevicePathRole,
        MountedRole,
        HasRekordboxRole,
        HasEngineRole,
        RekordboxPathRole,
        EnginePathRole,
        IsSdCardRole,
    };

    explicit DetectedStickListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // The whole row as a plain object, for QML call sites that need more
    // than one field for a specific row at once (e.g. "the currently
    // selected stick's rekordboxPath and enginePath together") rather
    // than binding a delegate to individual roles. Empty map for an
    // out-of-range row.
    Q_INVOKABLE QVariantMap get(int row) const;

    void setSticks(std::vector<application::DetectedStick> sticks);
    const std::vector<application::DetectedStick> &sticks() const { return m_sticks; }

private:
    std::vector<application::DetectedStick> m_sticks;
};

// Wraps RemovableMediaLocator for QML: detects USB sticks (mounted or not),
// exposed as `sticks`, and auto-refreshes on udev hotplug events via
// RemovableMediaMonitor. Source-selection entry point for every other
// controller.
//
// A stick that shows up with a filesystem but no mount point is mounted
// automatically (at startup and on insertion), so what is on it can be
// read and shown right away instead of every page saying "not mounted".
// Every mount Seabass performed itself -- automatic or via the row's
// mount button -- is remembered and unmounted again when the app quits;
// a stick the desktop had already mounted is left alone. A stick the
// user ejected from the list is not re-mounted until it is re-inserted,
// and one whose mount fails is not retried until then either.
// Result of a background mount/unmount task -- see MediaController::
// mountStick()/unmountStick(). Built entirely on a worker thread, no
// access to the controller itself.
struct MediaTaskResult
{
    bool success = false;
    QString errorMessage;
};

class MediaController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(seabass::gui::DetectedStickListModel *sticks READ sticksModel CONSTANT)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString busyDevicePath READ busyDevicePath NOTIFY busyChanged)

public:
    explicit MediaController(QObject *parent = nullptr);
    ~MediaController() override;

    DetectedStickListModel *sticksModel() { return &m_model; }
    QString errorMessage() const { return m_errorMessage; }
    bool busy() const { return m_busy; }
    // Which stick's devicePath mount/unmount is in flight -- lets a row's
    // own delegate show a spinner instead of the eject icon for just the
    // stick actually being acted on, not every row.
    QString busyDevicePath() const { return m_busyDevicePath; }

    Q_INVOKABLE void detect();

    // Both run the actual mount/unmount (a real syscall/subprocess that
    // can visibly take a moment -- confirmed by this exact freeze once
    // looking like the app had hung or lost the stick) on a background
    // thread rather than blocking the UI thread the way these used to.
    // A no-op while another mount/unmount is already in flight.
    Q_INVOKABLE void mountStick(const QString &devicePath);
    Q_INVOKABLE void unmountStick(const QString &devicePath);

signals:
    void errorMessageChanged();
    void busyChanged();

public:
    // Unmounts every stick Seabass mounted itself that is still mounted
    // (synchronously, best effort). Runs on QCoreApplication::aboutToQuit
    // and again from the destructor; idempotent.
    void unmountOwnMounts();

private:
    void setErrorMessage(const QString &message);
    void startTask(bool mount, const QString &devicePath, bool automatic);
    void onTaskFinished();
    void queueAutoMounts();
    void startNextAutoMount();

    DetectedStickListModel m_model;
    std::unique_ptr<application::RemovableMediaMonitor> m_monitor;
    QTimer m_debounceTimer;
    QString m_errorMessage;
    QFutureWatcher<MediaTaskResult> m_watcher;
    bool m_busy = false;
    bool m_busyIsMount = false;
    bool m_busyIsAutomatic = false;
    QString m_busyDevicePath;
    QSet<QString> m_mountedByUs;      // devicePaths Seabass mounted, to unmount on quit
    QSet<QString> m_userUnmounted;    // ejected from the list: leave alone until re-inserted
    QSet<QString> m_autoMountFailed;  // do not retry until re-inserted
    QStringList m_autoMountQueue;
    bool m_ownMountsReleased = false;
};

}  // namespace seabass::gui
