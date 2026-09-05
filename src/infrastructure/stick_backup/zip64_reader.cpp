#include "infrastructure/stick_backup/zip64_reader.hpp"

#include <zlib.h>

#include <algorithm>
#include <utility>

#include "infrastructure/stick_backup/zip_format.hpp"

namespace seabass::infrastructure::stick_backup
{

using namespace zip;

namespace
{

std::vector<std::byte> readRange(const ArchiveFile &file, std::uint64_t offset, std::size_t length)
{
    std::vector<std::byte> out(length);
    file.readAt(offset, out);
    return out;
}

struct ParsedExtras
{
    std::optional<std::uint64_t> uncompressedSize;
    std::optional<std::uint64_t> compressedSize;
    std::optional<std::uint64_t> localHeaderOffset;
    std::optional<std::int64_t> mtimeUnix;
};

// Walks the extra-field blocks of one central-directory entry. The ZIP64
// block only carries the fields whose 32-bit counterparts are 0xFFFFFFFF,
// in a fixed order, so the caller says which ones to expect.
ParsedExtras parseExtras(std::span<const std::byte> extra, bool expectSize, bool expectOffset)
{
    ParsedExtras result;
    std::size_t pos = 0;
    while (pos + 4 <= extra.size()) {
        std::uint16_t id = readU16(extra, pos, "extra field header");
        std::uint16_t length = readU16(extra, pos + 2, "extra field header");
        pos += 4;
        requireRange(extra, pos, length, "extra field body");
        std::span<const std::byte> body = extra.subspan(pos, length);
        if (id == Zip64ExtraId) {
            std::size_t at = 0;
            if (expectSize) {
                result.uncompressedSize = readU64(body, at, "zip64 uncompressed size");
                result.compressedSize = readU64(body, at + 8, "zip64 compressed size");
                at += 16;
            }
            if (expectOffset) {
                result.localHeaderOffset = readU64(body, at, "zip64 local header offset");
            }
        } else if (id == ExtendedTimestampExtraId) {
            if (length >= 5 && (std::to_integer<unsigned>(body[0]) & 0x01) != 0) {
                result.mtimeUnix = static_cast<std::int32_t>(readU32(body, 1, "extended timestamp"));
            }
        }
        pos += length;
    }
    return result;
}

}  // namespace

Zip64Reader::Zip64Reader(const ArchiveFile &file, ArchiveLayout layout, std::vector<CentralEntry> entries)
    : m_file(&file), m_layout(std::move(layout)), m_entries(std::move(entries)), m_dataOffsets(m_entries.size())
{
    m_indexByName.reserve(m_entries.size());
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        m_indexByName.emplace(m_entries[i].name, i);
    }
}

std::optional<Zip64Reader> Zip64Reader::tryOpen(const ArchiveFile &file, std::string *error)
{
    try {
        return open(file);
    } catch (const ArchiveFormatError &e) {
        if (error != nullptr) {
            *error = e.what();
        }
        return std::nullopt;
    }
}

