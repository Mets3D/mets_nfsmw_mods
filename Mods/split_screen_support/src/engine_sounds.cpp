/*
 * The engine sounds of the cars of a split screen race (the sound system: agent_docs/split-screen.md "Audio").
 *
 * Opponents. `CSTATEMGR_AICar::EnterWorld` (0x4F3000) makes one engine sound state (an `EAXCar`: the sound of one car) for each AI racer, but not more than 4 (`cmp eax, 4` at
 * 0x4F3012 and `mov eax, 4` at 0x4F301C); a car that comes within 135 metres of a player takes a free state (`CSTATEMGR_CarState::UpdateParams` 0x4E6330), so with more than 4
 * opponents the others race without a sound. `[options] ai_engine_sounds` raises that number (15 by default: 16 racers less a player; 4 = the game's own). The engine banks the
 * states play are chosen apart from this (`ResolveCarBanks` keeps at most 4 kinds of engine loaded and maps the others to the nearest kind): more states are more voices of the
 * same few engines. Every engine also needs its bank loaded into the sound system's memory, which the game made too small for more than 5 cars' engines at once (2 players and 3
 * opponents, 1 player and 4): see `audio_pool_mb` in streaming_pool.cpp, and the Csis objects and voices next to it.
 *
 * Players 3 and up. The engine sound of the players is a different manager (`CSTATEMGR_PlayerCar`, 2 states: `EAXSound::AttachPlayerCars` 0x4C2100 attaches the first two cars
 * with the context 0, "player", of the list of car states and stops there), so the cars of the others are silent. Their car states get the context of an opponent (1) as soon as
 * they exist (before the audio assets are committed, so that their engines are loaded with the opponents'): they then sound like opponents do, whenever they are near a listener.
 * The listeners are the cameras of players 1 and 2 (the sound system keeps their positions at 0x914430, 0x10 bytes each, and a car looks for the nearest in UpdateParams); the
 * extra players' cameras are added to that list here (a copy of the game's two and the cameras of the extra players, which the one reading site of UpdateParams is pointed at,
 * with its call of GetClosestPlayerCar), so that an opponent or a player's car near player 3 is heard as near as one near player 1.
 * Opponents' engine type. `CSTATEMGR_AICar::QueueSlots` 0x4E5C80 gives the opponents the synthesized (Ginsu) engines when there are 2 or fewer, the sampled (AEMS) ones
 * otherwise (`cmp eax, 2` at 0x4E5CF3). In a split screen race the Ginsu engines of the opponents were silent although every stage that could be logged was as in a single player
 * race (the state attached, the controllers set, the mixer's outputs and the values handed to the hardware of the same size), while the sampled ones were heard [tested: a race of 2
 * players and 2 opponents with `[options] ai_sampled_engines` was heard; before, silent]. Why the Ginsu engines fail is not known. The setting makes the limit 0 in a split
 * screen race. Not the cause: the mix map (both maps hold the same data for the opponents' state), the two player way of working the 3D position out. (The memory, the cause of
 * the silence of 4 or more, was not the cause with 2: 3 sampled engines fitted.)
 *
 * The mix itself has two listeners (the 3D position of a sound is worked out against the arrays of SndCamera, which hold players 1 and 2): a sound nearest to the car of a player 3 or up
 * is worked out with that player's camera and car in the place of the second player's for the length of the call (`hook_update_params_3d`), so it is panned and made loud or quiet as that player hears it.
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
#include "streaming_log.h"

namespace split_screen_support::engine_sounds {

namespace {

constexpr uintptr_t kCompareLimit = 0x4F3014, kClampLimit = 0x4F301D;  // the 4s of `cmp eax, 4` and `mov eax, 4` in EnterWorld
constexpr uintptr_t kStatesRemoteCount = 0x4F300C, kSlotsRemoteCount = 0x4E5CCE;  // the address of the online opponents' count that EnterWorld and QueueSlots read (6 byte instructions, the address at +2)
constexpr uintptr_t kClosestCall = 0x4E63DF, kClosestPlayerCar = 0x4B0790, kListenerArray = 0x4E63ED;  // in CarState::UpdateParams: the call, its target, and the `add ecx, 0x914430`'s number
constexpr uintptr_t kGameListeners = 0x914430, kGameListenerCount = 0x8EF998, kFindCarState = 0x4B89D0, kStateManagers = 0x911F60, kGameFlowState = 0x925E90;
constexpr unsigned kContextOffset = 0x228, kHeadState = 0x10, kNextState = 4, kAttached = 0x38;
constexpr int kContextAiRacer = 1, kSimableWorldIdSlot = 28;  // (ISimable's virtual table: entry 28 is GetWorldID [guess, checked by the log])
constexpr uintptr_t kGetSfxCtlObject = 0x4D92D0;  // CSTATE_Base::GetSFXCTLObject(int): the state's controller with that object index (4 the engine's, 7 the car's 3D position)
constexpr unsigned kCtlObjectPosition = 0x28, kCtlObjectDirection = 0x2C, kCtlObjectVelocity = 0x30, kCtlPlayerRef = 0x44;  // SFXCTL_3DObjPos: the pointers to the object's position, direction and velocity; its player
constexpr int kAiManager = 3, kPlayerManager = 2;
// The sound system's limits (see streaming_pool.cpp): the memory pool of InitializeSoundDriver (its number at 0x8EE558, its size at 0x912848), the Csis objects' slot pool
// ([0x911F58]: +0x18 in use, +0x1C the most, +0x2C capacity), the voice table of the Snd6 library (its count in the low byte at 0x9C218C, the table at [0x9C21F4], 0x90 bytes a voice,
// +0x6D set while it plays).
constexpr uintptr_t kAudioPool = 0x8EE558, kAudioPoolSize = 0x912848, kCsisSlotPool = 0x911F58, kVoiceCount = 0x9C218C, kVoiceTable = 0x9C21F4;
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
        if (!state) continue;  // (the sound connection of the car is made a little after the car)
        g_converted[i] = true;
        const int before = *reinterpret_cast<int *>(state + kContextOffset);
        *reinterpret_cast<int *>(state + kContextOffset) = kContextAiRacer;
        g_log.line("engine sounds: the car of player %d (world id %#x) had the sound context %d, it is an opponent's now (%s)", i + 3, id, before, ready_only ? "in a frame of the race" : "before the audio assets");
    }
}

// ---- the mix's listeners
// The 3D position of a sound is worked out against the data of one of two players (SFXCTL_3DObjPos::UpdateParams 0x4C6250 calls Generate3DParams 0x4B6030, which takes the one whose
// car is nearer, and UpdateDoppler 0x4BF2E0; both read SndCamera's arrays at index `this+0x44`, 0 or 1, which SndCamera::UpdateCameras 0x4D5D40 fills for the first two players
// only). A sound nearest to a car of the players 3 and up is worked out here with the data of that player in the place of the second player's: the arrays are swapped for the
// length of the call.
struct Block {
    uintptr_t first;  // the first player's element; the second's follows it
    unsigned size;
};
enum { kCamPos, kCamDir, kCamDir2, kCamPos2, kCarPos2, kCamDirCopy, kAveragedCamPos, kCarOffset, kCarDir2, kCarVel, kCamVel, kCarPos, kCarDir, kBlockCount };
constexpr Block kBlocks[kBlockCount] = {
    {0x914430, 16},  // m_CurCamPos
    {0x914450, 16},  // m_CurCamDir
    {0x914490, 8},   // the camera's direction, normalized (2D)
    {0x9144A0, 8},   // the camera's position (2D)
    {0x9144B0, 8},   // m_WorldCarPos
    {0x9144C0, 8},   // a copy of the camera's direction
    {0x9144D0, 8},   // m_AveragedCamPos
    {0x9144F0, 8},   // the car's position less the camera's
    {0x914510, 8},   // m_NormCarDir
    {0x914520, 16},  // m_v3WorldCarVel
    {0x914540, 16},  // m_v3WorldCamVel
    {0x914560, 16},  // m_v3WorldCarPos
    {0x914580, 16},  // m_v3WorldCarDir
};
constexpr uintptr_t kSoundSystemPointer = 0x911FA8, kUpdateParams3D = 0x4C6250;
constexpr unsigned kMixMode = 0x64, kSoundPosition = 0x28;

struct ExtraListener {
    bool valid = false;
    unsigned char image[kBlockCount][16];
    float camera[3];
    float velocity[3];  // the camera's, from its movement between frames
    bool have_camera = false;
    long long time = 0;
    unsigned uses = 0;  // sounds worked out against it
};
ExtraListener g_extra_listener[nfsmw::kMaxExtras];
int g_valid_extras = 0;

void put(ExtraListener &l, int block, const float *values, int count) {
    float *to = reinterpret_cast<float *>(l.image[block]);
    memset(to, 0, 16);
    for (int i = 0; i < count; ++i) to[i] = values[i];
}

void normalized(const float *v, float *out) {
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1]);
    out[0] = length > 1e-6f ? v[0] / length : 0.f;
    out[1] = length > 1e-6f ? v[1] / length : 0.f;
}

// The car state (EAX_CarState: position at +0x40, direction +0x10, velocity +0x50, as SndCamera::UpdateCameras reads them) of the car of extra player `i`.
const float *extra_car_state(int i) {
    if (!more_players::extra_ready(i)) return nullptr;
    void *player = more_players::extra_player(i);
    const unsigned id = player ? world_id_of(player) : 0;
    return id ? reinterpret_cast<const float *>(reinterpret_cast<unsigned char *(__cdecl *)(unsigned)>(kFindCarState)(id)) : nullptr;
}

// What SndCamera::UpdateCameras 0x4D5D40 makes of a player's camera and car, for the extra player `i`.
void refresh_extra_listener(int i) {
    ExtraListener &l = g_extra_listener[i];
    const unsigned char *car = reinterpret_cast<const unsigned char *>(extra_car_state(i));
    float camera[3], direction[3];
    l.valid = false;
    if (!car || !extra_cameras::camera_pose(i, camera, direction)) {
        l.have_camera = false;
        return;
    }
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    if (l.have_camera) {
        const float seconds = static_cast<float>(now.QuadPart - l.time) / static_cast<float>(frequency.QuadPart);
        if (seconds > 0.0005f && seconds < 0.5f)
            for (int a = 0; a < 3; ++a) l.velocity[a] = 0.5f * l.velocity[a] + 0.5f * std::min(std::max((camera[a] - l.camera[a]) / seconds, -300.f), 300.f);
    } else {
        l.velocity[0] = l.velocity[1] = l.velocity[2] = 0;
    }
    memcpy(l.camera, camera, sizeof(camera));
    l.time = now.QuadPart;
    l.have_camera = true;
    const float *position = reinterpret_cast<const float *>(car + 0x40), *forward = reinterpret_cast<const float *>(car + 0x10), *velocity = reinterpret_cast<const float *>(car + 0x50);
    float camera_direction[2], car_direction[2], offset[2] = {position[0] - camera[0], position[1] - camera[1]};
    normalized(direction, camera_direction);
    normalized(forward, car_direction);
    put(l, kCamPos, camera, 3);
    put(l, kCamDir, direction, 3);
    put(l, kCamDir2, camera_direction, 2);
    put(l, kCamPos2, camera, 2);
    put(l, kCarPos2, position, 2);
    put(l, kCamDirCopy, camera_direction, 2);
    put(l, kAveragedCamPos, camera, 2);
    put(l, kCarOffset, offset, 2);
    put(l, kCarDir2, car_direction, 2);
    put(l, kCarVel, velocity, 3);
    put(l, kCamVel, l.velocity, 3);
    put(l, kCarPos, position, 3);
    put(l, kCarDir, forward, 3);
    l.valid = true;
}

void refresh_extra_listeners() {
    int valid = 0;
    for (int i = 0; i < nfsmw::kMaxExtras; ++i) {
        if (players::split_screen() && i < more_players::extras_in_race()) refresh_extra_listener(i);
        else g_extra_listener[i].valid = g_extra_listener[i].have_camera = false;
        valid += g_extra_listener[i].valid ? 1 : 0;
    }
    g_valid_extras = valid;
}

// The extra player whose car is the nearest to the sound (nearer than the cars of players 1 and 2), or -1.
int extra_listener_for(const unsigned char *control) {
    if (!g_valid_extras) return -1;
    const unsigned char *sound = *reinterpret_cast<const unsigned char *const *>(kSoundSystemPointer);
    const float *at = *reinterpret_cast<const float *const *>(control + kSoundPosition);
    if (!at || !sound || *reinterpret_cast<const int *>(sound + kMixMode) == 0 || *reinterpret_cast<const int *>(kGameListenerCount) < 2) return -1;
    const auto distance = [at](const float *car) { return (car[0] - at[0]) * (car[0] - at[0]) + (car[1] - at[1]) * (car[1] - at[1]); };
    const float *game_cars = reinterpret_cast<const float *>(kBlocks[kCarPos2].first);
    float best = std::min(distance(game_cars), distance(game_cars + 2));
    int pick = -1;
    for (int i = 0; i < nfsmw::kMaxExtras; ++i) {
        if (!g_extra_listener[i].valid) continue;
        const float d = distance(reinterpret_cast<const float *>(g_extra_listener[i].image[kCarPos2]));
        if (d < best) {
            best = d;
            pick = i;
        }
    }
    return pick;
}

void(__fastcall *g_orig_update_params_3d)(void *, void *, float);

void __fastcall hook_update_params_3d(void *self, void *edx, float seconds) {
    const int extra = extra_listener_for(static_cast<const unsigned char *>(self));
    if (extra < 0) return g_orig_update_params_3d(self, edx, seconds);
    ExtraListener &l = g_extra_listener[extra];
    unsigned char saved[kBlockCount][16];
    for (int b = 0; b < kBlockCount; ++b) {
        unsigned char *second = reinterpret_cast<unsigned char *>(kBlocks[b].first + kBlocks[b].size);
        memcpy(saved[b], second, kBlocks[b].size);
        memcpy(second, l.image[b], kBlocks[b].size);
    }
    if (++l.uses == 1) g_log.line("engine sounds: the first sound worked out against the player %d's camera and car", extra + 3);
    g_orig_update_params_3d(self, edx, seconds);
    for (int b = 0; b < kBlockCount; ++b) memcpy(reinterpret_cast<void *>(kBlocks[b].first + kBlocks[b].size), saved[b], kBlocks[b].size);
}

// ---- the log: everything the sound system holds about the cars
void hex_line(const char *what, const void *address, unsigned bytes) {
    char text[600];
    int n = snprintf(text, sizeof(text), "engine sounds: %s %p:", what, address);
    for (unsigned i = 0; i < bytes / 4 && n < static_cast<int>(sizeof(text)) - 12; ++i) n += snprintf(text + n, sizeof(text) - n, " %x", reinterpret_cast<const unsigned *>(address)[i]);
    g_log.line("%s", text);
}

void *state_ctl(void *state, int index) { return reinterpret_cast<void *(__fastcall *)(void *, void *, int)>(kGetSfxCtlObject)(state, nullptr, index); }

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
            const unsigned char *engine_ctl = static_cast<const unsigned char *>(state_ctl(st, 4)), *position_ctl = static_cast<const unsigned char *>(state_ctl(st, 7));
            g_log.line("engine sounds: %s state %d %p: controllers engine %p, position %p (car position %p, direction %p, velocity %p, player %d)", manager_name(m), n, st, engine_ctl, position_ctl,
                       position_ctl ? *reinterpret_cast<void *const *>(position_ctl + kCtlObjectPosition) : nullptr, position_ctl ? *reinterpret_cast<void *const *>(position_ctl + kCtlObjectDirection) : nullptr,
                       position_ctl ? *reinterpret_cast<void *const *>(position_ctl + kCtlObjectVelocity) : nullptr, position_ctl ? *reinterpret_cast<const int *>(position_ctl + kCtlPlayerRef) : -1);
            const auto dmix = [](const void *object) { return object ? *reinterpret_cast<void *const *>(static_cast<const unsigned char *>(object) + 0x10) : nullptr; };  // SndBase +0x10: its outputs from the mixer (null: not connected to the mix map, GetDMixOutput says 0)
            g_log.line("engine sounds: %s state %d %p: mixer outputs of the sfx objects %p %p %p %p, of the engine controller %p, of the position controller %p", manager_name(m), n, st, dmix(state_sfx(st, 0)),
                       dmix(state_sfx(st, 1)), dmix(state_sfx(st, 2)), dmix(state_sfx(st, 3)), dmix(engine_ctl), dmix(position_ctl));
            g_log.line("engine sounds: %s state %d %p: attached %d, car %p (world id %#x), sfx objects %p %p %p %p, data loaded %d", manager_name(m), n, st, attached, car,
                       attached && car ? *reinterpret_cast<const unsigned *>(static_cast<const unsigned char *>(car) + 0x230) : 0, state_sfx(st, 0), state_sfx(st, 1), state_sfx(st, 2), state_sfx(st, 3),
                       reinterpret_cast<bool(__fastcall *)(void *)>(kStateDataLoaded)(st) ? 1 : 0);
        }
    }
    {
        const unsigned char *sound = *reinterpret_cast<const unsigned char *const *>(0x911FA8);
        g_log.line("engine sounds: the sound system %p: player mix mode %d, the game's number of listeners %d, players' engine states %p %p, game flow state %d", sound, sound ? *reinterpret_cast<const int *>(sound + 0x64) : -1,
                   *reinterpret_cast<const int *>(kGameListenerCount), *reinterpret_cast<void *const *>(0x9142D0), *reinterpret_cast<void *const *>(0x9142D4), *reinterpret_cast<const int *>(kGameFlowState));
    }
    {
        const int audio = *reinterpret_cast<const int *>(kAudioPool);
        const unsigned char *csis = *reinterpret_cast<const unsigned char *const *>(kCsisSlotPool);
        const unsigned char *voices = *reinterpret_cast<const unsigned char *const *>(kVoiceTable);
        const int voice_count = *reinterpret_cast<const int *>(kVoiceCount) & 0xFF;
        int playing = 0;
        for (int i = 0; voices && i < voice_count; ++i) playing += voices[i * 0x90 + 0x6D] ? 1 : 0;
        g_log.line("engine sounds: the sound system's memory (pool %d): %d KB, %d KB free, the largest free block %d KB; Csis objects %d of %d (the most %d); voices playing %d of %d", audio,
                   *reinterpret_cast<const int *>(kAudioPoolSize) >> 10, streaming_log::pool_free(audio) >> 10, streaming_log::pool_largest(audio) >> 10, csis ? *reinterpret_cast<const int *>(csis + 0x18) : -1,
                   csis ? *reinterpret_cast<const int *>(csis + 0x2C) : -1, csis ? *reinterpret_cast<const int *>(csis + 0x1C) : -1, playing, voice_count);
    }
    for (int i = 0; i < nfsmw::kMaxExtras; ++i) {
        const ExtraListener &l = g_extra_listener[i];
        if (l.valid)
            g_log.line("engine sounds: the listener of player %d: camera (%.0f, %.0f, %.0f), car (%.0f, %.0f), camera speed (%.0f, %.0f, %.0f), %u sounds worked out against it", i + 3, l.camera[0], l.camera[1], l.camera[2],
                       reinterpret_cast<const float *>(l.image[kCarPos2])[0], reinterpret_cast<const float *>(l.image[kCarPos2])[1], l.velocity[0], l.velocity[1], l.velocity[2], l.uses);
    }
    hex_line("FinalEngines", reinterpret_cast<void *>(0x9147A4), 0x30);
    hex_line("FinalCopV8Engines", reinterpret_cast<void *>(0x914614), 0x30);
    hex_line("FinalMapping", reinterpret_cast<void *>(0x914660), 0x70);
    hex_line("EngineToCarMapping", reinterpret_cast<void *>(0x914958), 0x70);
    hex_line("bank slots queued / ginsu flag", reinterpret_cast<void *>(0x911ED4), 8);
    hex_line("the AEMS manager", reinterpret_cast<void *>(0x912270), 0x130);
}

void (__cdecl *g_orig_resolve_car_banks)();

// CommitAssets -> ResolveCarBanks: the engines of the cars not yet connected are loaded (4 kinds at the most) and each car's is mapped onto one.
void __cdecl hook_resolve_car_banks() {
    convert_extras(false);
    g_orig_resolve_car_banks();
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

// CSTATEMGR_AICar::QueueSlots 0x4E5C80 uses the synthesized (Ginsu) engines for the opponents when there are 2 or fewer (`cmp eax, 2` at 0x4E5CF3), the sampled (AEMS) ones
// otherwise: in the tests a single player race with 3 or more opponents (AEMS) was heard, and a split screen race with 2 (Ginsu) was not. `[options] ai_sampled_engines` (1) makes
// the limit 0 in a split screen race, so that the opponents always have the sampled engines.
constexpr uintptr_t kGinsuLimit = 0x4E5CF5;  // the imm8 of that compare
bool g_sampled_engines = false;

void set_ginsu_limit(unsigned char limit) {
    const unsigned char now = *reinterpret_cast<unsigned char *>(kGinsuLimit), expected = now == 2 ? 2 : 0;
    if ((now == 2 || now == 0) && now != limit) nfsmw::patch_bytes(kGinsuLimit, &expected, &limit, 1);
}

void on_world_load() {
    if (g_sampled_engines) set_ginsu_limit(players::split_screen() ? 0 : 2);
    for (bool &c : g_converted) c = false;
    for (ExtraListener &l : g_extra_listener) l.valid = l.have_camera = false;
    g_valid_extras = 0;
    g_extra_states = players::split_screen() ? more_players::extras_in_race() : 0;
}

// The opponents' sound manager attaches an engine to a car within `m_fConnectDistance` (+0x20: 135 m, set by its constructor) of the nearest listener.
float g_connect_distance = 0;

void on_frame() {
    if (g_connect_distance > 0) {
        void *mgr = *reinterpret_cast<void **>(kStateManagers + 4 * kAiManager);
        if (mgr) *reinterpret_cast<float *>(static_cast<unsigned char *>(mgr) + 0x20) = g_connect_distance;
    }
    refresh_listeners();
    refresh_extra_listeners();
    g_extra_states = players::split_screen() ? more_players::extras_in_race() : 0;
    if (!players::split_screen()) return;
    convert_extras(true);
}

}  // namespace

void log_state(const char *why) { dump(why); }

void install() {
    g_sampled_engines = GetPrivateProfileIntA("options", "ai_sampled_engines", 1, g_ini) != 0;
    g_connect_distance = static_cast<float>(GetPrivateProfileIntA("options", "ai_engine_radius", 0, g_ini));
    if (g_connect_distance > 0) g_log.line("engine sounds: opponents' engines are attached to cars within %.0f m of a player (the game's 135)", g_connect_distance);
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
            {"sfx3dparams", "SFXCTL_3DObjPos::UpdateParams", kUpdateParams3D, {0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x28, 0x33}, false, nfsmw::detour(&hook_update_params_3d),
             nfsmw::orig(&g_orig_update_params_3d)},
        };
        nfsmw::install_hooks(hooks, 2, g_log, g_ini, "engine sounds");
        events::on_world_load(&on_world_load);
        events::on_frame(&on_frame);
    }
}

}  // namespace split_screen_support::engine_sounds
