#pragma once

#include <QString>
#include <QStringList>

namespace seabass::gui
{

class SaveContext;

struct ChangeOutcome
{
    bool ok = false;
    QString error;

    static ChangeOutcome success() { return {true, {}}; }
    static ChangeOutcome failure(QString error) { return {false, std::move(error)}; }
};

// One staged edit to a library: exactly what one Save step writes. The
// unit is the user-visible thing the summary counts ("5 of 31 tracks
// written"), which is why a change is one track, one group, one settings
// field, one cue -- never a batch. The save loop applies changes in order
// and checks for cancellation only *between* them, so every change is
// either fully on the stick or still pending, never half-written.
//
// apply() runs on a worker thread with no access to any controller or
// model: everything a change needs travels in by value at construction
// (same rule as every background task in src/gui). Per-save resources
// (backups, the operation log, per-format writers and scratch copies)
// come from the SaveContext.
class PendingChange
{
public:
    virtual ~PendingChange() = default;

    // Stable within a session, e.g. "sync:engine:1234". Staging a change
    // with an id already staged replaces the earlier one.
    virtual QString id() const = 0;
    // One human line for the pending list / tooltip.
    virtual QString description() const = 0;
    // Plural noun for the summary: "tracks", "settings", "groups", "cues".
    virtual QString unit() const = 0;
    // Catalog formats this change writes ("rekordbox", "engine",
    // "onelibrary"), for cache invalidation after the save.
    virtual QStringList formatsTouched() const = 0;

    virtual ChangeOutcome apply(SaveContext &ctx) = 0;
};

}  // namespace seabass::gui
