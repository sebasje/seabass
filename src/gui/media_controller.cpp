#include "media_controller.hpp"

#include <QCoreApplication>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <iterator>
#include <utility>

#include "application/stick_presence_diff.hpp"
#include "infrastructure/media/media_factory.hpp"
#include "gui/future_result.hpp"

namespace seabass::gui
{

namespace
{

// Runs entirely on a background thread (see MediaController::startTask())
// -- no access to the controller itself.
MediaTaskResult runMediaTask(bool mount, QString devicePath)
{
    MediaTaskResult result;
    auto mounter = infrastructure::media::createRemovableMediaMounter();
    std::string error;
    if (mount) {
        result.success = mounter->mount(devicePath.toStdString(), error).has_value();
    } else {
        result.success = mounter->unmount(devicePath.toStdString(), error);
    }
    if (!result.success) {
        result.errorMessage = QString::fromStdString(error);
    }
    return result;
}

}  // namespace

DetectedStickListModel::DetectedStickListModel(QObject *parent) : QAbstractListModel(parent) {}

int DetectedStickListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_sticks.size());
}

QVariant DetectedStickListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= m_sticks.size()) {
        return {};
    }
    const auto &stick = m_sticks[static_cast<size_t>(index.row())];
    switch (role) {
    case LabelRole:
        return QString::fromStdString(stick.label);
    case MountPointRole:
        return QString::fromStdString(stick.mountPoint);
    case DevicePathRole:
        return QString::fromStdString(stick.devicePath);
    case MountedRole:
        return stick.mounted;
    case HasRekordboxRole:
        return stick.rekordboxPath.has_value();
    case HasEngineRole:
        return stick.enginePath.has_value();
    case RekordboxPathRole:
        return stick.rekordboxPath ? QString::fromStdString(*stick.rekordboxPath) : QString();
    case EnginePathRole:
        return stick.enginePath ? QString::fromStdString(*stick.enginePath) : QString();
    case IsSdCardRole:
        return stick.isSdCard;
    case LibraryIdRole:
        return QString::fromStdString(stick.identity.libraryId());
    case HardwareSerialRole:
        return QString::fromStdString(stick.identity.hardwareSerial);
    case IdentityStrengthRole:
        return QString::fromLatin1(application::StickIdentity::strengthName(stick.identity.strength()));
    case CapacityBytesRole:
        return QVariant::fromValue(static_cast<qulonglong>(stick.capacityBytes));
    default:
        return {};
    }
}

QHash<int, QByteArray> DetectedStickListModel::roleNames() const
{
    return {
        {LabelRole, "label"},
        {CapacityBytesRole, "capacityBytes"},
        {MountPointRole, "mountPoint"},
        {DevicePathRole, "devicePath"},
        {MountedRole, "mounted"},
        {HasRekordboxRole, "hasRekordbox"},
        {HasEngineRole, "hasEngine"},
        {RekordboxPathRole, "rekordboxPath"},
        {EnginePathRole, "enginePath"},
        {IsSdCardRole, "isSdCard"},
        {LibraryIdRole, "libraryId"},
        {HardwareSerialRole, "hardwareSerial"},
        {IdentityStrengthRole, "identityStrength"},
    };
}

QVariantMap DetectedStickListModel::get(int row) const
{
    QVariantMap result;
    if (row < 0 || static_cast<size_t>(row) >= m_sticks.size()) {
        return result;
    }
    const auto roles = roleNames();
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
        result.insert(QString::fromUtf8(it.value()), data(index(row), it.key()));
    }
    return result;
}

void DetectedStickListModel::setSticks(std::vector<application::DetectedStick> sticks)
{
    // Sticks with a DJ library Seabass can actually do something with are
    // what the user opened this page for -- put those first (per
    // BRAINSTORM.md's "List sticks / SD cards with DJ libraries first,
    // those without second") rather than making them scroll past plain
    // storage devices. stable_sort so unmounted sticks (whose library
    // status is unknown until mounted) keep udev's own enumeration order
    // relative to each other, not a second, arbitrary reshuffle.
    auto hasKnownLibrary = [](const application::DetectedStick &s) {
        return s.mounted && (s.rekordboxPath.has_value() || s.enginePath.has_value());
    };
    std::stable_sort(sticks.begin(), sticks.end(), [&](const auto &a, const auto &b) {
        return hasKnownLibrary(a) && !hasKnownLibrary(b);
    });

    beginResetModel();
    m_sticks = std::move(sticks);
    endResetModel();
}

