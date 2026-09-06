#include "media_controller.hpp"

#include <QCoreApplication>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <iterator>
#include <utility>

#include "infrastructure/media/media_factory.hpp"

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
    default:
        return {};
    }
}

QHash<int, QByteArray> DetectedStickListModel::roleNames() const
{
    return {
        {LabelRole, "label"},
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

void MediaController::detect()
{
    auto locator = infrastructure::media::createRemovableMediaLocator();
    m_model.setSticks(locator->detect());
    for (const application::DetectedStick &stick : m_model.sticks()) {
        if (stick.mounted && !stick.mountPoint.empty()) {
            m_lastKnownByMountPoint[stick.mountPoint] = stick.identity;
        }
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
    for (const application::DetectedStick &stick : m_model.sticks()) {
        const QString devicePath = QString::fromStdString(stick.devicePath);
        if (devicePath.isEmpty()) {
            continue;
        }
        present.insert(devicePath);
        if (stick.mounted) {
            continue;
        }
        if (stick.hasNoFilesystem || m_userUnmounted.contains(devicePath) || m_autoMountFailed.contains(devicePath)
            || m_autoMountQueue.contains(devicePath) || (m_busy && m_busyDevicePath == devicePath)) {
            continue;
        }
        m_autoMountQueue.push_back(devicePath);
    }
    for (QSet<QString> *set : {&m_userUnmounted, &m_autoMountFailed, &m_mountedByUs}) {
        for (auto it = set->begin(); it != set->end();) {
            it = present.contains(*it) ? std::next(it) : set->erase(it);
        }
    }
    m_autoMountQueue.erase(std::remove_if(m_autoMountQueue.begin(), m_autoMountQueue.end(),
                                          [&](const QString &d) { return !present.contains(d); }),
                           m_autoMountQueue.end());
    startNextAutoMount();
}

void MediaController::startNextAutoMount()
{
    if (m_busy || m_autoMountQueue.isEmpty()) {
        return;
    }
    const QString devicePath = m_autoMountQueue.takeFirst();
    startTask(true, devicePath, true);
}

void MediaController::mountStick(const QString &devicePath)
{
    m_userUnmounted.remove(devicePath);
    m_autoMountFailed.remove(devicePath);
    startTask(true, devicePath, false);
}

void MediaController::unmountStick(const QString &devicePath)
{
    m_userUnmounted.insert(devicePath);
    m_autoMountQueue.removeAll(devicePath);
    startTask(false, devicePath, false);
}

void MediaController::startTask(bool mount, const QString &devicePath, bool automatic)
{
    if (m_busy) {
        return;
    }
    setErrorMessage({});
    m_busy = true;
    m_busyIsMount = mount;
    m_busyIsAutomatic = automatic;
    m_busyDevicePath = devicePath;
    emit busyChanged();
    m_watcher.setFuture(QtConcurrent::run(runMediaTask, mount, devicePath));
}

void MediaController::onTaskFinished()
{
    MediaTaskResult result = m_watcher.result();
    const QString devicePath = m_busyDevicePath;
    m_busy = false;
    m_busyDevicePath.clear();
    if (m_busyIsMount) {
        if (result.success) {
            m_mountedByUs.insert(devicePath);
        } else if (m_busyIsAutomatic) {
            m_autoMountFailed.insert(devicePath);
        }
    } else if (result.success) {
        m_mountedByUs.remove(devicePath);
    }
    setErrorMessage(result.errorMessage);
    emit busyChanged();
    detect();
}

void MediaController::unmountOwnMounts()
{
    if (m_ownMountsReleased) {
        return;
    }
    m_ownMountsReleased = true;
    m_autoMountQueue.clear();
    m_watcher.waitForFinished();
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
