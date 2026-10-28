/*
 * A car with a customized paint job is drawn with a texture the game composes from its parts (base colour, vinyls) at load time into a "dummy skin": the texture named DUMMY_SKIN<n>
 * (and DUMMY_WHEEL<n>, DUMMY_SPINNER<n>) of CARS\TEXTURES.BIN, where n is the car's skin slot. VehicleRenderConn::Load (0x75D220) takes the lowest free slot from a mask
 * (`mOpenSkinSlots` 0x903690, 12 bits in the exe; a car frees its slot in Unload) and RideInfo::SetCompositeNameHash (0x747F10) names the textures after it. The game has 4 slots for
 * composite skins (5 to 12 are for the pre-composited vinyls of the consoles). NFSMWExtraOptions' CarSkinFix lets the search go up to 16 and
 * makes the fifth car use DUMMY_SKIN2 (ids above 4 count 2, 3, 4, 2, 3, 4...), so the fifth car's paint was composed over the second car's [tested: a race of 2 players and 3
 * opponents: the opponent in slot 5 had the hashes of slot 2, and player 2's car had its colours].
 * Here every slot has its own textures: the search goes up to `[options] skin_slots` (16 by default: the most racers a race has; CARS\TEXTURES.BIN of Redux v3 has 20), the
 * mask has that many bits, and SetCompositeNameHash is replaced by the game's code without the wrapping (all slots are composite skins, type 1). A car that finds no slot, or whose
 * textures the pack does not have, keeps its stock paint (CompositeSkin skips a skin without its texture).
 */
#include "skin_slots.h"

#include <windows.h>

#include <cstdio>

#include "hooks.h"
#include "patch.h"
#include "split_screen_support.h"

namespace split_screen_support::skin_slots {

namespace {

constexpr uintptr_t kSlotLimit = 0x75D2B6, kOpenSkinSlots = 0x903690, kSetCompositeNameHash = 0x747F10, kStringHash = 0x460BF0, kCarTypeTable = 0x9B09D8;
constexpr unsigned kCarTypeSize = 0xD0, kSkinnedFlag = 0xC7;  // CarTypeInfo: the flag that says the car has a composite skin
constexpr int kMaxSlots = 20;

void (__fastcall *g_orig)(void *, void *, int) = nullptr;
int g_slots = 16;

// RideInfo: +0 car type, +7 skin type (1 composite, 2 pre-composite, 0 none), +0x3C / 0x40 / 0x44 the hashes of the dummy skin, wheel and spinner textures.
void __fastcall hook_set_composite_name_hash(unsigned char *ride, void *, int slot) {
    ride[7] = slot >= 1 && slot <= g_slots ? 1 : 0;
    const auto hash = reinterpret_cast<unsigned(__cdecl *)(const char *)>(kStringHash);
    char name[64];
    snprintf(name, sizeof(name), "DUMMY_SKIN%d", slot);
    const unsigned skin = hash(name);
    const unsigned char *types = *reinterpret_cast<unsigned char *const *>(kCarTypeTable);
    *reinterpret_cast<unsigned *>(ride + 0x3C) = types[*reinterpret_cast<int *>(ride) * kCarTypeSize + kSkinnedFlag] ? skin : 0;
    snprintf(name, sizeof(name), "DUMMY_WHEEL%d", slot);
    *reinterpret_cast<unsigned *>(ride + 0x40) = hash(name);
    snprintf(name, sizeof(name), "DUMMY_SPINNER%d", slot);
    *reinterpret_cast<unsigned *>(ride + 0x44) = hash(name);
}

}  // namespace

void install() {
    g_slots = static_cast<int>(GetPrivateProfileIntA("options", "skin_slots", 16, g_ini));
    if (g_slots <= 4) return;
    if (g_slots > kMaxSlots) g_slots = kMaxSlots;
    const nfsmw::Hook hooks[] = {
        {"skinslots", "RideInfo::SetCompositeNameHash", kSetCompositeNameHash, {0x83, 0xEC, 0x40, 0x56, 0x57, 0x8B, 0x7C}, false, nfsmw::detour(&hook_set_composite_name_hash),
         nfsmw::orig(&g_orig)},
    };
    if (nfsmw::install_hooks(hooks, 1, g_log, g_ini, "skin slots").created != 1) return;
    const unsigned char now = *reinterpret_cast<unsigned char *>(kSlotLimit), wanted = static_cast<unsigned char>(g_slots);  // (NFSMWExtraOptions' CarSkinFix writes 16)
    const bool limit = now == wanted || ((now == 4 || now == 16) && nfsmw::patch_bytes(kSlotLimit, &now, &wanted, 1));
    *reinterpret_cast<unsigned *>(kOpenSkinSlots) |= (1u << g_slots) - 1;
    g_log.line("skin slots: %d (the search limit %s)", g_slots, limit ? "set" : "not changed");
}

}  // namespace split_screen_support::skin_slots
