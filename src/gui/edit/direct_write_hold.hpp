#pragma once

#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <optional>

namespace seabass::gui
{

// The edit lock a direct write operation (a full stick backup, a
// restore, a clone, a format) holds for its duration. One operation can
// touch more than one library at once (a clone reads the source stick,
// whose archive on disk it updates, and writes the target stick), so
// this holds a list and takes all or none.
//
// GUI thread only, like EditSessionRegistry.
class DirectWriteHold
{
public:
    DirectWriteHold() = default;
    ~DirectWriteHold();
    DirectWriteHold(const DirectWriteHold &) = delete;
    DirectWriteHold &operator=(const DirectWriteHold &) = delete;

    // Why acquire() refused. Locked: another instance holds the library
    // and `holder` says who -- the caller shows the locked dialog.
    // ReadOnly: the library is a stick backup being browsed; the registry
    // has already shown that, and `holder` is empty -- the caller shows
    // nothing. Explicit, rather than "an empty holder means read-only",
    // so the next reason to refuse gets a name instead of a convention.
    struct Refusal
    {
        enum class Kind { Locked, ReadOnly };
        Kind kind = Kind::Locked;
        QVariantMap holder;
        bool isLocked() const { return kind == Kind::Locked; }
    };

    // Takes the edit lock of every id (empty ids are skipped: a blank
    // drive has no library). On the first refusal every lock taken so
    // far is given back and the refusal is returned; nothing is held.
    // `retry` is kept for retryLockedAction().
    std::optional<Refusal> acquire(const QStringList &libraryIds, const QString &stickLabel,
                                   std::function<void()> retry = {});
    void release();
    bool held() const { return !m_held.isEmpty(); }

    // The library id the last acquire() was refused on.
    QString refusedLibraryId() const { return m_refusedLibraryId; }

    // Re-runs the action the last refusal stopped (after the user chose
    // "Remove Lock"). No-op without one.
    void retryLockedAction();

private:
    QStringList m_held;
    QString m_refusedLibraryId;
    std::function<void()> m_retry;
};

}  // namespace seabass::gui
