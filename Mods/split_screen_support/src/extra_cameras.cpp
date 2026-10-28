/*
 * The game makes a camera for each local player with render port 1 or 2: CameraAI::Update (0x480E40) loops over those two view ids, and for each makes a
 * CameraAI::Director (0x2C8 bytes, constructor 0x480D20 with the view id) that picks an action by name; "CDActionDrive" (constructor 0x484C20 with the
 * director and the player) makes the CameraAnchor of the car and a CubicCameraMover that places the camera of the view every frame, from the player's
 * chosen camera view (their PlayerSettings' CurCam). It is also where nfsmw_camera_settings reaches the cameras (CameraAnchor::GetPov and the mover's
 * springs), and where the look, the look-back and the field of view with speed come from, so players 3 and 4 get the same director, with one of ours in
 * the loop: a view of its own for each (nfsmw::views::kPlayerView: views nothing uses), a camera and PlatInfo copied from view 2's, the director made
 * as the game's loop would, with the extra player as the only local player and the view as its render port for the moment the action looks for them.
 * CameraAI::Update then updates the directors of all views, and UpdateCameraMovers (0x47C880) the first mover of every view.
 * The action also makes the rear view mirror's mover in view 3, whose list the mirror of player 1 is the first of; for the extra players it is removed.
 * Drawing: split_view.cpp trades the view for view 1 for the pass, as it does with view 2 for player 2.
 */
#include "extra_cameras.h"

#include <cstring>

#include "events.h"
#include "layout.h"
#include "max_players.h"
#include "hooks.h"
#include "more_players.h"
#include "players.h"
#include "split_screen_support.h"
#include "view_ids.h"

