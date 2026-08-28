#pragma once

#include <cstdint>
#include <string>

namespace djconvert::infrastructure::rekordbox
{

// export.pdb is little-endian (unlike ANLZ, which is big-endian -- see
// big_endian.hpp). Same shape as that file's helpers, just byte order.

inline uint16_t readU16LE(const std::string &data, size_t offset)
{
    return static_cast<uint16_t>(static_cast<unsigned char>(data[offset]) |
                                  (static_cast<unsigned char>(data[offset + 1]) << 8));
}

inline uint32_t readU32LE(const std::string &data, size_t offset)
{
    return static_cast<uint32_t>(static_cast<unsigned char>(data[offset])) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[offset + 1])) << 8) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[offset + 2])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[offset + 3])) << 24);
}

inline void writeU16LE(std::string &data, size_t offset, uint16_t value)
{
    data[offset] = static_cast<char>(value & 0xFF);
    data[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
}

inline void writeU32LE(std::string &data, size_t offset, uint32_t value)
{
    data[offset] = static_cast<char>(value & 0xFF);
    data[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
    data[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
    data[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
}

}  // namespace djconvert::infrastructure::rekordbox
