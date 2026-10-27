/*
 * The reflection on the cars is a cube map drawn from six extra views (18-23) around the followed car. The game points those views at the first
 * player's car once per frame (UpdateEnvironmentMapCameras, 0x756060, only when [0x9017B8] is set), sets them up (SetupPerView, 0x6CF400, with the
 * arguments eDisplayFrame gives it) and ___eDisplayFrame then draws them before the world. In a split screen race every pass is drawn around its own
 * player's car, into the one cube map.
 *
 * "Car Reflection Update Rate" is [0x9017B8] (1 to 3; 0 = no reflections): eEnvMap::UpdateCameras (0x6DA350) switches on only some of the six faces
 * each frame, by the frame number: one face a frame (rate 1, a cycle of 6 frames), three faces (rate 2, 4 frames), all six (rate 3). With several
 * passes the faces a frame leaves out would keep what another player's pass drew into them, so the players' reflections came and went. Drawing all
 * six faces in every pass cost most of what the extra passes cost (24 world drawings a frame for four players: a third of the frame time), so each
 * player has a copy of the six faces instead: it is put into the cube map before the player's pass and taken out after, and the game updates its
 * faces at its own rate, as in a game for one player. The first pass of a player draws all six.
 */
#include "reflections.h"

#include <MinHook.h>

#include "hooks.h"
#include "more_players.h"
#include "patch.h"
#include "split_screen_support.h"

