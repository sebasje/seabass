#pragma once

#include <string>

#include "application/ports/removable_media_locator.hpp"

namespace djconvert::infrastructure::media
{

// Checks a mounted root directory for either format's on-disk signature
// (PIONEER/rekordbox/export.pdb, Engine Library/Database2/m.db) and fills
// in stick.rekordboxPath/enginePath accordingly. Pure std::filesystem, no
// OS-specific device APIs -- shared by every platform's
// RemovableMediaLocator so "what makes a mounted root a rekordbox/Engine
// stick" is defined exactly once.
void scanMountedRoot(const std::string &mountPoint, application::DetectedStick &stick);

}  // namespace djconvert::infrastructure::media