Zip64Reader Zip64Reader::open(const ArchiveFile &file)
{
    const std::uint64_t fileSize = file.size();
    if (fileSize < EndOfCentralDirectorySize) {
        throw ArchiveFormatError("file too small to be an archive");
    }

    // The EOCD is the last thing in the file; we never write a comment,
    // so it must end exactly at EOF. Tolerate a comment (scan backward)
    // only as far as the format allows, and insist the comment length
    // agrees with the distance to EOF -- that is what rejects a stray
    // signature inside entry data.
    const std::size_t tailLength =
        static_cast<std::size_t>(std::min<std::uint64_t>(fileSize, EndOfCentralDirectorySize + MaxCommentLength));
    const std::uint64_t tailStart = fileSize - tailLength;
    std::vector<std::byte> tail = readRange(file, tailStart, tailLength);

    std::optional<std::size_t> eocdInTail;
    for (std::size_t i = tailLength - EndOfCentralDirectorySize + 1; i-- > 0;) {
        if (readU32(tail, i) == EndOfCentralDirectorySignature
            && readU16(tail, i + 20) == tailLength - (i + EndOfCentralDirectorySize)) {
            eocdInTail = i;
            break;
        }
    }
    if (!eocdInTail) {
        throw ArchiveFormatError("no end-of-central-directory record");
    }
    ArchiveLayout layout;
    layout.fileSize = fileSize;
    layout.endOfCentralDirectoryOffset = tailStart + *eocdInTail;

    std::uint64_t entryCount = readU16(tail, *eocdInTail + 10);
    std::uint64_t centralDirectorySize = readU32(tail, *eocdInTail + 12);
    std::uint64_t centralDirectoryOffset = readU32(tail, *eocdInTail + 16);
    if (readU16(tail, *eocdInTail + 4) != 0 || readU16(tail, *eocdInTail + 6) != 0) {
        throw ArchiveFormatError("multi-disk archives are not supported");
    }

    if (layout.endOfCentralDirectoryOffset >= Zip64LocatorSize) {
        std::vector<std::byte> locator = readRange(file, layout.endOfCentralDirectoryOffset - Zip64LocatorSize, Zip64LocatorSize);
        if (readU32(locator, 0) == Zip64LocatorSignature) {
            layout.zip64LocatorOffset = layout.endOfCentralDirectoryOffset - Zip64LocatorSize;
            if (readU32(locator, 4) != 0 || readU32(locator, 16) != 1) {
                throw ArchiveFormatError("multi-disk zip64 archives are not supported");
            }
            std::uint64_t zip64EocdOffset = readU64(locator, 8);
            if (zip64EocdOffset + Zip64EndOfCentralDirectorySize > layout.zip64LocatorOffset) {
                throw ArchiveFormatError("zip64 end-of-central-directory offset out of range");
            }
            std::vector<std::byte> zip64Eocd = readRange(file, zip64EocdOffset, Zip64EndOfCentralDirectorySize);
            if (readU32(zip64Eocd, 0) != Zip64EndOfCentralDirectorySignature) {
                throw ArchiveFormatError("bad zip64 end-of-central-directory signature");
            }
            if (readU32(zip64Eocd, 16) != 0 || readU32(zip64Eocd, 20) != 0) {
                throw ArchiveFormatError("multi-disk zip64 archives are not supported");
            }
            layout.zip64EndOfCentralDirectoryOffset = zip64EocdOffset;
            entryCount = readU64(zip64Eocd, 32);
            centralDirectorySize = readU64(zip64Eocd, 40);
            centralDirectoryOffset = readU64(zip64Eocd, 48);
        }
    }
    if (layout.zip64EndOfCentralDirectoryOffset == 0 && layout.zip64LocatorOffset == 0) {
        if (entryCount == Max16 || centralDirectorySize == Max32 || centralDirectoryOffset == Max32) {
            throw ArchiveFormatError("zip64 markers present but no zip64 record");
        }
        layout.zip64EndOfCentralDirectoryOffset = layout.endOfCentralDirectoryOffset;
        layout.zip64LocatorOffset = layout.endOfCentralDirectoryOffset;
    }

    const std::uint64_t centralDirectoryEnd = layout.zip64EndOfCentralDirectoryOffset;
    if (centralDirectoryOffset > centralDirectoryEnd || centralDirectorySize > centralDirectoryEnd - centralDirectoryOffset) {
        throw ArchiveFormatError("central directory out of range");
    }
    if (centralDirectorySize < entryCount * CentralDirectoryEntrySize) {
        throw ArchiveFormatError("central directory too small for its entry count");
    }
    layout.centralDirectoryOffset = centralDirectoryOffset;
    layout.centralDirectorySize = centralDirectorySize;
    layout.entryCount = entryCount;

    std::vector<std::byte> directory = readRange(file, centralDirectoryOffset, static_cast<std::size_t>(centralDirectorySize));
    std::vector<CentralEntry> entries;
    entries.reserve(static_cast<std::size_t>(entryCount));
    std::size_t pos = 0;
    for (std::uint64_t n = 0; n < entryCount; ++n) {
        if (readU32(directory, pos, "central directory entry") != CentralDirectorySignature) {
            throw ArchiveFormatError("bad central directory entry signature");
        }
        std::uint16_t flags = readU16(directory, pos + 8);
        std::uint16_t method = readU16(directory, pos + 10);
        DosDateTime stamp{readU16(directory, pos + 14), readU16(directory, pos + 12)};
        std::uint32_t crc = readU32(directory, pos + 16);
        std::uint32_t compressed32 = readU32(directory, pos + 20);
        std::uint32_t uncompressed32 = readU32(directory, pos + 24);
        std::uint16_t nameLength = readU16(directory, pos + 28);
        std::uint16_t extraLength = readU16(directory, pos + 30);
        std::uint16_t commentLength = readU16(directory, pos + 32);
        std::uint32_t externalAttributes = readU32(directory, pos + 38);
        std::uint32_t offset32 = readU32(directory, pos + 42);
        pos += CentralDirectoryEntrySize;

        requireRange(directory, pos, static_cast<std::size_t>(nameLength) + extraLength + commentLength, "central directory entry");
        std::string name(reinterpret_cast<const char *>(directory.data() + pos), nameLength);
        std::span<const std::byte> extra(directory.data() + pos + nameLength, extraLength);
        pos += static_cast<std::size_t>(nameLength) + extraLength + commentLength;

        if (name.empty()) {
            throw ArchiveFormatError("central directory entry with an empty name");
        }
        if (method != MethodStore) {
            throw ArchiveFormatError("unsupported compression method in " + name);
        }
        const bool expectSize = uncompressed32 == Max32 || compressed32 == Max32;
        ParsedExtras extras = parseExtras(extra, expectSize, offset32 == Max32);

        CentralEntry entry;
        entry.name = std::move(name);
        entry.size = expectSize ? extras.uncompressedSize.value_or(Max32) : uncompressed32;
        std::uint64_t compressedSize = expectSize ? extras.compressedSize.value_or(Max32) : compressed32;
        if (compressedSize != entry.size) {
            throw ArchiveFormatError("stored entry with mismatched sizes: " + entry.name);
        }
        entry.localHeaderOffset = offset32 == Max32 ? extras.localHeaderOffset.value_or(Max32) : offset32;
        if (entry.localHeaderOffset + LocalFileHeaderSize > centralDirectoryOffset) {
            throw ArchiveFormatError("entry offset past the central directory: " + entry.name);
        }
        entry.crc32 = crc;
        entry.mtimeUnix = extras.mtimeUnix.value_or(unixFromDosDateTime(stamp));
        entry.isDirectory = entry.name.back() == '/' || (externalAttributes & 0x10u) != 0;
        entry.hasDataDescriptor = (flags & FlagDataDescriptor) != 0;
        entries.push_back(std::move(entry));
    }
    if (pos != centralDirectorySize) {
        throw ArchiveFormatError("central directory size does not match its entries");
    }

    return Zip64Reader(file, layout, std::move(entries));
}

