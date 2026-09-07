#include "gui/edit/changes/change_helpers.hpp"

#include <QStringList>

#include <memory>

#include "gui/edit/save_context.hpp"

namespace seabass::gui
{

QString issueFormat(const domain::LibraryConsistencyIssue &issue)
{
    if (issue.survivor) {
        return QString::fromStdString(issue.survivor->format);
    }
    if (!issue.brokenGroup.empty()) {
        return QString::fromStdString(issue.brokenGroup.front().format);
    }
    return {};
}

QString issueKeyFor(const domain::LibraryConsistencyIssue &issue)
{
    QStringList ids;
    if (issue.survivor) {
        ids << QString::fromStdString(issue.survivor->sourceId);
    }
    for (const auto &broken : issue.brokenGroup) {
        ids << QString::fromStdString(broken.sourceId);
    }
    return issueFormat(issue) + ":" + ids.join('+');
}

QString junkKeyFor(const domain::Track &track)
{
    return QString::fromStdString(track.format) + ":" + QString::fromStdString(track.sourceId);
}

QString describeCues(const std::vector<domain::CuePoint> &cues)
{
    int hot = 0;
    int memory = 0;
    for (const auto &cue : cues) {
        (cue.kind == domain::CuePoint::Kind::Hot ? hot : memory)++;
    }
    QString result = QString("%1 hot").arg(hot);
    if (memory > 0) {
        result += QString(", %1 memory (not written - Engine writer only handles hot cues)").arg(memory);
    }
    return result;
}

// Holds the index plus whether building it failed, so a catalog that
// cannot be read is not retried once per item.
namespace
{
struct SharedAnlzIndex
{
    std::unique_ptr<infrastructure::rekordbox::AnlzPathIndex> index;
};
}  // namespace

const infrastructure::rekordbox::AnlzPathIndex *sharedAnlzPathIndex(SaveContext &ctx, const QString &pioneerRoot)
{
    SharedAnlzIndex &shared = ctx.shared<SharedAnlzIndex>(
        "anlz-index:" + pioneerRoot.toStdString(), [&]() {
            auto holder = std::make_unique<SharedAnlzIndex>();
            try {
                holder->index = std::make_unique<infrastructure::rekordbox::AnlzPathIndex>(pioneerRoot.toStdString());
            } catch (const std::exception &e) {
                // Not fatal: every caller falls back to looking one id up
                // at a time, which is what it did before this existed.
                ctx.log().record(std::string("could not index analysis paths, falling back to per-track lookups: ")
                                 + e.what());
            }
            return holder;
        });
    return shared.index.get();
}

}  // namespace seabass::gui
