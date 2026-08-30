#pragma once

#include <optional>
#include <string>

#include <djinterop/musical_key.hpp>

namespace djconvert::infrastructure::engine
{

// Parses a rekordbox-style key string (e.g. "Fm", "F#m", "Gbm", "C",
// "A♭") into libdjinterop's musical_key enum, by pitch class + mode
// rather than by exact spelling, so either enharmonic spelling ("F#m" or
// "Gbm") maps to the same key. Handles both ASCII ('#'/'b') and Unicode
// ('♯'/'♭') accidentals. Returns nullopt for anything it doesn't
// recognize (including Camelot notation, e.g. "8A" -- not supported in
// this first version) rather than guessing wrong.
std::optional<djinterop::musical_key> parseRekordboxKey(const std::string &key);

}  // namespace djconvert::infrastructure::engine
