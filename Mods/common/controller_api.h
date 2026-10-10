// What nfsmw_controller_support offers other mods. It is looked up at run time, because it may not be installed:
//
//     extern "C" __declspec(dllexport) void nfsmw_controller_support_exclusive_pad(int slot);
//
// kEveryPad (-1): as normal; the game itself reads only the first controller DirectInput gave it. 0..3: only that
// XInput controller drives the game: each XInput controller DirectInput shows the game reports that one's state, so the
// game's controller is that pad whichever one the game opened. kNoPad (-2): no controller, the keyboard only.
// kMergedPads (-3): every XInput controller, merged into one (any button; each stick and trigger from the pad pushing it
// furthest). Header only.
#pragma once

#include "nfsmw_util.h"

namespace nfsmw::controller_api {

constexpr int kEveryPad = -1, kNoPad = -2, kMergedPads = -3;
constexpr const char *kExclusivePadExport = "nfsmw_controller_support_exclusive_pad";
using ExclusivePadFn = void (*)(int slot);

// The function, or null when nfsmw_controller_support is not loaded.
inline ExclusivePadFn find_exclusive_pad() {
    const HMODULE module = GetModuleHandleA("nfsmw_controller_support.asi");
    return module ? reinterpret_cast<ExclusivePadFn>(reinterpret_cast<void *>(GetProcAddress(module, kExclusivePadExport)))
                  : nullptr;
}

}  // namespace nfsmw::controller_api
