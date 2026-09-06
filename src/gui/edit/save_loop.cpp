#include "gui/edit/save_loop.hpp"

#include <exception>

namespace seabass::gui
{

SaveLoopResult runSaveLoop(const std::vector<std::shared_ptr<PendingChange>> &changes, SaveContext &ctx)
{
    SaveLoopResult result;
    ctx.progress().start("Saving changes", changes.size());
    size_t done = 0;
    for (const auto &change : changes) {
        if (ctx.cancel().cancelled()) {
            result.cancelled = true;
            break;
        }
        ctx.status(change->description());
        ChangeOutcome outcome;
        try {
            outcome = change->apply(ctx);
        } catch (const std::exception &e) {
            outcome = ChangeOutcome::failure(QString::fromStdString(e.what()));
        }
        if (!outcome.ok) {
            result.failedId = change->id();
            result.error = outcome.error.isEmpty() ? QStringLiteral("failed") : outcome.error;
            break;
        }
        result.appliedIds << change->id();
        ctx.progress().tick(++done);
    }

    ctx.status(QStringLiteral("Finishing"));
    // ok means "the whole batch went through"; a cancel or a failure hands
    // the hooks false so a scratch copy commits only what completed.
    if (auto hookError = ctx.runFinishHooks(result.error.isEmpty() && !result.cancelled)) {
        // Whatever the hooks were committing did not land: report every
        // change as still pending rather than guess which did.
        result.appliedIds.clear();
        if (result.error.isEmpty()) {
            result.error = *hookError;
        }
    }
    ctx.progress().finish();
    result.backups = ctx.takeBackups();
    return result;
}

}  // namespace seabass::gui
