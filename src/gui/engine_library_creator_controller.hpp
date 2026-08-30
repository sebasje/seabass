#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>

#include <memory>

#include "gui/qt_progress_reporter.hpp"

namespace seabass::gui
{

// Result of the background creation task, see
// EngineLibraryCreatorController::create(). Built entirely on a worker
// thread, no access to the controller.
struct EngineLibraryCreationTaskResult
{
    int tracksCreated = 0;
    int tracksSkipped = 0;
    int cuesCopied = 0;
    QString errorMessage;  // empty on success
};

// Wraps infrastructure::engine::EngineLibraryCreator for QML. See that
// class's own doc comment for exactly what is and isn't carried over
// from the rekordbox source, and docs/experimental-features.md for why
// this whole feature is gated as experimental: it's the first thing in
// this codebase that fabricates an entire new database from scratch
// rather than modifying an existing, already-recognized one, and real
// Denon hardware's tolerance for a library this project created (as
// opposed to one Engine DJ or a player itself created) is genuinely
// unverified until tested against real units.
class EngineLibraryCreatorController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int scanCurrent READ scanCurrent NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanProgressChanged)
    // "Scanning rekordbox" while reading, then "Creating Engine Library"
    // while writing -- both real, ticking phases, not a static message,
    // so a library of any real size doesn't look indistinguishable from
    // a hang (see EngineLibraryCreator::create()'s own progress-reporter
    // parameter for why the write side in particular needs this).
    Q_PROPERTY(QString currentPhase READ currentPhase NOTIFY currentPhaseChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit EngineLibraryCreatorController(QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    int scanCurrent() const { return m_scanCurrent; }
    int scanTotal() const { return m_scanTotal; }
    QString currentPhase() const { return m_currentPhase; }
    QString errorMessage() const { return m_errorMessage; }
    QString statusMessage() const { return m_statusMessage; }

    // rekordboxPath: the stick's PIONEER root, scanned fresh (so the
    // library created reflects the current state of the rekordbox
    // export, not a stale earlier scan). schemaGeneration: 0=V1, 1=V2,
    // 2=V3 -- see infrastructure::engine::EngineSchemaGeneration; exposed
    // as a plain int since QML enums would need their own registration
    // for a value used nowhere else in this app.
    Q_INVOKABLE void create(const QString &rekordboxPath, int schemaGeneration);

signals:
    void busyChanged();
    void scanProgressChanged();
    void currentPhaseChanged();
    void errorMessageChanged();
    void statusMessageChanged();

private:
    void onCreateFinished();
    void setBusy(bool busy);
    void setScanProgress(int current, int total);
    void setCurrentPhase(const QString &phase);
    void setErrorMessage(const QString &message);
    void setStatusMessage(const QString &message);
    std::shared_ptr<QtProgressReporter> makeReporter();

    QFutureWatcher<EngineLibraryCreationTaskResult> m_watcher;
    bool m_busy = false;
    int m_scanCurrent = 0;
    int m_scanTotal = 0;
    QString m_currentPhase;
    QString m_errorMessage;
    QString m_statusMessage;
};

}  // namespace seabass::gui
