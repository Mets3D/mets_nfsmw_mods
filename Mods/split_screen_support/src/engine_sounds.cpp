/*
 * The engine sounds of the cars of a split screen race (the sound system: agent_docs/split-screen.md "Audio").
 *
 * Opponents. `CSTATEMGR_AICar::EnterWorld` (0x4F3000) makes one engine sound state (an `EAXCar`: the sound of one car) for each AI racer, but not more than 4 (`cmp eax, 4` at
 * 0x4F3012 and `mov eax, 4` at 0x4F301C); a car that comes within 135 metres of a player takes a free state (`CSTATEMGR_CarState::UpdateParams` 0x4E6330), so with more than 4
 * opponents the others race without a sound. `[options] ai_engine_sounds` raises that number (15 by default: 16 racers less a player; 4 = the game's own). The engine banks the
 * states play are chosen apart from this (`ResolveCarBanks` keeps at most 4 kinds of engine loaded and maps the others to the nearest kind): more states are more voices of the
 * same few engines.
 *
 * Players 3 and up. The engine sound of the players is a different manager (`CSTATEMGR_PlayerCar`, 2 states: `EAXSound::AttachPlayerCars` 0x4C2100 attaches the first two cars
 * with the context 0, "player", of the list of car states and stops there), so the cars of the others are silent. Their car states get the context of an opponent (1) as soon as
 * they exist (before the audio assets are committed, so that their engines are loaded with the opponents'): they then sound like opponents do, whenever they are near a listener.
 * The listeners are the cameras of players 1 and 2 (the sound system keeps their positions at 0x914430, 0x10 bytes each, and a car looks for the nearest in UpdateParams); the
 * extra players' cameras are added to that list here (a copy of the game's two and the cameras of the extra players, which the one reading site of UpdateParams is pointed at,
 * with its call of GetClosestPlayerCar), so that an opponent or a player's car near player 3 is heard as near as one near player 1.
 * The mix itself still has two listeners (the 3D position of each sound is worked out against players 1 and 2): a car far from both of them is quiet.
 */
#include "engine_sounds.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "events.h"
#include "hooks.h"
#include "extra_cameras.h"
#include "max_players.h"
#include "more_players.h"
#include "patch.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::engine_sounds {

