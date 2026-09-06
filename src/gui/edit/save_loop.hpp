#pragma once

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

#include "gui/edit/pending_change.hpp"
#include "gui/edit/save_context.hpp"

namespace seabass::gui
{

struct SaveLoopResult
{
    QStringList appliedIds;  // in order; a change here is fully on the stick
    QString failedId;        // the change that failed, if any (it and everything after it stay pending)
    QString error;           // empty unless a change or a finish hook failed
    bool cancelled = false;  // stopped between two changes on request
    std::vector<UndoableBackup> backups;
};

// The one save loop every session runs (worker thread): applies changes
// in order, checks the token between them, runs the finish hooks, and
// says exactly which changes landed. A finish-hook failure (a scratch
// copy that could not be committed) reports every change as still
// pending -- nothing it covered reached the stick -- so a retry re-applies
// them; the per-item writers are idempotent.
SaveLoopResult runSaveLoop(const std::vector<std::shared_ptr<PendingChange>> &changes, SaveContext &ctx);

}  // namespace seabass::gui
