/*
 * Which controller is whose.
 *
 * The Press Start screen (uiQRPressStart) is shown once per player. The game itself cannot tell who pressed: the PC
 * version has one input device, port 0, for the keyboard and the controllers, and it reads only the first controller
 * DirectInput gave it (so on its own, both players must press Start on that one). XInput can tell: every frame the four
 * XInput slots are read, and when the screen gets its Start message (0xEBFCDA65) the controller whose Start was down last
 * (within 1.5 s) becomes that player's; none means the keyboard (Enter).
 *
 * Which controllers reach the game is set through nfsmw_controller_support (common/controller_api.h):
 *  - on a Press Start screen (CreateQRPressStart, 0x570C60): all of them, merged, so that either player's can press;
 *  - after a player pressed Start: that player's controller alone (or none, for the keyboard), so player 2 picks their
 *    car with their own controller;
 *  - when the race loads: player 1's alone. Player 2's joystick port is set to -1 (no game device; player2_input.cpp
 *    drives that car from player 2's controller). A player who did not press Start gets the first free controller;
 *  - back in the menus (no longer a split screen game): as normal.
 * Without nfsmw_controller_support, only the game's own controller reaches it, for both players.
 */
#include "players.h"

#include "controller_api.h"
#include "events.h"
#include "hooks.h"
#include "split_screen_support.h"