namespace split_screen_support::extra_cameras {

namespace {

constexpr uintptr_t kViews = 0x9195E0, kFastMem = 0x925B30, kFastMemAlloc = 0x5D29D0, kDirectorConstructor = 0x480D20, kSetAction = 0x4797F0,
                    kStringHash64 = 0x4522F0, kStringHash32 = 0x4519D0, kActionName = 0x894C20;  // "CDActionDrive"
constexpr uintptr_t kSetupLighting = 0x5029F0, kSetupPerView = 0x6CF400;
constexpr unsigned kFarPlane = 0x466A6000;  // 15000.0f, as eDisplayFrame passes it
constexpr size_t kDirectorSize = 0x2C8, kCameraSize = 0x2C0, kPlatInfoSize = 0x1A0;
constexpr int kViewPlatInfo = 0x00, kViewId = 0x04, kViewCamera = 0x40, kViewMoverList = 0x44, kViewRenderTarget = 0x60;
constexpr int kDirectorAction = 0x20, kActionCubicMover = 0x28, kActionRearViewMover = 0x2C, kActionAnchor = 0x30, kActionPlayer = 0x44;
constexpr uintptr_t kIntroFlags[] = {0x911054, 0x911055};  // bytes the CDActionDrive constructor clears

struct Extra {
    void *camera = nullptr, *plat_info = nullptr;
    void *director = nullptr;
};
Extra g_extra[nfsmw::kMaxExtras];

uintptr_t view_address(int index) { return kViews + 0x70 * nfsmw::views::kPlayerView[2 + index]; }

template <typename T>
T &at(uintptr_t base, int offset) {
    return *reinterpret_cast<T *>(base + offset);
}

void *copy_of(void *original, size_t size) {
    void *copy = original ? VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) : nullptr;
    if (copy) memcpy(copy, original, size);
    return copy;
}

// The view gets a camera and PlatInfo (copies of view 2's) and view 1's render target, which the per view setup reads the size of.
bool set_up_view(int index) {
    Extra &e = g_extra[index];
    const uintptr_t view = view_address(index), view2 = kViews + 0x70 * 2, view1 = kViews + 0x70;
    if (!e.camera) e.camera = copy_of(at<void *>(view2, kViewCamera), kCameraSize);
    if (!e.plat_info) e.plat_info = copy_of(at<void *>(view2, kViewPlatInfo), kPlatInfoSize);
    if (!e.camera || !e.plat_info) return false;
    at<void *>(view, kViewCamera) = e.camera;
    at<void *>(view, kViewPlatInfo) = e.plat_info;
    at<void *>(view, kViewRenderTarget) = at<void *>(view1, kViewRenderTarget);
    return true;
}

// The directors of the game: an array of pointers (0x911300) and its count (0x911308). The game deletes all of them when a race is left, ours too.
bool in_game_list(void *director) {
    void *const *list = *reinterpret_cast<void *const *const *>(0x911300);
    const unsigned count = *reinterpret_cast<const unsigned *>(0x911308);
    for (unsigned i = 0; list && i < count; ++i)
        if (list[i] == director) return true;
    return false;
}

// The player (0-based) whose camera is the anchor: the game's list of directors has them all (ours too), each director's action holds the anchor (+0x30) and
// the player (+0x44).
int player_of_anchor(void *anchor) {
    void *const *list = *reinterpret_cast<void *const *const *>(0x911300);
    const unsigned count = *reinterpret_cast<const unsigned *>(0x911308);
    for (unsigned i = 0; list && i < count; ++i) {
        void *action = list[i] ? at<void *>(reinterpret_cast<uintptr_t>(list[i]), kDirectorAction) : nullptr;
        if (!action || at<void *>(reinterpret_cast<uintptr_t>(action), kActionAnchor) != anchor) continue;
        void *const player = at<void *>(reinterpret_cast<uintptr_t>(action), kActionPlayer);
        for (int p = 0; p < nfsmw::kMaxPlayers; ++p)
            if (player && more_players::game_player(p) == player) return p;
        return -1;
    }
    return -1;
}

void destroy(int index) {
    Extra &e = g_extra[index];
    if (!e.director) return;
    if (!in_game_list(e.director)) {
        e.director = nullptr;
        g_log.line("extra cameras: the game took the camera of player %d away", index + 3);
        return;
    }
    void **vtable = *reinterpret_cast<void ***>(e.director);
    reinterpret_cast<void(__fastcall *)(void *, void *, int)>(vtable[0])(e.director, nullptr, 1);  // the deleting destructor: the action and its movers go too
    e.director = nullptr;
    at<void *>(view_address(index), kViewRenderTarget) = nullptr;
    g_log.line("extra cameras: the camera of player %d is gone", index + 3);
}

void create(int index) {
    Extra &e = g_extra[index];
    if (!set_up_view(index)) return;
    const int view_id = nfsmw::views::kPlayerView[nfsmw::kPlayersPerRound + index];
    unsigned char intro[2];
    for (int i = 0; i < 2; ++i) intro[i] = *reinterpret_cast<unsigned char *>(kIntroFlags[i]);
    void *director = reinterpret_cast<void *(__fastcall *)(void *, void *, unsigned, const char *)>(kFastMemAlloc)(reinterpret_cast<void *>(kFastMem), nullptr, kDirectorSize, nullptr);
    if (!director) return;
    reinterpret_cast<void *(__fastcall *)(void *, void *, int)>(kDirectorConstructor)(director, nullptr, view_id);
    // SetAction makes the action by its name, which looks for the local player whose render port is the director's view.
    const auto name = reinterpret_cast<const char *>(kActionName);
    const unsigned long long hash64 = reinterpret_cast<unsigned long long(__cdecl *)(const char *)>(kStringHash64)(name);
    const unsigned hash32 = reinterpret_cast<unsigned(__cdecl *)(const char *)>(kStringHash32)(name);
    more_players::make_only_local_player(index, true);
    reinterpret_cast<void(__fastcall *)(void *, void *, unsigned, unsigned, unsigned, const char *)>(kSetAction)(
        director, nullptr, static_cast<unsigned>(hash64), static_cast<unsigned>(hash64 >> 32), hash32, name);
    more_players::make_only_local_player(index, false);
    for (int i = 0; i < 2; ++i) *reinterpret_cast<unsigned char *>(kIntroFlags[i]) = intro[i];  // the race's own countdown view must not be reset by this
    e.director = director;
    void *action = at<void *>(reinterpret_cast<uintptr_t>(director), kDirectorAction);
    void *mover = action ? at<void *>(reinterpret_cast<uintptr_t>(action), kActionCubicMover) : nullptr;
    if (!mover) {
        g_log.line("extra cameras: the director of player %d (view %d) made no camera mover", index + 3, view_id);
        destroy(index);
        return;
    }
    void *&mirror = at<void *>(reinterpret_cast<uintptr_t>(action), kActionRearViewMover);
    if (mirror) {  // a second rear view mirror mover at the front of view 3's list would take the mirror from player 1
        void **vtable = *reinterpret_cast<void ***>(mirror);
        reinterpret_cast<void(__fastcall *)(void *, void *, int)>(vtable[1])(mirror, nullptr, 1);
        mirror = nullptr;
    }
    g_log.line("extra cameras: the camera of player %d: director %p, action %p, camera mover %p, view %d", index + 3, director, action, mover, view_id);
}

void (__cdecl *g_orig_camera_ai_update)(float);

// The game's loop makes the directors of its two views; ours make the others', before the game updates them all.
void __cdecl hook_camera_ai_update(float dt) {
    for (int i = 0; i < nfsmw::kMaxExtras; ++i) {
        if (g_extra[i].director && !in_game_list(g_extra[i].director)) destroy(i);
        const bool wanted = players::split_screen() && more_players::extra_ready(i);
        if (wanted && !g_extra[i].director) create(i);
        else if (!wanted && g_extra[i].director) destroy(i);
    }
    g_orig_camera_ai_update(dt);
}

// A race is about to load: the directors of the one before are gone with it (before the game makes the new ones, which may take the place of ours).
void on_world_load() {
    for (int i = 0; i < nfsmw::kMaxExtras; ++i) destroy(i);
}

}  // namespace

