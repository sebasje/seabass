#pragma once

#include <cstddef>
#include <string>

namespace djconvert::infrastructure::compression
{

// Thin wrapper over zlib's deflate/inflate (level 6, the zlib default --
// a good size/speed tradeoff for the mostly-text track/cue data this is
// used on). Throws std::runtime_error on failure.
std::string compress(const std::string &data);

// originalSize must be the exact uncompressed size (the caller is expected
// to have stored it alongside the compressed bytes -- zlib's inflate needs
// to know the output buffer size upfront). Throws std::runtime_error if
// decompression fails or doesn't produce exactly originalSize bytes.
std::string decompress(const std::string &data, size_t originalSize);

}  // namespace djconvert::infrastructure::compression