namespace {

constexpr uintptr_t kCompareLimit = 0x4F3014, kClampLimit = 0x4F301D;  // the 4s of `cmp eax, 4` and `mov eax, 4` in EnterWorld
constexpr uintptr_t kStatesRemoteCount = 0x4F300C, kSlotsRemoteCount = 0x4E5CCE;  // the address of the online opponents' count that EnterWorld and QueueSlots read (6 byte instructions, the address at +2)
constexpr uintptr_t kClosestCall = 0x4E63DF, kClosestPlayerCar = 0x4B0790, kListenerArray = 0x4E63ED;  // in CarState::UpdateParams: the call, its target, and the `add ecx, 0x914430`'s number
constexpr uintptr_t kGameListeners = 0x914430, kGameListenerCount = 0x8EF998, kFindCarState = 0x4B89D0, kStateManagers = 0x911F60, kGameFlowState = 0x925E90;
constexpr unsigned kContextOffset = 0x228, kHeadState = 0x10, kNextState = 4, kAttached = 0x38;
constexpr int kContextAiRacer = 1, kSimableWorldIdSlot = 28;  // (ISimable's virtual table: entry 28 is GetWorldID [guess, checked by the log])
constexpr int kAiManager = 3, kPlayerManager = 2;
constexpr uintptr_t kGetSfxObject = 0x4D92A0, kStateDataLoaded = 0x4F2D00, kCarStateList = 0x913DB4, kCarStateCount = 0x913DBC, kResolveCarBanks = 0x4F9020;

// ---- the listeners
constexpr int kListeners = nfsmw::kMaxPlayers;
float g_listeners[kListeners][4];  // x, y, z (the world's axes) and a pad: the layout of the game's own array
int g_listener_count = 0;

// The game's own listeners (one in a race of one player) and, in a split screen race, the extra players'.
void refresh_listeners() {
    const int game = std::min(std::max(*reinterpret_cast<const int *>(kGameListenerCount), 0), 2);
    int count = game;
    memcpy(g_listeners, reinterpret_cast<const void *>(kGameListeners), game * sizeof(g_listeners[0]));
    for (int i = 0; players::split_screen() && game == 2 && i < nfsmw::kMaxExtras && count < kListeners; ++i) {
        if (!extra_cameras::camera_position(i, g_listeners[count])) break;
        g_listeners[count][3] = 0;
        ++count;
    }
    g_listener_count = count;
}

// CSTATEMGR_CarState::UpdateParams asks which player's listener a car is nearest to: the index (0 or 1), then reads the position at 0x914430 + 0x10 * index.
void *__cdecl closest_listener(const float *position, bool, int *index) {
    float best = 1e30f;
    *index = 0;
    for (int i = 0; i < g_listener_count; ++i) {  // (the game's: the cars of the players of the race, index 0 when there are none)
        const float dx = g_listeners[i][0] - position[0], dy = g_listeners[i][1] - position[1], dz = g_listeners[i][2] - position[2];
        const float distance = dx * dx + dy * dy + dz * dz;
        if (distance < best) {
            best = distance;
            *index = i;
        }
    }
    return nullptr;
}

// ---- the extra players' cars as opponents
bool g_converted[nfsmw::kMaxExtras];
int g_extra_states = 0;  // the cars of the extra players are opponents to the sound manager: it makes a state for each (read in place of the count of online opponents, which is 0 offline)
int g_dumps = 0;  // the dumps of this race made so far (15 s, 40 s and 70 s after the world began to load: the cars are driving from about 25 s)
DWORD g_world_ms = 0;

unsigned world_id_of(void *player) {
    void *simable = reinterpret_cast<void *(__fastcall *)(void *, void *)>((*reinterpret_cast<void ***>(player))[1])(player, nullptr);  // IPlayer::GetSimable
    if (!simable) return 0;
    return reinterpret_cast<unsigned(__fastcall *)(void *, void *)>((*reinterpret_cast<void ***>(simable))[kSimableWorldIdSlot])(simable, nullptr);
}

// `ready_only`: wait for the race to run (the frame hook); false: as soon as the player has a car (before the audio assets are committed).
void convert_extras(bool ready_only) {
    for (int i = 0; i < nfsmw::kMaxExtras; ++i) {
        if (g_converted[i] || (ready_only && !more_players::extra_ready(i))) continue;
        void *player = more_players::extra_player(i);
        const unsigned id = player ? world_id_of(player) : 0;
        unsigned char *state = id ? reinterpret_cast<unsigned char *(__cdecl *)(unsigned)>(kFindCarState)(id) : nullptr;
        static bool logged_id[nfsmw::kMaxExtras];
        if (!logged_id[i]) {
            logged_id[i] = true;
            g_log.line("engine sounds: the world id of the car of player %d is %#x, its sound state is %p", i + 3, id, state);
        }
        if (!state) continue;  // (the sound connection of the car is made a little after the car)
        g_converted[i] = true;
        const int before = *reinterpret_cast<int *>(state + kContextOffset);
        *reinterpret_cast<int *>(state + kContextOffset) = kContextAiRacer;
        g_log.line("engine sounds: the car of player %d (world id %#x) had the sound context %d, it is an opponent's now (%s)", i + 3, id, before, ready_only ? "in a frame of the race" : "before the audio assets");
    }
}

// ---- the log: everything the sound system holds about the cars
void hex_line(const char *what, const void *address, unsigned bytes) {
    char text[600];
    int n = snprintf(text, sizeof(text), "engine sounds: %s %p:", what, address);
    for (unsigned i = 0; i < bytes / 4 && n < static_cast<int>(sizeof(text)) - 12; ++i) n += snprintf(text + n, sizeof(text) - n, " %x", reinterpret_cast<const unsigned *>(address)[i]);
    g_log.line("%s", text);
}

void *state_sfx(void *state, int index) { return reinterpret_cast<void *(__fastcall *)(void *, void *, int)>(kGetSfxObject)(state, nullptr, index); }

const char *manager_name(int m) { return m == 2 ? "players'" : m == 3 ? "opponents'" : m == 4 ? "cops'" : m == 5 ? "traffic's" : "other"; }

void dump(const char *why) {
    g_log.line("engine sounds: ---- dump: %s", why);
    void *const *cars = *reinterpret_cast<void *const *const *>(kCarStateList);
    const int car_count = *reinterpret_cast<const int *>(kCarStateCount);
    for (int i = 0; cars && i < car_count && i < 64; ++i) {
        const unsigned char *car = static_cast<const unsigned char *>(cars[i]);
        const float *p = reinterpret_cast<const float *>(car + 0x40);
        const char *where = "no state";
        for (int m = 2; m <= 5; ++m) {
            void *mgr = *reinterpret_cast<void **>(kStateManagers + 4 * m);
            for (const unsigned char *st = mgr ? *reinterpret_cast<const unsigned char *const *>(static_cast<unsigned char *>(mgr) + kHeadState) : nullptr; st; st = *reinterpret_cast<const unsigned char *const *>(st + kNextState))
                if (st[kAttached] && *reinterpret_cast<const void *const *>(st + 0x34) == car) where = manager_name(m);
        }
        g_log.line("engine sounds: car state %d %p: world id %#x, context %d, sim updating %d, at (%.0f, %.0f, %.0f), sound: %s", i, car, *reinterpret_cast<const unsigned *>(car + 0x230),
                   *reinterpret_cast<const int *>(car + kContextOffset), car[0x22C], p[0], p[1], p[2], where);
    }
    for (int m = 2; m <= 5; ++m) {
        void *mgr = *reinterpret_cast<void **>(kStateManagers + 4 * m);
        if (!mgr) continue;
        int n = 0;
        for (unsigned char *st = *reinterpret_cast<unsigned char **>(static_cast<unsigned char *>(mgr) + kHeadState); st && n < 40; st = *reinterpret_cast<unsigned char **>(st + kNextState), ++n) {
            const bool attached = st[kAttached] != 0;
            const void *car = *reinterpret_cast<const void *const *>(st + 0x34);
            g_log.line("engine sounds: %s state %d %p: attached %d, car %p (world id %#x), sfx objects %p %p %p %p, data loaded %d", manager_name(m), n, st, attached, car,
                       attached && car ? *reinterpret_cast<const unsigned *>(static_cast<const unsigned char *>(car) + 0x230) : 0, state_sfx(st, 0), state_sfx(st, 1), state_sfx(st, 2), state_sfx(st, 3),
                       reinterpret_cast<bool(__fastcall *)(void *)>(kStateDataLoaded)(st) ? 1 : 0);
        }
    }
    hex_line("FinalEngines", reinterpret_cast<void *>(0x9147A4), 0x30);
    hex_line("FinalCopV8Engines", reinterpret_cast<void *>(0x914614), 0x30);
    hex_line("FinalMapping", reinterpret_cast<void *>(0x914660), 0x70);
    hex_line("EngineToCarMapping", reinterpret_cast<void *>(0x914958), 0x70);
    hex_line("bank slots queued / ginsu flag", reinterpret_cast<void *>(0x911ED4), 8);
    hex_line("the AEMS manager", reinterpret_cast<void *>(0x912264), 0x134);
}

void (__cdecl *g_orig_resolve_car_banks)();

// CommitAssets -> ResolveCarBanks: the engines of the cars not yet connected are loaded (4 kinds at the most) and each car's is mapped onto one.
void __cdecl hook_resolve_car_banks() {
    convert_extras(false);
    dump("before ResolveCarBanks");
    g_orig_resolve_car_banks();
    dump("after ResolveCarBanks");
}

// How many states a manager has and how many of them are attached (a car's sound).
void count_states(int manager, int *states, int *attached) {
    void *mgr = *reinterpret_cast<void **>(kStateManagers + 4 * manager);
    *states = *attached = 0;
    if (!mgr) return;
    for (const unsigned char *s = *reinterpret_cast<const unsigned char *const *>(static_cast<unsigned char *>(mgr) + kHeadState); s && *states < 100; s = *reinterpret_cast<const unsigned char *const *>(s + kNextState)) {
        ++*states;
        if (s[kAttached]) ++*attached;
    }
}

void on_world_load() {
    for (bool &c : g_converted) c = false;
    g_extra_states = players::split_screen() ? more_players::extras_in_race() : 0;
    g_dumps = 0;
    g_world_ms = GetTickCount();
}

void on_frame() {
    refresh_listeners();
    g_extra_states = players::split_screen() ? more_players::extras_in_race() : 0;
    if (!players::split_screen()) return;
    convert_extras(true);
    static const DWORD kDumpAt[] = {15000, 40000, 70000};
    if (g_dumps < 3 && *reinterpret_cast<int *>(kGameFlowState) == 6 && GetTickCount() - g_world_ms >= kDumpAt[g_dumps]) {
        char why[40];
        snprintf(why, sizeof(why), "%d s after the race began to load", static_cast<int>(kDumpAt[g_dumps] / 1000));
        ++g_dumps;
        dump(why);
    }
}

}  // namespace

