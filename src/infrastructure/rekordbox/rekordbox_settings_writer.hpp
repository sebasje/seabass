#pragma once

#include <string>

namespace seabass::infrastructure::rekordbox
{

// Sets one known settings field (see allSettingsFields() in
// rekordbox_settings_fields.hpp) to one of its known options and rewrites
// the file's checksum, in place. Refuses (returns false, changes nothing)
// if the field/option isn't recognized, the file is missing, or its size
// doesn't match what readDeviceSettings() expects -- this never writes
// anything Seabass does not fully understand the layout of.
//
// Does NOT back up the file first -- callers must do that themselves (see
// SettingsController::setField()), matching every other write path in the
// app.
bool writeDeviceSettingField(const std::string &pioneerRoot, const std::string &fileName,
                              const std::string &fieldLabel, const std::string &optionName);

}  // namespace seabass::infrastructure::rekordbox
