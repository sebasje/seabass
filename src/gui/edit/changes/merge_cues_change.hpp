#pragma once

#include <QString>
#include <QStringList>

#include "domain/local_restore.hpp"
#include "gui/edit/pending_change.hpp"

namespace seabass::gui
{

// One restore candidate: the cues a local backup holds that the stick's
// track is missing, merged onto it. The candidate carries the complete
// cue list to end up with, not just the additions.
class MergeCuesChange : public PendingChange
{
public:
    MergeCuesChange(QString format, QString path, domain::RestoreCandidate candidate);

    QString id() const override;
    QString description() const override;
    QString unit() const override;
    QStringList formatsTouched() const override;
    ChangeOutcome apply(SaveContext &ctx) override;

private:
    QString m_format;
    QString m_path;
    domain::RestoreCandidate m_candidate;
};

}  // namespace seabass::gui
