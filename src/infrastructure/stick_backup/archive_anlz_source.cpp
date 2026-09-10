#include "infrastructure/stick_backup/archive_anlz_source.hpp"

#include <mutex>
#include <utility>

namespace seabass::infrastructure::stick_backup
{

ArchiveAnlzSource::ArchiveAnlzSource(std::shared_ptr<const Zip64Reader> reader, std::string entryPrefix,
                                       std::shared_ptr<const ArchiveFile> file)
    : m_file(std::move(file)), m_reader(std::move(reader)), m_entryPrefix(std::move(entryPrefix))
{
}

std::optional<std::string> ArchiveAnlzSource::read(const std::string &relativePath)
{
    if (!m_reader) {
        return std::nullopt;
    }
    std::lock_guard<std::mutex> lock(m_readMutex);
    auto index = m_reader->findEntry(m_entryPrefix + relativePath);
    if (!index) {
        // A track whose analysis file is not in the backup reads as "no
        // cues", the same as one whose file is missing off a real stick.
        return std::nullopt;
    }
    return m_reader->readEntryToString(*index);
}

}  // namespace seabass::infrastructure::stick_backup
