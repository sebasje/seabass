#pragma once

#include <memory>
#include <optional>
#include <string>

#include "infrastructure/rekordbox/anlz_byte_source.hpp"
#include "infrastructure/stick_backup/archive_file.hpp"
#include "infrastructure/stick_backup/zip64_reader.hpp"

namespace seabass::infrastructure::stick_backup
{

// Serves a browsed stick backup's analysis files straight out of the
// archive: one findEntry() plus one read (inflating if the entry was
// deflated), no extraction. See rekordbox::AnlzByteSource for why ANLZ is
// worth reading this way when the databases are not -- it is ~99% of a
// library's metadata bytes and is wanted one track at a time.
//
// Holds the Zip64Reader (and so the ArchiveFile behind it) for its whole
// life: the reader has already validated every offset and count against
// the file size, so entry lookups here are pure arithmetic on that
// already-checked central directory.
//
// Thread-safety matches ArchiveFile's: a scan reads tracks on one
// background thread at a time, which is how LibraryCatalogCache already
// serializes a catalog's readers.
class ArchiveAnlzSource : public rekordbox::AnlzByteSource
{
public:
    // `entryPrefix` is what the stick's PIONEER folder is called inside
    // the archive -- "PIONEER/" for a backup taken from a stick root.
    // Passed in rather than assumed so a backup of a differently-nested
    // tree stays readable.
    ArchiveAnlzSource(std::shared_ptr<const Zip64Reader> reader, std::string entryPrefix);

    std::optional<std::string> read(const std::string &relativePath) override;

private:
    std::shared_ptr<const Zip64Reader> m_reader;
    std::string m_entryPrefix;
};

}  // namespace seabass::infrastructure::stick_backup
