// What Steam Input hides behind its virtual controllers. Every controller Steam Input handles shows up as the same "Steam virtual
// controller" (vendor 28DE, product 11FF), but Steam lists the real devices in a file, config/virtualgamepadinfo.txt (its path is also in
// the environment variable SteamVirtualGamepadInfo of games it starts): per virtual slot the controller's name, vendor, product, type
// (xboxone, ps5, ...) and a Steam handle. A virtual controller says which slot it is in two ways: its DirectInput path ends in IG_0<slot>,
// and the undocumented XInputGetCapabilitiesEx puts the slot in a field of its own (as SDL does). A controller that is not behind Steam
// is described by the vendor and product XInputGetCapabilitiesEx reports.
#pragma once

#include "pad.h"

namespace pad::steam_pads {

struct RealDevice {
    char name[64];  // "" when not known (a controller that is not behind Steam)
    unsigned short vid, pid;
    char type[16];  // Steam's word for it ("xboxone", "ps5", ...), or "" when not known
    unsigned long long handle;  // Steam's handle for it, 0 when not behind Steam
    int steam_slot;  // the virtual slot, -1 when not behind Steam
};

// The device in an XInput slot (0-3); false when the slot is empty or XInputGetCapabilitiesEx is missing.
bool device_of_slot(int xinput_slot, RealDevice *out);
// What tells the controller in a slot from others, if Steam tells us: "045E_0B20" (vendor and product), with "_2", "_3" for further
// controllers of the same model. False when it is not behind Steam, or its virtual slot is not in the file.
bool key_of_slot(int xinput_slot, char *out, size_t size);
// "Xbox One S Controller", or "" when not known.
void name_of_slot(int xinput_slot, char *out, size_t size);
// One line for the log.
void describe(int xinput_slot, char *out, size_t size);
// The model a vendor and product are ("Xbox One S Controller", "DualSense Wireless Controller"), or "" when the table does not have it.
const char *model_name(unsigned short vid, unsigned short pid);
// The XInput slot's Steam virtual slot, or -1 (not a Steam virtual controller).
int steam_slot_of_xinput(int xinput_slot);
// The Steam virtual slot in a DirectInput device path (upper case, "...&IG_01#..."), or -1.
int steam_slot_of_path(const wchar_t *path);

}  // namespace pad::steam_pads