namespace split_screen_support::players {

namespace xinput = nfsmw::xinput;

namespace {

constexpr uintptr_t kFeDatabase = 0x91CF90;
constexpr unsigned kStartReleased = 0xEBFCDA65;
constexpr int kNotChosen = -2;

DWORD g_start_down_at[4];
bool g_connected[4];
DWORD g_next_scan = 0;
int g_chosen[2] = {kNotChosen, kNotChosen};  // at the Press Start screens
int g_race[2] = {kKeyboard, kKeyboard};       // in the race
bool g_controllers_asked = false;  // nfsmw_controller_support was told which controllers reach the game

unsigned char *fe_database() { return *reinterpret_cast<unsigned char **>(kFeDatabase); }

const char *describe(int slot) {
    static const char *const names[] = {"controller 0", "controller 1", "controller 2", "controller 3"};
    return slot >= 0 && slot < 4 ? names[slot] : "the keyboard";
}

void poll() {
    const DWORD now = GetTickCount();
    const bool scan = static_cast<LONG>(now - g_next_scan) >= 0;
    if (scan) g_next_scan = now + 2000;
    for (int i = 0; i < 4; ++i) {
        if (!g_connected[i] && !scan) continue;  // asking an empty slot is slow
        xinput::Gamepad p;
        const bool was = g_connected[i];
        g_connected[i] = xinput::read_slot(i, &p);
        if (g_connected[i] != was) g_log.line("XInput controller %d %s", i, g_connected[i] ? "connected" : "disconnected");
        if (g_connected[i] && (p.buttons & xinput::kStart)) g_start_down_at[i] = now | 1;  // never 0
    }
}

void reach_game(int controllers) {
    static const nfsmw::controller_api::ExclusivePadFn fn = nfsmw::controller_api::find_exclusive_pad();
    static bool warned = false;
    g_controllers_asked = controllers != nfsmw::controller_api::kEveryPad;
    if (fn) return fn(controllers);
    if (!warned && g_controllers_asked) {
        warned = true;
        g_log.line("nfsmw_controller_support is not loaded: only the game's own controller reaches it, for both players");
    }
}

int first_free_pad(int taken) {
    for (int i = 0; i < 4; ++i)
        if (g_connected[i] && i != taken) return i;
    return kKeyboard;
}

void on_world_load() {
    if (!split_screen()) return;
    poll();
    int p1 = g_chosen[0], p2 = g_chosen[1];  // a choice made at a Press Start screen wins over a default
    if (p1 == kNotChosen && p2 == kNotChosen) {
        p1 = first_free_pad(kKeyboard);
        p2 = first_free_pad(p1);
    } else if (p1 == kNotChosen) {
        p1 = first_free_pad(p2);
    } else if (p2 == kNotChosen) {
        p2 = first_free_pad(p1);
    } else if (p1 == p2 && p1 != kKeyboard) {
        g_log.line("both players pressed Start on controller %d; player 2 gets another one", p1);
        p2 = first_free_pad(p1);
    }
    if (p1 == kKeyboard && p2 == kKeyboard) g_log.line("both players on the keyboard: player 2 cannot drive (connect a controller)");
    g_race[0] = p1;
    g_race[1] = p2;
    fe_database()[0x0D] = 0xFF;  // player 2's joystick port: -1, no game device
    reach_game(p1 >= 0 ? p1 : nfsmw::controller_api::kNoPad);
    g_log.line("split screen race: player 1 on %s, player 2 on %s", describe(p1), describe(p2));
}

void on_frame() {
    poll();
    if (g_controllers_asked && !split_screen()) {
        reach_game(nfsmw::controller_api::kEveryPad);
        g_chosen[0] = g_chosen[1] = kNotChosen;
        g_log.line("split screen over");
    }
}

// uiQRPressStart::NotificationMessage(uint32 message, FEObject *object, uint32 param1, uint32 param2), thiscall
using PressStartFn = void(__fastcall *)(void *screen, void *, unsigned, void *, unsigned, unsigned);
PressStartFn g_orig_press_start;
void __fastcall hook_press_start(void *screen, void *edx, unsigned message, void *object, unsigned param1, unsigned param2) {
    if (message == kStartReleased) {
        const int player = *reinterpret_cast<int *>(static_cast<char *>(screen) + 0x2C);
        const DWORD now = GetTickCount();
        int slot = kKeyboard;
        DWORD newest = 0;
        for (int i = 0; i < 4; ++i) {
            const DWORD at = g_start_down_at[i];
            if (at && now - at < 1500 && (slot == kKeyboard || static_cast<LONG>(at - newest) > 0)) {
                slot = i;
                newest = at;
            }
        }
        if (player == 0 || player == 1) {
            g_chosen[player] = slot;
            g_log.line("Press Start: player %d on %s", player + 1, describe(slot));
            reach_game(slot >= 0 ? slot : nfsmw::controller_api::kNoPad);  // their screens, their controller
        }
    }
    g_orig_press_start(screen, edx, message, object, param1, param2);
}

// MenuScreen *CreateQRPressStart(ScreenConstructorData *): a Press Start screen; the player is at screen+0x2C.
using CreatePressStartFn = void *(*)(void *);
CreatePressStartFn g_orig_create_press_start;
void *hook_create_press_start(void *data) {
    void *screen = g_orig_create_press_start(data);
    if (screen && split_screen()) {
        g_log.line("Press Start screen for player %d: every controller can press", *reinterpret_cast<int *>(static_cast<char *>(screen) + 0x2C) + 1);
        reach_game(nfsmw::controller_api::kMergedPads);
    }
    return screen;
}

}  // namespace

bool split_screen() {
    const unsigned char *db = fe_database();
    return db && db[0] == 2 && (*reinterpret_cast<const unsigned *>(db + 0x12C) & 4);
}

int pad(int player) { return player == 0 || player == 1 ? g_race[player] : kKeyboard; }

bool read_pad(int player, xinput::Gamepad *out) {
    const int slot = pad(player);
    return slot >= 0 && xinput::read_slot(slot, out);
}

void install() {
    if (!xinput::load(&g_log)) {
        g_log.line("players: no XInput, controllers cannot be told apart");
        return;
    }
    events::on_frame(&on_frame);
    events::on_world_load(&on_world_load);
    const nfsmw::Hook hooks[] = {
        {"pressstart", "uiQRPressStart::NotificationMessage", 0x7A3F20, {0x8B, 0x44, 0x24, 0x04, 0x3D, 0x64, 0xB3}, false,
         nfsmw::detour(&hook_press_start), nfsmw::orig(&g_orig_press_start)},
        {"createpressstart", "CreateQRPressStart", 0x570C60, {0x6A, 0xFF, 0x68, 0xB4, 0x17, 0x87, 0x00}, false,
         nfsmw::detour(&hook_create_press_start), nfsmw::orig(&g_orig_create_press_start)},
    };
    nfsmw::install_hooks(hooks, 2, g_log, g_ini, "players");
}

}  // namespace split_screen_support::players
