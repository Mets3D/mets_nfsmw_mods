/*
 * The EA Trax song display (the game calls it the chyron) covers much of player 1's picture, especially with four
 * players. SFXObj_PFEATrax::NotifyChyron summons it once for each song, then sets bit 0x40 in the object's flags so it
 * is not summoned again. In split screen this hook skips the display and sets the same bit; the song still plays.
 */
#include "ea_trax.h"

#include "hooks.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::ea_trax {

namespace {

constexpr uintptr_t kNotifyChyron = 0x4F4FC0;
constexpr unsigned kFlags = 0x114, kChyronShown = 0x40;

void(__fastcall *g_orig_notify_chyron)(void *);
void __fastcall hook_notify_chyron(void *trax) {
    if (!players::split_screen()) {
        g_orig_notify_chyron(trax);
        return;
    }
    *reinterpret_cast<unsigned *>(static_cast<unsigned char *>(trax) + kFlags) |= kChyronShown;
}

}  // namespace

void install() {
    const nfsmw::Hook hooks[] = {
        {"eatrax", "SFXObj_PFEATrax::NotifyChyron", kNotifyChyron, {0x56, 0x8B, 0xF1, 0x8B, 0x86, 0x50, 0x01}, false,
         nfsmw::detour(&hook_notify_chyron), nfsmw::orig(&g_orig_notify_chyron)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, g_ini, "EA Trax");
}

}  // namespace split_screen_support::ea_trax
