#include "gui/edit/changes/change_helpers.hpp"

#include <QStringList>

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

}  // namespace seabass::gui
