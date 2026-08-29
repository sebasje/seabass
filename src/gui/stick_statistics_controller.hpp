#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>

namespace djconvert::gui
{

// Result of the background scan task, see
// StickStatisticsController::scan(). Built entirely on a worker thread,
// no access to the controller.
struct StickStatisticsScanResult
{
    QVariantMap filesystemInfo;    // filesystem/capacity/USB facts, see the .cpp for the exact keys
    QVariantMap rekordboxStats;    // empty map if rekordboxPath wasn't given
    QVariantMap engineStats;       // empty map if enginePath wasn't given
    QVariantMap oneLibraryStats;   // empty map if this stick has no OneLibrary export
    QVariantMap diskUsage;         // {totalBytes, usedBytes, freeBytes, root: {label, sizeBytes, children:[...]}}
    QVariantMap benchmarkSamples;  // {databaseFiles: [...], audioFiles: [...]}, fed to runBenchmark() later
    QString errorMessage;
};

// Read-only: this page never writes anything to the stick, so unlike
// LibraryConsistencyController there's no write lock, no backup, no
// rekordbox-running refusal -- the same "just reads" contract as
// ScanController (Browse Library).
//
// A full scan (library stats + disk usage) and a speed benchmark are
// deliberately separate operations (scan()/runBenchmark()): the
// benchmark does real timed I/O against the stick and its result is
// meaningful to persist, so it shouldn't silently re-run every time the
// page happens to rescan.
class StickStatisticsController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QVariantMap filesystemInfo READ filesystemInfo NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap rekordboxStats READ rekordboxStats NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap engineStats READ engineStats NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap oneLibraryStats READ oneLibraryStats NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap diskUsage READ diskUsage NOTIFY resultsChanged)
    Q_PROPERTY(bool benchmarkRunning READ benchmarkRunning NOTIFY benchmarkRunningChanged)
    Q_PROPERTY(QString benchmarkErrorMessage READ benchmarkErrorMessage NOTIFY benchmarkErrorMessageChanged)
    Q_PROPERTY(QVariantList benchmarkHistory READ benchmarkHistory NOTIFY benchmarkHistoryChanged)

public:
    explicit StickStatisticsController(QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString errorMessage() const { return m_errorMessage; }
    QVariantMap filesystemInfo() const { return m_filesystemInfo; }
    QVariantMap rekordboxStats() const { return m_rekordboxStats; }
    QVariantMap engineStats() const { return m_engineStats; }
    QVariantMap oneLibraryStats() const { return m_oneLibraryStats; }
    QVariantMap diskUsage() const { return m_diskUsage; }
    bool benchmarkRunning() const { return m_benchmarkRunning; }
    QString benchmarkErrorMessage() const { return m_benchmarkErrorMessage; }
    QVariantList benchmarkHistory() const { return m_benchmarkHistory; }

    // rekordboxPath/enginePath: empty for a catalog not present on this
    // stick, same convention as every other controller in this app.
    Q_INVOKABLE void scan(const QString &stickLabel, const QString &rekordboxPath, const QString &enginePath);

    // Runs the real timed read-speed benchmark against the sample files
    // the last scan() picked out, then persists + reloads the history for
    // this stick. A no-op if scan() hasn't completed successfully yet.
    Q_INVOKABLE void runBenchmark();

signals:
    void busyChanged();
    void errorMessageChanged();
    void resultsChanged();
    void benchmarkRunningChanged();
    void benchmarkErrorMessageChanged();
    void benchmarkHistoryChanged();

private:
    void onScanFinished();
    void onBenchmarkFinished();
    void setBusy(bool busy);
    void setErrorMessage(const QString &message);
    void setBenchmarkRunning(bool running);
    void setBenchmarkErrorMessage(const QString &message);
    void loadBenchmarkHistory();

    QFutureWatcher<StickStatisticsScanResult> m_watcher;
    QFutureWatcher<QVariantMap> m_benchmarkWatcher;

    bool m_busy = false;
    QString m_errorMessage;
    QVariantMap m_filesystemInfo;
    QVariantMap m_rekordboxStats;
    QVariantMap m_engineStats;
    QVariantMap m_oneLibraryStats;
    QVariantMap m_diskUsage;
    QVariantMap m_benchmarkSamples;  // carried from the last scan(), consumed by runBenchmark()

    bool m_benchmarkRunning = false;
    QString m_benchmarkErrorMessage;
    QVariantList m_benchmarkHistory;
};

}  // namespace djconvert::gui
