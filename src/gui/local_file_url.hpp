#pragma once

#include <QString>
#include <QUrl>

#include <string>

namespace djconvert::gui
{

// Converts a native local path (e.g. "H:\PIONEER/Contents/..." -- rekordbox
// stores its own paths with forward slashes regardless of platform, and
// readers just prepend the platform's native stick root) into a properly
// formed file:// URL string for QML's Image.source. Plain "file://" + path
// string concatenation works by accident on Linux (an absolute POSIX path
// already starts with '/', so the result happens to have the URL's required
// triple slash) but produces a malformed URL on Windows (missing the third
// slash before the drive letter, and containing raw backslashes) -- QUrl::
// fromLocalFile() handles both platforms correctly. Empty in, empty out.
inline QString toLocalFileUrl(const std::string &path)
{
    if (path.empty()) {
        return {};
    }
    return QUrl::fromLocalFile(QString::fromStdString(path)).toString();
}

}  // namespace djconvert::gui
