/*
 * The intro cutscene of a race (IntroNis01..10, IntroNisDD, IntroNisBL..., IntroNisToll01: the scene is named by the race's script, which calls the Lua binding
 * `NIS_Play(instance, scene, ...)` 0x604B80; that makes an EPlayRaceNIS event (0x640F00), which makes an NISActivity and starts it with `NISActivity::Load` 0x6F51F0).
 * The cutscene is what puts the first four cars of a race in the grid (start_grid.cpp). A split screen race asks for it as any other, but Load ends the cutscene at once
 * (its state, +0x2AC of the activity, is set to 7: "ended") when `[0x9260C0] != 0 || [0x988E04] == 1`: the second is the game mode flag of a split screen game (the one
 * more_players.cpp clears while it makes the extra players) [tested: the cutscene then plays, in each player's view]. Load is called with that flag cleared.
 * The cutscene is for the whole screen: split_view.cpp draws one picture while `playing()`, and the split starts when the cutscene is over. The activity's state
 * (+0x2EC of its task interface, the `this` of OnTask, 0x6F58D0) is 3 while it waits for its scene, 5 while it plays (NISActivity::Play 0x6F5750 starts that), 6 and 7 at the end.
 */
#include "race_intro.h"

#include <windows.h>

#include "hooks.h"
#include "split_screen_support.h"
#include "start_grid.h"

namespace split_screen_support::race_intro {

namespace {

constexpr uintptr_t kNisPlay = 0x604B80, kActivityLoad = 0x6F51F0, kActivityPlay = 0x6F5750, kOnTask = 0x6F58D0, kSplitScreenFlag = 0x988E04, kNisSingleton = 0x9885C8;
constexpr unsigned kTaskState = 0x2EC;

bool g_let_it_play = true;
bool g_playing = false;

void (__cdecl *g_orig_nis_play)(void *, const char *, const char *, int, const char *, const char *);
void(__fastcall *g_orig_load)(void *, void *, int, int, int, int);
void(__fastcall *g_orig_play)(void *, void *);
unsigned char(__fastcall *g_orig_on_task)(void *, void *, void *, float);

const char *text(const char *s) { return !s ? "(none)" : IsBadReadPtr(s, 1) ? "(?)" : s; }

void __cdecl hook_nis_play(void *instance, const char *scene, const char *kind, int c, const char *d, const char *e) {
    g_log.line("cutscene: the race asks for \"%.40s\" (%.20s)", text(scene), text(kind));
    g_orig_nis_play(instance, scene, kind, c, d, e);
}

// NISActivity::Load(kind, name, ...) (thiscall, ret 0x10).
void __fastcall hook_load(void *self, void *edx, int a, int b, int c, int d) {
    int &flag = *reinterpret_cast<int *>(kSplitScreenFlag);
    const int saved = flag;
    if (saved == 1 && g_let_it_play) flag = 0;
    g_orig_load(self, edx, a, b, c, d);
    flag = saved;
}

void __fastcall hook_play(void *self, void *edx) {
    g_playing = true;
    g_log.line("cutscene: plays");
    g_orig_play(self, edx);
}

// The task of the cutscene, every frame: it is over when its state is 6 or 7.
unsigned char __fastcall hook_on_task(void *self, void *edx, void *task, float dt) {
    const unsigned char result = g_orig_on_task(self, edx, task, dt);
    const int state = *reinterpret_cast<const int *>(static_cast<const char *>(self) + kTaskState);
    if (g_playing && (state == 6 || state == 7)) {
        g_playing = false;
        g_log.line("cutscene: over");
        start_grid::cutscene_over();
    }
    return result;
}

}  // namespace

bool playing() { return g_playing && *reinterpret_cast<void **>(kNisSingleton) != nullptr; }

void install() {
    g_let_it_play = ini_flag("options", "intro_cutscene", true);
    const nfsmw::Hook hooks[] = {
        {"nisplay", "NIS_Play", kNisPlay, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A}, false, nfsmw::detour(&hook_nis_play), nfsmw::orig(&g_orig_nis_play)},
        {"nisload", "NISActivity::Load", kActivityLoad, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x6A}, false, nfsmw::detour(&hook_load), nfsmw::orig(&g_orig_load)},
        {"nisplayact", "NISActivity::Play", kActivityPlay, {0x6A, 0xFF, 0x68, 0xDF, 0xEB, 0x87, 0x00}, false, nfsmw::detour(&hook_play), nfsmw::orig(&g_orig_play)},
        {"nistask", "NISActivity::OnTask", kOnTask, {0x8B, 0x44, 0x24, 0x04, 0x56, 0x8B, 0xF1}, false, nfsmw::detour(&hook_on_task), nfsmw::orig(&g_orig_on_task)},
    };
    nfsmw::install_hooks(hooks, 4, g_log, g_ini, "race intro");
}

}  // namespace split_screen_support::race_intro
