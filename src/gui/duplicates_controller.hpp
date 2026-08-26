#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QQmlEngine>

#include <vector>

#include "domain/duplicate_cue_consolidation.hpp"

namespace djconvert::gui
{

// Read-only Qt list model over the consolidation plans DuplicatesController
// last computed. Only Unambiguous (fixable) and Conflict (informational)
// plans are exposed -- NoCues/AlreadyConsistent groups need no attention,
// same filtering cli/main.cpp's handleDuplicates applies.
class ConsolidationPlanListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Populated by DuplicatesController; not constructible from QML")

public:
    enum Roles {
        KindRole = Qt::UserRole + 1,
        FilenameRole,
        DescriptionRole,
        ActionableRole,
        TracksRole,
    };

    explicit ConsolidationPlanListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setPlans(std::vector<domain::ConsolidationPlan> plans);
    const std::vector<domain::ConsolidationPlan> &plans() const { return m_plans; }

private:
    std::vector<domain::ConsolidationPlan> m_plans;
};

// Wraps ConsolidateDuplicateCues for QML: scans a library, finds duplicate
// tracks, and (for Unambiguous groups) can copy cues from the one copy that
// has them onto the others -- backing up first, mirroring cli/main.cpp's
// handleDuplicates wiring exactly so GUI and CLI behave identically.
class DuplicatesController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(djconvert::gui::ConsolidationPlanListModel *plans READ plansModel CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit DuplicatesController(QObject *parent = nullptr);

    ConsolidationPlanListModel *plansModel() { return &m_model; }
    bool busy() const { return m_busy; }
    QString errorMessage() const { return m_errorMessage; }
    QString statusMessage() const { return m_statusMessage; }

    // format is "rekordbox" or "engine"; path is the corresponding
    // DetectedStick.rekordboxPath / .enginePath.
    Q_INVOKABLE void scan(const QString &format, const QString &path);
    Q_INVOKABLE void applyOne(int index);
    Q_INVOKABLE void applyAllUnambiguous();

signals:
    void busyChanged();
    void errorMessageChanged();
    void statusMessageChanged();

private:
    void rescan();
    void setBusy(bool busy);
    void setErrorMessage(const QString &message);
    void setStatusMessage(const QString &message);

    ConsolidationPlanListModel m_model;
    QString m_format;
    QString m_path;
    bool m_busy = false;
    QString m_errorMessage;
    QString m_statusMessage;
};

}  // namespace djconvert::gui
