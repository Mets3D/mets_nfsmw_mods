// Settings kept per career profile: <profile folder>/controller_settings.ini, next to the profile's save file (the game saves a profile as
// <save root>/<name>/<name>; the root is wherever the game's save system keeps it, which differs with the mods installed). The file is
// the profile's own, so its sections carry no profile name: [controller_<id>], [rumble_<id>], [look_bindings], [pad_bindings].
// A key the file lacks is taken from the same section of the mod's own ini (the user's defaults), and then the caller's default.
// Until the profile has a folder (a profile that was never saved) everything is written to the mod's ini instead.
#pragma once

#include "pad.h"

namespace pad::profile {

// The current profile's settings file, or "" while the game has no folder for it. Cheap to call: the disk is looked at only when the
// profile's name changes, and every two seconds while there is no folder yet. Another profile, or the folder appearing, changes the text.
// Game thread only.
const char *file();

bool get_string(const char *section, const char *key, char *out, size_t size);  // false (out empty) when neither file has it
float get_float(const char *section, const char *key, float def);
int get_int(const char *section, const char *key, int def);
void set(const char *section, const char *key, const char *value);  // to the profile's file, else the mod's ini
// Changes whenever either file is written (to pick up edits by hand).
unsigned long long stamp();

}  // namespace pad::profile
