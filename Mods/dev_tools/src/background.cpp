/*
 * The game's message pump (sub_6C21A0, called once per frame) takes its messages with PeekMessage while the window is
 * active ([0x982BEC], set from WM_ACTIVATEAPP by the window procedure sub_6DB6C0) or while FEDatabase+0x12C has bit 3 or 6
 * set, and otherwise with GetMessage, which blocks until a message arrives: an inactive window in the menus stops the
 * whole game, its rendering and simulation included. The `je` at 0x6C2245 that picks GetMessage becomes two nops, so the
 * pump always uses PeekMessage. (WM_ACTIVATE only mutes the sound; [0x982C50], which it writes, is read by nothing but
 * NFSMWExtraOptions' hot keys.)
 */
#include "background.h"

#include "dev_tools.h"
#include "patch.h"

namespace dev_tools::background {

void install() {
    constexpr uintptr_t kBlockingJump = 0x6C2245;
    const unsigned char expected[] = {0x74, 0x0E}, replacement[] = {0x90, 0x90};
    if (nfsmw::patch_bytes(kBlockingJump, expected, replacement, 2)) g_log.line("background: the game keeps running while its window is not in front");
    else g_log.line("background: NOT patched (the code at %#x differs)", kBlockingJump);
}

}  // namespace dev_tools::background