std::optional<std::size_t> Zip64Reader::findEntry(std::string_view name) const
{
    auto it = m_indexByName.find(std::string(name));
    if (it == m_indexByName.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::uint64_t Zip64Reader::dataOffset(std::size_t index) const
{
    const CentralEntry &entry = m_entries.at(index);
    if (m_dataOffsets[index]) {
        return *m_dataOffsets[index];
    }
    std::vector<std::byte> header = readRange(*m_file, entry.localHeaderOffset, LocalFileHeaderSize);
    if (readU32(header, 0) != LocalFileHeaderSignature) {
        throw ArchiveFormatError("bad local header signature for " + entry.name);
    }
    std::uint16_t nameLength = readU16(header, 26);
    std::uint16_t extraLength = readU16(header, 28);
    std::uint64_t offset = entry.localHeaderOffset + LocalFileHeaderSize + nameLength + extraLength;
    if (offset > m_layout.centralDirectoryOffset || entry.size > m_layout.centralDirectoryOffset - offset) {
        throw ArchiveFormatError("entry data runs into the central directory: " + entry.name);
    }
    m_dataOffsets[index] = offset;
    return offset;
}

std::uint64_t Zip64Reader::footprint(std::size_t index) const
{
    const CentralEntry &entry = m_entries.at(index);
    std::uint64_t headerBytes = dataOffset(index) - entry.localHeaderOffset;
    return headerBytes + entry.size + (entry.hasDataDescriptor ? DataDescriptorSize : 0);
}

void Zip64Reader::readEntry(std::size_t index, const std::function<void(std::span<const std::byte>)> &sink,
                            std::size_t chunkSize) const
{
    const CentralEntry &entry = m_entries.at(index);
    std::uint64_t offset = dataOffset(index);
    std::uint64_t remaining = entry.size;
    std::vector<std::byte> buffer(static_cast<std::size_t>(std::min<std::uint64_t>(chunkSize, std::max<std::uint64_t>(remaining, 1))));
    while (remaining > 0) {
        std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
        std::span<std::byte> piece(buffer.data(), take);
        m_file->readAt(offset, piece);
        sink(piece);
        offset += take;
        remaining -= take;
    }
}

std::string Zip64Reader::readEntryToString(std::size_t index) const
{
    std::string out;
    out.reserve(static_cast<std::size_t>(m_entries.at(index).size));
    readEntry(index, [&out](std::span<const std::byte> piece) {
        out.append(reinterpret_cast<const char *>(piece.data()), piece.size());
    });
    return out;
}

bool Zip64Reader::verifyCrc(std::size_t index) const
{
    std::uint32_t crc = 0;
    readEntry(index, [&crc](std::span<const std::byte> piece) {
        crc = static_cast<std::uint32_t>(
            ::crc32(crc, reinterpret_cast<const Bytef *>(piece.data()), static_cast<uInt>(piece.size())));
    });
    return crc == m_entries.at(index).crc32;
}

}  // namespace seabass::infrastructure::stick_backup
