/*
 * The volume of a split screen player: every positioned sound (SFXCTL_3DObjPos: engines, tyres, wind, collisions...) is worked out against one player's listener
 * (engine_sounds.cpp), and its volume is scaled by that player's setting. The game works the volume of such a sound out in NFSMixMap::Update3DMixCtls 0x4F18E0: for each 3D control
 * a record {+4 the control's output block, +0xC the volume in hundredths of a dB, +0x10 the volume in Q15}, which the mix channels of the sound take their level from. The hook runs the
 * game's function, then scales the records of the sounds noted in the last few rounds. Sounds that are not positioned (music, menus, speech) are not touched.
 */
#include "split_volume.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <unordered_map>

#include "hooks.h"
#include "max_players.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::split_volume {

namespace {

constexpr uintptr_t kUpdate3DMixCtls = 0x4F18E0, kGetdBFromQ15 = 0x4E4180;
constexpr unsigned kRecordList = 0x14C, kRecordCount = 0x180;  // NFSMixMap: the array of {definition, record} pairs and its length
constexpr unsigned kRecordBlock = 4, kRecordDecibels = 0xC, kRecordQ15 = 0x10;
constexpr int kSilentDecibels = static_cast<int>(0xFFFFD8F0);  // what the game writes for a sound out of range

int g_percent[nfsmw::kMaxPlayers];
// Output block -> the gain last noted for it and in which round of the mix. Kept across rounds: the engines read the gain at their own times (engine_sounds.cpp), and a
// table cleared at each round made them find none just after the clear, take the scaled mixer volume as it was and cut out.
struct Noted {
    float gain;
    unsigned round;
};
std::unordered_map<const void *, Noted> g_gain;
unsigned g_round = 0;
constexpr unsigned kFreshRounds = 4, kPruneEvery = 600;  // a note older than this many rounds is for a sound that is no longer updated (its block may be reused)

const Noted *fresh(const void *block) {
    const auto found = block ? g_gain.find(block) : g_gain.end();
    return found != g_gain.end() && g_round - found->second.round <= kFreshRounds ? &found->second : nullptr;
}
bool g_logged = false;

const char *section_of(int player) {
    static char name[nfsmw::kMaxPlayers][16];
    if (!name[player][0]) snprintf(name[player], sizeof(name[player]), "player_%d", player + 1);
    return name[player];
}

void(__fastcall *g_orig_update_3d_mix)(void *, void *);

void __fastcall hook_update_3d_mix(void *map, void *edx) {
    g_orig_update_3d_mix(map, edx);
    if (++g_round % kPruneEvery == 0)
        for (auto i = g_gain.begin(); i != g_gain.end();) i = g_round - i->second.round > kFreshRounds ? g_gain.erase(i) : std::next(i);
    if (g_gain.empty()) return;
    int *const *entries = *reinterpret_cast<int *const *const *>(static_cast<unsigned char *>(map) + kRecordList);
    const int count = *reinterpret_cast<int *>(static_cast<unsigned char *>(map) + kRecordCount);
    for (int i = 0; entries && i < count; ++i) {
        unsigned char *record = reinterpret_cast<unsigned char *>(entries[2 * i + 1]);
        if (!record) continue;
        const Noted *found = fresh(*reinterpret_cast<const void *const *>(record + kRecordBlock));
        int &q15 = *reinterpret_cast<int *>(record + kRecordQ15);
        if (!found || found->gain >= 1.f || q15 <= 0) continue;
        q15 = static_cast<int>(static_cast<float>(q15) * found->gain);
        *reinterpret_cast<int *>(record + kRecordDecibels) = q15 > 0 ? reinterpret_cast<int(__cdecl *)(int)>(kGetdBFromQ15)(q15) : kSilentDecibels;
        if (!g_logged) {
            g_logged = true;
            g_log.line("split volume: the first sound scaled (by %.0f%%)", found->gain * 100);
        }
    }
}

}  // namespace

int percent(int player) { return player >= 0 && player < nfsmw::kMaxPlayers ? g_percent[player] : 100; }

void set_percent(int player, int value) {
    if (player >= 0 && player < nfsmw::kMaxPlayers) g_percent[player] = std::clamp(value, 0, 100);
}

void save(int player) {
    char text[16];
    snprintf(text, sizeof(text), "%d", percent(player));
    WritePrivateProfileStringA(section_of(player), "sound_volume", text, g_ini);
    g_log.line("player %d's split screen volume %d%%", player + 1, percent(player));
}

float gain_of(const void *block) {
    const Noted *found = fresh(block);
    return found ? found->gain : 1.f;
}

void note_sound(const void *block, int player) {
    if (!block || !players::split_screen()) return;
    g_gain[block] = {static_cast<float>(percent(player)) / 100.f, g_round};
}

void install() {
    for (int player = 0; player < nfsmw::kMaxPlayers; ++player) g_percent[player] = std::clamp(static_cast<int>(GetPrivateProfileIntA(section_of(player), "sound_volume", 100, g_ini)), 0, 100);
    g_gain.reserve(512);
    const nfsmw::Hook hooks[] = {
        {"update3dmixctls", "NFSMixMap::Update3DMixCtls", kUpdate3DMixCtls, {0x83, 0xEC, 0x30, 0x53, 0x55, 0x8B, 0xE9}, false, nfsmw::detour(&hook_update_3d_mix), nfsmw::orig(&g_orig_update_3d_mix)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, g_ini, "split volume");
}

}  // namespace split_screen_support::split_volume
