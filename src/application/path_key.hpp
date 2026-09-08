#pragma once

#include <string>

namespace seabass::application
{

// A file path reduced to a key two spellings of the same physical file
// both land on.
//
// Three separate decisions rest on this comparison and all three are
// destructive if it says "different" about one file: whether a file on
// disk is referenced by any catalog (findUnreferencedFiles), whether a
// file queued for deletion is still needed (resolvePendingDeletions),
// and whether two catalog rows describe one file or two
// (collapseCatalogRows). They had two byte-identical copies of this
// between them before this header existed; a third would have been the
// moment one of them quietly drifted.
//
// Backslashes become slashes explicitly, before std::filesystem sees
// anything: it only treats '\' as a separator on Windows, so leaving it
// to the platform would make a stick written on Windows compare
// differently when read on Linux.
//
// Then ASCII-lowercased, because exFAT and NTFS are case-insensitive:
// "Contents/A/b.mp3" and "contents/a/b.mp3" are one file, and comparing
// them case-sensitively would call a referenced file unreferenced. Note
// which way that error runs -- normalizing can only ever move a file
// toward "still referenced" or two rows toward "the same file", which is
// the harmless direction in all three uses.
std::string normalizedPathKey(const std::string &path);

}  // namespace seabass::application
