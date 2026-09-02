#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>

#include <memory>

#include "gui/qt_progress_reporter.hpp"

namespace seabass::gui
{

// Result of a background add-cue task -- see AddCueController::addCue().
// Built entirely on a worker thread, no access to the controller.
struct AddCueResult
{
    QString errorMessage;
    QString statusMessage;
};

// The one genuinely new cue-writing feature in this codebase -- every
// other write path here only ever merges or copies cues that already
// exist somewhere (Clean Up, Sync, Local Cue Backup). Re-scans the
// library fresh right before writing (never trusts whatever cue list the
// calling page had cached) so the augmented cue list handed to the
// existing per-format CueWriter always starts from the track's real
// current state -- same "never trust stale data before a mutating write"
// stance every other controller here already takes.
//
// Position-only for now: no beatgrid-snap option yet (see
// docs/onelibrary-format.md-style reasoning -- rekordbox's beatgrid lives
// in a section of the ANLZ file this project deliberately treats as
// opaque bytes; Engine's is available via libdjinterop but unused here
// too). A later pass can add snapping without touching this controller's
// write path at all, only how the position it's given gets computed.
//
// Handles all three catalogs as primary write targets, including
// OneLibrary -- but OneLibrary goes through OneLibraryCueWriter directly
// rather than the application::CueWriter dispatch rekordbox/Engine share,
// since that writer deliberately isn't a CueWriter (it keys by file path,
// not sourceId -- content_id is a separate id space from export.pdb's
// track id). See the .cpp for the branch.
//
// Hot loops (isLoop) are Engine-only: LibdjinteropEngineCueWriter writes
// them through libdjinterop's own tested loop API, but RekordboxCueWriter
// can't -- AnlzCueCodec's own doc comment marks loop encoding out of
// scope pending real hardware verification of the still-uncertain raw
// byte fields (see anlz_cue_codec.hpp). Refused outright there rather
// than silently written as a plain point cue, which would quietly
// discard the loop-out a DJ asked to save.
class AddCueController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool writing READ writing NOTIFY writingChanged)
    Q_PROPERTY(int scanCurrent READ scanCurrent NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanProgressChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit AddCueController(QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    bool writing() const { return m_writing; }
    int scanCurrent() const { return m_scanCurrent; }
    int scanTotal() const { return m_scanTotal; }
    QString errorMessage() const { return m_errorMessage; }
    QString statusMessage() const { return m_statusMessage; }

    // kind is "hot" or "memory"; hotCueNumber is ignored for "memory".
    // color is "#RRGGBB" or empty (writer picks its own default). isLoop
    // is only honored for format == "engine" and kind == "hot" -- see
    // the .cpp for why rekordbox and OneLibrary loop writes are refused
    // rather than silently downgraded to a point cue.
    Q_INVOKABLE void addCue(const QString &format, const QString &path, const QString &sourceId, double positionMs,
                             const QString &kind, int hotCueNumber, const QString &color, const QString &comment,
                             bool isLoop, double loopEndMs);

signals:
    void busyChanged();
    void writingChanged();
    void scanProgressChanged();
    void errorMessageChanged();
    void statusMessageChanged();

private:
    void onTaskFinished();
    void setBusy(bool busy);
    void setWriting(bool writing);
    void setScanProgress(int current, int total);
    void setErrorMessage(const QString &message);
    void setStatusMessage(const QString &message);
    std::shared_ptr<QtProgressReporter> makeReporter();

    QFutureWatcher<AddCueResult> m_watcher;
    bool m_busy = false;
    bool m_writing = false;
    int m_scanCurrent = 0;
    int m_scanTotal = 0;
    QString m_errorMessage;
    QString m_statusMessage;
};

}  // namespace seabass::gui