void log_state(const char *why) { dump(why); }

void install() {
    const int wanted = static_cast<int>(GetPrivateProfileIntA("options", "ai_engine_sounds", 15, g_ini));
    if (wanted > 4 && wanted <= 100) {
        const unsigned char four = 4, limit8 = static_cast<unsigned char>(wanted);
        const unsigned char expected32[4] = {4, 0, 0, 0}, limit32[4] = {limit8, 0, 0, 0};
        const bool patched = nfsmw::patch_bytes(kCompareLimit, &four, &limit8, 1) && nfsmw::patch_bytes(kClampLimit, expected32, limit32, 4);
        g_log.line("engine sounds: up to %d opponents have an engine sound (the game's 4)%s", wanted, patched ? "" : ": NOT CHANGED, the code is not as expected");
    }
    if (GetPrivateProfileIntA("options", "extra_player_sounds", 1, g_ini)) {
        const unsigned char expected[4] = {0x30, 0x44, 0x91, 0x00};
        const unsigned char *call = reinterpret_cast<const unsigned char *>(kClosestCall);
        const bool as_expected = call[0] == 0xE8 && memcmp(reinterpret_cast<const void *>(kListenerArray), expected, 4) == 0;
        refresh_listeners();
        bool listeners = false;
        if (as_expected) {  // (the array first, then the call that fills the index: neither is any use without the other)
            const uintptr_t array = reinterpret_cast<uintptr_t>(&g_listeners[0][0]);
            unsigned char replacement[4];
            memcpy(replacement, &array, 4);
            listeners = nfsmw::redirect_call(kClosestCall, kClosestPlayerCar, reinterpret_cast<uintptr_t>(&closest_listener)) && nfsmw::patch_bytes(kListenerArray, expected, replacement, 4);
        }
        const unsigned char online[4] = {0xC4, 0xCF, 0x92, 0x00};
        const uintptr_t extra_states = reinterpret_cast<uintptr_t>(&g_extra_states);
        unsigned char states_address[4];
        memcpy(states_address, &extra_states, 4);
        const bool states = nfsmw::patch_bytes(kStatesRemoteCount, online, states_address, 4) && nfsmw::patch_bytes(kSlotsRemoteCount, online, states_address, 4);
        g_log.line("engine sounds: the cars of players 3 and up count as opponents%s; the listeners are the cameras of all players%s", states ? "" : " (NOT CHANGED: the count of states)",
                   listeners ? "" : " (NOT CHANGED: the listeners)");
        const nfsmw::Hook hooks[] = {
            {"resolvecarbanks", "CSTATEMGR_CarState::ResolveCarBanks", kResolveCarBanks, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x6A}, false, nfsmw::detour(&hook_resolve_car_banks),
             nfsmw::orig(&g_orig_resolve_car_banks)},
        };
        nfsmw::install_hooks(hooks, 1, g_log, g_ini, "engine sounds");
        events::on_world_load(&on_world_load);
        events::on_frame(&on_frame);
    }
}

}  // namespace split_screen_support::engine_sounds