void *prepare_pass(int index) {
    const uintptr_t view = view_address(index);
    const auto *list = reinterpret_cast<const void *const *>(view + kViewMoverList);
    if (!g_extra[index].director || list[0] == list) return nullptr;  // no camera mover yet
    // The per view setup of the game treats the views with ids 1, 2, 4 and 5 as player views; the id is the player 2's while it runs.
    int &id = at<int>(view, kViewId);
    const int saved = id;
    id = 2;
    reinterpret_cast<void(__fastcall *)(void *, void *, void *, int, unsigned, int, int)>(kSetupPerView)(at<void *>(view, kViewPlatInfo), nullptr, reinterpret_cast<void *>(view), 0, kFarPlane, 0, 1);
    reinterpret_cast<void(__fastcall *)(void *, void *)>(kSetupLighting)(reinterpret_cast<void *>(view), nullptr);
    id = saved;
    return reinterpret_cast<void *>(view);
}

int wide_picture(void *anchor) {
    const int players = more_players::race_players();
    if (!players::split_screen() || players < 2) return -1;
    const int player = player_of_anchor(anchor);
    if (player < 0 || player >= players) return -1;  // (a camera just made is not in the director's action yet)
    const RECT cell = layout::cell(player, players, 1000, 1000);  // a square "screen": a cell wider than 1.5 times its height is wider than the screen's own
    return 2 * (cell.right - cell.left) > 3 * (cell.bottom - cell.top) ? 1 : 0;
}

void install() {
    events::on_world_load(&on_world_load);
    const nfsmw::Hook hooks[] = {
        {"cameraai", "CameraAI::Update", 0x480E40, {0x6A, 0xFF, 0x68, 0xD2, 0xA5, 0x86, 0x00}, false,
         nfsmw::detour(&hook_camera_ai_update), nfsmw::orig(&g_orig_camera_ai_update)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, g_ini, "extra cameras");
}

}  // namespace split_screen_support::extra_cameras
