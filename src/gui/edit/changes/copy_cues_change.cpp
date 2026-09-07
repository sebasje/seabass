#include "gui/edit/changes/copy_cues_change.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "application/ports/cue_writer.hpp"
#include "gui/edit/save_context.hpp"
#include "gui/onelibrary_cue_writer_adapter.hpp"
#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/rekordbox/pdb_lookup.hpp"
#include "infrastructure/rekordbox/rekordbox_cue_writer.hpp"

namespace seabass::gui
{

namespace fs = std::filesystem;

namespace
{

// One format's cue writer plus the rule for which files a given track's
// write needs backed up first. rekordbox names a different file per
// track (its analysis file); the database formats name one file for all.
struct DuplicatesFormatContext
{
    std::unique_ptr<application::CueWriter> writer;
    std::function<std::vector<std::string>(const std::string &)> filesToBackUpFor;
};

// oneLibrarySourceIdToPath is only consulted when format == "onelibrary"
// -- built by the caller from the actual tracks about to be written
// (OneLibraryCueWriterAdapter's own comment explains why sourceId alone
// isn't enough for this format).
std::unique_ptr<DuplicatesFormatContext> makeContext(
    const QString &format, const QString &path,
    const std::unordered_map<std::string, std::string> &oneLibrarySourceIdToPath)
{
    auto ctx = std::make_unique<DuplicatesFormatContext>();
    if (format == "rekordbox") {
        std::string pioneerRoot = path.toStdString();
        ctx->writer = std::make_unique<infrastructure::rekordbox::RekordboxCueWriter>(pioneerRoot);
        ctx->filesToBackUpFor = [pioneerRoot](const std::string &trackSourceId) -> std::vector<std::string> {
            auto analyzePath = infrastructure::rekordbox::findAnlzPathForTrackId(
                pioneerRoot, static_cast<uint32_t>(std::stoul(trackSourceId)));
            if (!analyzePath) {
                return {};
            }
            return {infrastructure::rekordbox::extAnlzPath(pioneerRoot, *analyzePath)};
        };
    } else if (format == "onelibrary") {
        std::string pioneerRoot = path.toStdString();  // same PIONEER root rekordbox uses, see scan()'s own comment
        ctx->writer = std::make_unique<OneLibraryCueWriterAdapter>(pioneerRoot, oneLibrarySourceIdToPath);
        std::string dbFile = infrastructure::onelibrary::OneLibraryCueWriter::dbPathFor(pioneerRoot);
        ctx->filesToBackUpFor = [dbFile](const std::string &) -> std::vector<std::string> { return {dbFile}; };
    } else {
        std::string engineLibraryPath = path.toStdString();
        ctx->writer = std::make_unique<infrastructure::engine::LibdjinteropEngineCueWriter>(engineLibraryPath);
        std::string engineDbFile = (fs::path(engineLibraryPath) / "Database2" / "m.db").string();
        ctx->filesToBackUpFor = [engineDbFile](const std::string &) -> std::vector<std::string> {
            return {engineDbFile};
        };
    }
    return ctx;
}

}  // namespace

CopyCuesChange::CopyCuesChange(QString format, QString path, QString groupKey, DuplicatesCopyOp op)
    : m_format(std::move(format)), m_path(std::move(path)), m_groupKey(std::move(groupKey)), m_op(std::move(op))
{
}

QString CopyCuesChange::id() const
{
    return "dup:" + m_format + ":" + m_groupKey;
}

QString CopyCuesChange::description() const
{
    QString from = QString::fromStdString(m_op.source.filename.empty() ? m_op.source.sourceId : m_op.source.filename);
    QString title = QString::fromStdString(m_op.source.title);
    return QStringLiteral("Copy %1 cue(s) from %2 onto %3 other copy/copies of \"%4\"")
        .arg(m_op.source.cues.size())
        .arg(from)
        .arg(m_op.targets.size())
        .arg(title);
}

QString CopyCuesChange::unit() const
{
    return QStringLiteral("groups");
}

QStringList CopyCuesChange::formatsTouched() const
{
    return {m_format};
}

ChangeOutcome CopyCuesChange::apply(SaveContext &ctx)
{
    std::string key = "dup:" + m_format.toStdString();
    std::unordered_map<std::string, std::string> oneLibrarySourceIdToPath;
    if (m_format == "onelibrary") {
        oneLibrarySourceIdToPath[m_op.source.sourceId] = m_op.source.filePath;
        for (const auto &target : m_op.targets) {
            oneLibrarySourceIdToPath[target.sourceId] = target.filePath;
        }
        key += ":" + m_groupKey.toStdString();  // the adapter is bound to this group's tracks
    }
    DuplicatesFormatContext &format = ctx.shared<DuplicatesFormatContext>(
        key, [&]() { return makeContext(m_format, m_path, oneLibrarySourceIdToPath); });

    for (const auto &target : m_op.targets) {
        for (const auto &file : format.filesToBackUpFor(target.sourceId)) {
            ctx.backupOnce(file, "duplicate-cue-consolidation");
        }
        format.writer->writeHotCues(target.sourceId, m_op.source.cues);
        ctx.log().record("copied " + std::to_string(m_op.source.cues.size()) + " cue(s) from track id="
                         + m_op.source.sourceId + " to track id=" + target.sourceId);
    }
    return ChangeOutcome::success();
}

}  // namespace seabass::gui
