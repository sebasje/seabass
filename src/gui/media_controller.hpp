#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>

#include <memory>

#include "application/ports/removable_media_locator.hpp"
#include "application/ports/removable_media_monitor.hpp"
#include "application/ports/removable_media_mounter.hpp"

namespace djconvert::gui
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
    };

    explicit DetectedStickListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setSticks(std::vector<application::DetectedStick> sticks);

private:
    std::vector<application::DetectedStick> m_sticks;
};

// Wraps RemovableMediaLocator for QML: detects USB sticks (mounted or not),
// exposed as `sticks`, and auto-refreshes on udev hotplug events via
// RemovableMediaMonitor. Source-selection entry point for every other
// controller.
class MediaController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(djconvert::gui::DetectedStickListModel *sticks READ sticksModel CONSTANT)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    explicit MediaController(QObject *parent = nullptr);
    ~MediaController() override;

    DetectedStickListModel *sticksModel() { return &m_model; }
    QString errorMessage() const { return m_errorMessage; }

    Q_INVOKABLE void detect();
    Q_INVOKABLE bool mountStick(const QString &devicePath);
    Q_INVOKABLE bool unmountStick(const QString &devicePath);

signals:
    void errorMessageChanged();

private:
    void setErrorMessage(const QString &message);

    DetectedStickListModel m_model;
    std::unique_ptr<application::RemovableMediaMonitor> m_monitor;
    QTimer m_debounceTimer;
    QString m_errorMessage;
};

}  // namespace djconvert::gui
