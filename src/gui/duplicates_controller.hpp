#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QQmlEngine>

#include <string>
#include <unordered_map>
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

    // waveformsBySourceId is best-effort, precomputed by the caller (only
    // for tracks actually being displayed -- see DuplicatesController::
    // rescan()) since decoding a waveform needs its own file I/O per track.
    void setPlans(std::vector<domain::ConsolidationPlan> plans,
                  std::unordered_map<std::string, std::vector<double>> waveformsBySourceId = {});
    const std::vector<domain::ConsolidationPlan> &plans() const { return m_plans; }

private:
    std::vector<domain::ConsolidationPlan> m_plans;
    std::unordered_map<std::string, std::vector<double>> m_waveformsBySourceId;
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

    // Manual override for a Conflict group: copies sourceTrackId's cues
    // onto every other track in that same group. Unlike applyOne, this
    // works regardless of plan kind -- it's the human decision the domain
    // model defers to when copies disagree (see ConsolidationPlan::Kind::
    // Conflict's doc comment).
    Q_INVOKABLE void copyFromTrack(int index, const QString &sourceTrackId);

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
