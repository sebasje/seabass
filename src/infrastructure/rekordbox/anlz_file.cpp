#include "infrastructure/rekordbox/anlz_file.hpp"

#include <fstream>
#include <stdexcept>

#include "infrastructure/rekordbox/big_endian.hpp"

namespace djconvert::infrastructure::rekordbox
{

AnlzFile AnlzFile::readRaw(const std::string &path)
{
    std::ifstream ifs(path, std::ifstream::binary);
    if (!ifs.is_open()) {
        throw std::runtime_error("could not open " + path);
    }
    std::string data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    if (data.size() < 12 || data.substr(0, 4) != "PMAI") {
        throw std::runtime_error(path + " is not an ANLZ file (bad magic)");
    }

    uint32_t lenHeader = readU32BE(data, 4);
    uint32_t lenFile = readU32BE(data, 8);
    if (lenFile != data.size()) {
        throw std::runtime_error(path + ": len_file (" + std::to_string(lenFile) +
                                  ") doesn't match actual file size (" + std::to_string(data.size()) + ")");
    }
    if (lenHeader > data.size()) {
        throw std::runtime_error(path + ": len_header is larger than the file");
    }

    AnlzFile result;
    result.headerBytes = data.substr(0, lenHeader);

    size_t pos = lenHeader;
    while (pos < data.size()) {
        if (pos + 12 > data.size()) {
            throw std::runtime_error(path + ": truncated section header at offset " + std::to_string(pos));
        }
        uint32_t fourcc = readU32BE(data, pos);
        uint32_t lenTag = readU32BE(data, pos + 8);
        if (lenTag < 12 || pos + lenTag > data.size()) {
            throw std::runtime_error(path + ": invalid section length at offset " + std::to_string(pos));
        }
        result.sections.push_back({fourcc, data.substr(pos, lenTag)});
        pos += lenTag;
    }

    return result;
}

void AnlzFile::writeRaw(const std::string &path) const
{
    std::string out = headerBytes;
    for (const auto &section : sections) {
        out += section.rawBytes;
    }
    writeU32BE(out, 8, static_cast<uint32_t>(out.size()));

    std::ofstream ofs(path, std::ofstream::binary | std::ofstream::trunc);
    if (!ofs.is_open()) {
        throw std::runtime_error("could not open " + path + " for writing");
    }
    ofs.write(out.data(), static_cast<std::streamsize>(out.size()));
}

}  // namespace djconvert::infrastructure::rekordbox