MediaController::MediaController(QObject *parent) : QObject(parent)
{
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(500);
    connect(&m_debounceTimer, &QTimer::timeout, this, &MediaController::detect);

    detect();

    m_monitor = infrastructure::media::createRemovableMediaMonitor();
    m_monitor->start([this]() {
        QMetaObject::invokeMethod(this, [this]() { m_debounceTimer.start(); }, Qt::QueuedConnection);
    });

    connect(&m_watcher, &QFutureWatcher<MediaTaskResult>::finished, this, &MediaController::onTaskFinished);
    if (QCoreApplication *app = QCoreApplication::instance()) {
        connect(app, &QCoreApplication::aboutToQuit, this, &MediaController::unmountOwnMounts);
    }
}

MediaController::~MediaController()
{
    if (m_monitor) {
        m_monitor->stop();
    }
    unmountOwnMounts();
}

// Where a just-detected stick is mounted right now.
std::string MediaController::mountPointFor(const application::StickIdentity &identity) const
{
    for (const application::DetectedStick &stick : m_model.sticks()) {
        if (stick.mounted && stick.identity.libraryId() == identity.libraryId()) {
            return stick.mountPoint;
        }
    }
    return {};
}

void MediaController::detect()
{
    auto locator = infrastructure::media::createRemovableMediaLocator();
    m_model.setSticks(locator->detect());
    std::vector<application::StickIdentity> present;
    for (const application::DetectedStick &stick : m_model.sticks()) {
        if (stick.mounted && !stick.mountPoint.empty()) {
            m_lastKnownByMountPoint[stick.mountPoint] = stick.identity;
            if (stick.identity.strength() != application::StickIdentity::Strength::None) {
                present.push_back(stick.identity);
            }
        }
    }
    // Pulled and returned sticks, by identity. A pulled stick stays
    // awaited until the very same one is back, however long that takes;
    // a different stick on the same mount point in the meantime is just
    // a new stick.
    auto diff = application::diffStickPresence(m_presentIdentities, present);
    m_presentIdentities = std::move(present);
    for (const application::StickIdentity &identity : diff.gone) {
        if (!application::containsSameStick(m_awaitedIdentities, identity)) {
            m_awaitedIdentities.push_back(identity);
        }
        emit stickRemoved(QString::fromStdString(identity.libraryId()), QString::fromStdString(identity.label));
    }
    for (const application::StickIdentity &identity : diff.appeared) {
        emit stickAppeared(QString::fromStdString(identity.libraryId()),
                           QString::fromStdString(mountPointFor(identity)));
        auto awaited = application::findAwaited(m_awaitedIdentities, identity);
        if (!awaited) {
            continue;
        }
        m_awaitedIdentities.erase(std::remove_if(m_awaitedIdentities.begin(), m_awaitedIdentities.end(),
                                                 [&](const application::StickIdentity &a) { return a.isSameStick(identity); }),
                                  m_awaitedIdentities.end());
        emit stickReturned(QString::fromStdString(awaited->libraryId()),
                           QString::fromUtf8(application::StickIdentity::strengthName(
                               application::matchStrength(*awaited, identity))));
    }
    queueAutoMounts();
}

QString MediaController::libraryIdForMountPoint(const QString &mountPoint) const
{
    auto identity = lastKnownIdentity(mountPoint.toStdString());
    return identity ? QString::fromStdString(identity->libraryId()) : QString();
}

std::optional<application::StickIdentity> MediaController::lastKnownIdentity(const std::string &mountPoint) const
{
    for (const application::DetectedStick &stick : m_model.sticks()) {
        if (stick.mounted && stick.mountPoint == mountPoint) {
            return stick.identity;
        }
    }
    auto it = m_lastKnownByMountPoint.find(mountPoint);
    if (it != m_lastKnownByMountPoint.end()) {
        return it->second;
    }
    return std::nullopt;
}