namespace split_screen_support::reflections {

namespace {

constexpr uintptr_t kEnvMapEnabled = 0x9017B8, kUpdateEnvMapCameras = 0x756060, kViews = 0x9195E0;
constexpr uintptr_t kEnvUpdateCamerasCall = 0x75611D, kEnvUpdateCameras = 0x6DA350, kSetupPerView = 0x6CF400;
constexpr int kViewSize = 0x70, kViewActive = 0x08, kViewRenderTarget = 0x60, kTargetSurface = 8, kFirstFace = 18, kFaces = 6;
constexpr unsigned kFarPlane = 0x466A6000;  // 15000.0f, as eDisplayFrame passes it
constexpr int kMaxPlayers = 8;

bool g_split = false;
int g_player = 0, g_players = 2;     // the pass being drawn, and how many passes there are
int g_rate_limit_many = 1;           // the most faces-per-frame rate for more than two players ([options] reflection_rate_four_players; 0 = the game's)
bool g_copies_wanted = true;         // [options] reflection_copies
bool g_pass_drew_all = false;        // the update call forced every face in this pass

struct Copy {
    IDirect3DSurface9 *face[kFaces] = {};
    bool valid = false;  // holds a complete cube map
};
Copy g_copy[kMaxPlayers];
IDirect3DSurface9 *g_game_face[kFaces] = {};  // the surfaces the copies are for (they change when the device is reset)

using UpdateEnvMapCamerasFn = void (*)();
UpdateEnvMapCamerasFn g_orig_update_env_map_cameras = nullptr;

void *view_at(int id) { return reinterpret_cast<void *>(kViews + id * kViewSize); }

void release_copies() {
    for (Copy &copy : g_copy) {
        for (auto &face : copy.face) {
            if (face) face->Release();
            face = nullptr;
        }
        copy.valid = false;
    }
    for (auto &face : g_game_face) face = nullptr;
}

// The surfaces the game draws the six faces into.
bool game_faces(IDirect3DSurface9 *out[kFaces]) {
    for (int i = 0; i < kFaces; ++i) {
        const uintptr_t target = *reinterpret_cast<uintptr_t *>(reinterpret_cast<uintptr_t>(view_at(kFirstFace + i)) + kViewRenderTarget);
        out[i] = target ? *reinterpret_cast<IDirect3DSurface9 **>(target + kTargetSurface) : nullptr;
        if (!out[i]) return false;
    }
    return true;
}

// The copies of `player` exist and are for the game's current surfaces.
bool ensure_copies(IDirect3DDevice9 *device, int player, IDirect3DSurface9 *faces[kFaces]) {
    if (!game_faces(faces)) return false;
    bool same = true;
    for (int i = 0; i < kFaces; ++i) same = same && faces[i] == g_game_face[i];
    if (!same) {
        release_copies();
        for (int i = 0; i < kFaces; ++i) g_game_face[i] = faces[i];
    }
    Copy &copy = g_copy[player];
    for (int i = 0; i < kFaces; ++i) {
        if (copy.face[i]) continue;
        D3DSURFACE_DESC desc;
        if (FAILED(faces[i]->GetDesc(&desc)) || FAILED(device->CreateRenderTarget(desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &copy.face[i], nullptr))) {
            g_log.line("reflections: no copy of the cube map faces: the faces are drawn in every pass");
            g_copies_wanted = false;
            return false;
        }
        if (i == 0 && player == 0) g_log.line("reflections: cube map faces %u x %u, format %d, one copy for each player", desc.Width, desc.Height, static_cast<int>(desc.Format));
    }
    return true;
}

// Whether the game draws face `i` this frame (the Active byte of its view, set by eEnvMap::UpdateCameras).
bool face_drawn(int i) { return *(reinterpret_cast<unsigned char *>(view_at(kFirstFace + i)) + kViewActive) != 0; }

// Copies the faces `wanted` says (null = all) from one set of surfaces to the other.
bool copy_faces(IDirect3DDevice9 *device, IDirect3DSurface9 *const from[kFaces], IDirect3DSurface9 *const to[kFaces], bool (*wanted)(int) = nullptr) {
    for (int i = 0; i < kFaces; ++i) {
        if (wanted && !wanted(i)) continue;
        if (SUCCEEDED(device->StretchRect(from[i], nullptr, to[i], nullptr, D3DTEXF_NONE))) continue;
        g_log.line("reflections: copying a cube map face failed: the faces are drawn in every pass");
        g_copies_wanted = false;
        return false;
    }
    return true;
}



void hook_update_env_map_cameras() {
    int &rate = *reinterpret_cast<int *>(kEnvMapEnabled);
    const int saved = rate;
    if (g_split && saved > 0) {
        if (!g_copies_wanted || !g_copy[g_player].valid) {
            rate = 3;
            g_pass_drew_all = true;
        } else if (g_players > 2 && g_rate_limit_many > 0 && saved > g_rate_limit_many) {
            rate = g_rate_limit_many;
        }
    }
    g_orig_update_env_map_cameras();
    rate = saved;
}

// eEnvMap::UpdateCameras(viewer position, cube map centre) is called by UpdateEnvironmentMapCameras (0x75611D) with the centre at the car the first
// camera mover of view 1 follows. In the passes of players 3 and later that is the car of player 2 (their views have no mover of the game own), so the
// centre is moved by the difference between their car and player 2 car. The viewer position comes from view 1 camera, which holds theirs in those passes.
uintptr_t g_env_update_cameras = kEnvUpdateCameras;
float g_env_shift[3] = {};
bool g_env_shift_on = false;

void __fastcall env_update_cameras(void *env_map, void *edx, float *viewer, float *centre) {
    float moved[4];
    if (g_env_shift_on) {
        memcpy(moved, centre, sizeof(moved));
        for (int i = 0; i < 3; ++i) moved[i] += g_env_shift[i];
        centre = moved;
    }
    reinterpret_cast<void(__fastcall *)(void *, void *, float *, float *)>(g_env_update_cameras)(env_map, edx, viewer, centre);
}

// The shift (render space) from the car of player 2 to the car of `player` (3rd or later), for the next update.
void aim_at(int player) {
    float own[3], own_forward[3], other[3], other_forward[3];
    g_env_shift_on = more_players::player_car_state(1, own, own_forward) && more_players::player_car_state(player, other, other_forward);
    if (!g_env_shift_on) return;
    const float delta[3] = {other[0] - own[0], other[1] - own[1], other[2] - own[2]};
    g_env_shift[0] = delta[2];  // simulation (x, y, z) -> render (z, -x, y)
    g_env_shift[1] = -delta[0];
    g_env_shift[2] = delta[1];
}

// What the game does once a frame for player 1's views (UpdateEnvironmentMapCameras), and what ___eDisplayFrame's loop does for them.
void update_for_view1() {
    if (!*reinterpret_cast<int *>(kEnvMapEnabled)) return;
    reinterpret_cast<void(__cdecl *)()>(kUpdateEnvMapCameras)();
    using SetupPerViewFn = void(__fastcall *)(void *plat_info, void *edx, void *view, int unused, unsigned far_plane, int zero, int one);
    const auto setup = reinterpret_cast<SetupPerViewFn>(kSetupPerView);
    for (int i = 0; i < kFaces; ++i) {
        void *view = view_at(kFirstFace + i);
        setup(*static_cast<void **>(view), nullptr, view, 0, kFarPlane, 0, 1);
    }
}

}  // namespace

void set_split(bool on) {
    g_split = on;
    if (!on) release_copies();
}

void begin_pass(IDirect3DDevice9 *device, int player, int player_count) {
    g_player = player < kMaxPlayers ? player : kMaxPlayers - 1;
    g_players = player_count;
    if (player >= 1) {  // player 1's faces were chosen by the game's own call at the start of the frame
        if (player >= 2) aim_at(player);
        update_for_view1();
        g_env_shift_on = false;
    }
    // The faces the game draws now need nothing: the others are this player's from the last pass.
    if (g_copies_wanted && g_copy[g_player].valid && *reinterpret_cast<int *>(kEnvMapEnabled)) {
        IDirect3DSurface9 *faces[kFaces];
        if (ensure_copies(device, g_player, faces) && g_copy[g_player].valid) copy_faces(device, g_copy[g_player].face, faces, [](int i) { return !face_drawn(i); });
    }
}

void end_pass(IDirect3DDevice9 *device, int player) {
    const int slot = player < kMaxPlayers ? player : kMaxPlayers - 1;
    const bool all_faces = g_pass_drew_all;
    g_pass_drew_all = false;
    g_player = 0;  // the game's own update call, at the start of the next frame, is for player 1
    if (!g_copies_wanted || !*reinterpret_cast<int *>(kEnvMapEnabled)) return;
    IDirect3DSurface9 *faces[kFaces];
    if (!ensure_copies(device, slot, faces)) return;
    if (!g_copy[slot].valid && !all_faces) return;
    const bool had_all = g_copy[slot].valid;
    if (copy_faces(device, faces, g_copy[slot].face, had_all ? face_drawn : nullptr)) g_copy[slot].valid = true;
}

void release() { release_copies(); }

void install() {
    g_copies_wanted = ini_flag("options", "reflection_copies");
    g_rate_limit_many = GetPrivateProfileIntA("options", "reflection_rate_four_players", 1, g_ini);
    const nfsmw::Hook hooks[] = {
        {"envmapcameras", "UpdateEnvironmentMapCameras", kUpdateEnvMapCameras, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x83}, false,
         nfsmw::detour(&hook_update_env_map_cameras), nfsmw::orig(&g_orig_update_env_map_cameras)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, nullptr, "reflections");
    if (const uintptr_t previous = nfsmw::chain_call(kEnvUpdateCamerasCall, reinterpret_cast<uintptr_t>(&env_update_cameras))) g_env_update_cameras = previous;
}

}  // namespace split_screen_support::reflections
