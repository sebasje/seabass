// normalizedPathKey() is the comparison three destructive decisions rest
// on: whether a file on disk is referenced by any catalog
// (findUnreferencedFiles), whether a file queued for deletion is still
// needed (resolvePendingDeletions), and whether two catalog rows describe
// one file or two (collapseCatalogRows).
//
// The failure that matters is one-directional. If two spellings of ONE
// file produce different keys, a referenced file is reported as
// unreferenced and offered for deletion, and a file still in the catalog
// is deleted from the pending queue. Every case here is therefore a pair
// that must agree.
#include <cassert>
#include <iostream>
#include <string>

#include "application/path_key.hpp"

using seabass::application::normalizedPathKey;

namespace
{
void same(const std::string &a, const std::string &b, const char *what)
{
    if (normalizedPathKey(a) != normalizedPathKey(b)) {
        std::cerr << "FAILED: " << what << "\n  [" << normalizedPathKey(a) << "]\n  [" << normalizedPathKey(b) << "]\n";
        assert(false);
    }
}
}  // namespace

int main()
{
    // exFAT and NTFS are case-insensitive, so these are one file.
    same("/Contents/a.mp3", "/CONTENTS/A.MP3", "ASCII case");

    // A stick written on Windows read on Linux: std::filesystem only
    // treats '\' as a separator on Windows, so this cannot be left to
    // the platform.
    same("/Contents/a.mp3", "\\Contents\\a.mp3", "backslash separators");

    same("/Contents/./a.mp3", "/Contents/a.mp3", "dot component");
    same("/Contents//a.mp3", "/Contents/a.mp3", "doubled separator");
    same("/Contents/x/../a.mp3", "/Contents/a.mp3", "parent component");

    // THE ONE THAT BIT. export.pdb stores its strings in fixed-length
    // fields and space-pads them, so every path read out of a rekordbox
    // catalog arrives with trailing spaces, while the same path walked
    // off the filesystem does not. Comparing them unequal means every
    // audio file on the stick reads as unreferenced.
    same("/Contents/a.mp3", "/Contents/a.mp3   ", "export.pdb's trailing space padding");
    same("/Contents/a.mp3", "/Contents/a.mp3\t", "trailing tab");
    same("/Contents/a.mp3", std::string("/Contents/a.mp3\0\0", 17), "trailing NULs");

    // Padding must not eat a name that really ends in a space before the
    // extension, which is a different thing entirely.
    assert(normalizedPathKey("/Contents/a .mp3") != normalizedPathKey("/Contents/a.mp3"));

    // Empty stays empty rather than becoming "." -- callers use empty as
    // "no path known" and must not have it collide with a real one.
    assert(normalizedPathKey("").empty());
    assert(normalizedPathKey("   ").empty());

    std::cout << "all cases passed\n";
    return 0;
}