// Every stick with a filesystem and no mount point becomes a candidate,
// except one the user ejected here or one that already failed; both
// forget their exemption once the stick is gone, so re-inserting it
// mounts it again.
void MediaController::queueAutoMounts()
{
    QSet<QString> present;
    QSet<QString> queuedOrBusy;
    for (const PendingTask &task : m_taskQueue) {
        queuedOrBusy.insert(task.devicePath);
    }
    if (m_busy) {
        queuedOrBusy.insert(m_busyTask.devicePath);
    }
    for (const application::DetectedStick &stick : m_model.sticks()) {
        const QString devicePath = QString::fromStdString(stick.devicePath);
        if (devicePath.isEmpty()) {
            continue;
        }
        present.insert(devicePath);
        if (stick.mounted || stick.hasNoFilesystem || queuedOrBusy.contains(devicePath)
            || m_userUnmounted.contains(devicePath) || m_autoMountFailed.contains(devicePath)) {
            continue;
        }
        enqueue(devicePath, true, true, false);
    }
    for (QSet<QString> *set : {&m_userUnmounted, &m_autoMountFailed, &m_mountedByUs}) {
        for (auto it = set->begin(); it != set->end();) {
            it = present.contains(*it) ? std::next(it) : set->erase(it);
        }
    }
    m_taskQueue.erase(std::remove_if(m_taskQueue.begin(), m_taskQueue.end(),
                                     [&](const PendingTask &t) { return !present.contains(t.devicePath); }),
                      m_taskQueue.end());
}

void MediaController::mountStick(const QString &devicePath)
{
    m_userUnmounted.remove(devicePath);
    m_autoMountFailed.remove(devicePath);
    enqueue(devicePath, true, false, true);
}

void MediaController::unmountStick(const QString &devicePath)
{
    m_userUnmounted.insert(devicePath);
    enqueue(devicePath, false, false, true);
}

void MediaController::enqueue(const QString &devicePath, bool mount, bool automatic, bool priority)
{
    m_taskQueue.erase(std::remove_if(m_taskQueue.begin(), m_taskQueue.end(),
                                     [&](const PendingTask &t) { return t.devicePath == devicePath; }),
                      m_taskQueue.end());
    const PendingTask task{devicePath, mount, automatic};
    if (priority) {
        m_taskQueue.prepend(task);
    } else {
        m_taskQueue.append(task);
    }
    processQueue();
}

void MediaController::processQueue()
{
    if (m_busy || m_taskQueue.isEmpty()) {
        return;
    }
    startTask(m_taskQueue.takeFirst());
}

void MediaController::startTask(const PendingTask &task)
{
    setErrorMessage({});
    m_busy = true;
    m_busyTask = task;
    emit busyChanged();
    m_watcher.setFuture(QtConcurrent::run(runMediaTask, task.mount, task.devicePath));
}

void MediaController::onTaskFinished()
{
    QString thrown;
    MediaTaskResult result = takeResult(m_watcher, &thrown);
    if (!thrown.isEmpty()) {
        result.errorMessage = thrown;
    }
    const PendingTask task = m_busyTask;
    m_busy = false;
    m_busyTask = {};
    if (task.mount) {
        if (result.success) {
            m_mountedByUs.insert(task.devicePath);
        } else if (task.automatic) {
            m_autoMountFailed.insert(task.devicePath);
        }
    } else if (result.success) {
        m_mountedByUs.remove(task.devicePath);
    }
    setErrorMessage(result.errorMessage);
    emit busyChanged();
    detect();
    processQueue();
}

void MediaController::unmountOwnMounts()
{
    if (m_ownMountsReleased) {
        return;
    }
    m_ownMountsReleased = true;
    m_taskQueue.clear();
    awaitQuietly(m_watcher);
    if (m_mountedByUs.isEmpty()) {
        return;
    }
    // Only what is still mounted: the user may have ejected it meanwhile.
    QSet<QString> stillMounted;
    auto locator = infrastructure::media::createRemovableMediaLocator();
    for (const application::DetectedStick &stick : locator->detect()) {
        if (stick.mounted) {
            stillMounted.insert(QString::fromStdString(stick.devicePath));
        }
    }
    auto mounter = infrastructure::media::createRemovableMediaMounter();
    for (const QString &devicePath : std::as_const(m_mountedByUs)) {
        if (!stillMounted.contains(devicePath)) {
            continue;
        }
        std::string error;
        mounter->unmount(devicePath.toStdString(), error);  // best effort: nothing left to report to
    }
    m_mountedByUs.clear();
}

void MediaController::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

}  // namespace seabass::gui
