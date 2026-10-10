/*
 * Which device is whose.
 *
 * The Press Start screen (uiQRPressStart) is shown once per player. The game itself cannot tell who pressed: the PC
 * version has one input device, port 0, for the keyboard and the controllers. Here it is found out: every frame the four
 * XInput slots and the keyboard (through nfsmw_controller_support, which sees it) are read, and when the screen gets its
 * Start message (0xEBFCDA65) the device whose Start (Enter or Space on the keyboard) was pressed last, within 1.5 s,
 * becomes that player's. Nothing recent means the keyboard. (The game starts its Start action with Space on the keyboard,
 * not Enter, so Enter is turned into the Start message here: the screen is told when Enter is let go.)
 *
 * Which devices reach the game is set through nfsmw_controller_support (common/controller_api.h):
 *  - on a Press Start screen (CreateQRPressStart, 0x570C60): all of them, so that either player can press;
 *  - after a player pressed Start: that player's device alone, for the menus that follow (their car select). The other
 *    devices are ignored: a keyboard player's controllers do nothing, a controller player's keyboard does nothing;
 *  - when the race loads: player 1's device alone. Player 2's joystick port is set to -1 and they
 *    read a game input device of their own, made by nfsmw_controller_support from the bindings of player 2's device
 *    (without it they have no input). A player who did not press Start, or who pressed
 *    Start on the device the other player used, gets another one: a free controller, else the keyboard;
 *  - back in the menus (no longer a split screen game): as normal, all devices.
 * Without nfsmw_controller_support only the game's own device reaches it, for both players.
 */
#include "players.h"

#include "controller_api.h"
#include "events.h"
#include "hooks.h"
#include "split_screen_support.h"

namespace split_screen_support::players {

namespace xinput = nfsmw::xinput;
namespace api = nfsmw::controller_api;

namespace {

constexpr uintptr_t kFeDatabase = 0x91CF90;
constexpr uintptr_t kViewMode = 0x982B40;  // the current EVIEWMODE; 3 = two views
constexpr int kTwoViews = 3;
constexpr unsigned kStartReleased = 0xEBFCDA65;
constexpr int kNotChosen = -2;
constexpr int kEveryone = -3, kNormal = -4;  // for give_controls(): every device; back to the default
constexpr DWORD kStartWindow = 1500;         // ms from pressing Start to the screen reacting that still counts
constexpr int kKeysEnterSpace[] = {0x1C, 0x39, 0x9C};  // DirectInput codes: Enter, Space, numpad Enter
constexpr int kEnterKeys[] = {0x1C, 0x9C};             // Enter, numpad Enter

DWORD g_start_down_at[4];  // XInput slots
DWORD g_key_start_at = 0;  // the keyboard
bool g_connected[4];
DWORD g_next_scan = 0;
int g_chosen[2] = {kNotChosen, kNotChosen};  // at the Press Start screens
int g_race[2] = {kKeyboard, kKeyboard};       // in the race
bool g_controls_asked = false;                // nfsmw_controller_support was told what reaches the game
void *g_press_start_screen = nullptr;         // the Press Start screen showing, if any
bool g_enter_was_down = false;
enum class Phase { kMenus, kRace } g_phase = Phase::kMenus;  // kRace: the race of this split screen game is on
bool g_race_seen = false;                                    // ...and has been seen on screen (two views)

unsigned char *fe_database() { return *reinterpret_cast<unsigned char **>(kFeDatabase); }

void __fastcall hook_press_start(void *screen, void *edx, unsigned message, void *object, unsigned param1, unsigned param2);

const char *describe(int device) {
    static const char *const names[] = {"controller 0", "controller 1", "controller 2", "controller 3"};
    return device >= 0 && device < 4 ? names[device] : "the keyboard";
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
    unsigned char keys[256];
    if (!read_keyboard(keys)) return;
    static bool logged = false;
    if (!logged) {
        logged = true;
        g_log.line("the keyboard can be seen (through nfsmw_controller_support)");
    }
    for (const int key : kKeysEnterSpace)
        if (keys[key] & 0x80) g_key_start_at = now | 1;
    const bool enter_down = (keys[kEnterKeys[0]] | keys[kEnterKeys[1]]) & 0x80;
    if (g_enter_was_down && !enter_down && g_press_start_screen && split_screen())
        hook_press_start(g_press_start_screen, nullptr, kStartReleased, nullptr, 1, 0);  // joystick 0, as every press is
    g_enter_was_down = enter_down;
}

// What reaches the game: one device (a controller 0-3 or kKeyboard), every device, or the default.
void give_controls(int device) {
    static const api::PadsFn pads = api::find_pads();
    static const api::KeyboardFn keyboard = api::find_keyboard();
    static bool warned = false;
    g_controls_asked = device != kNormal;
    if (!pads || !keyboard) {
        if (!warned && g_controls_asked) {
            warned = true;
            g_log.line("nfsmw_controller_support is not loaded: only the game's own device reaches it, for both players");
        }
        return;
    }
    if (device >= 0) {
        pads(device);
        keyboard(0);
    } else if (device == kKeyboard) {
        pads(api::kNoPad);
        keyboard(1);
    } else if (device == kEveryone) {
        pads(api::kMergedPads);
        keyboard(1);
    } else {
        pads(api::kDefault);
        keyboard(1);
    }
}

// Player 2's input: a game input device of their own. nfsmw_controller_support makes it, fills it with what the Controls
// menu bindings of player 2's device ask for, and gives it to the player whose joystick port is -1 (their database port
// stays -1: the menus' input knows no other port and crashes on one), so the game's own input path serves them: steering,
// camera change, pause, rumble, the mouse look of a keyboard player. Returns false without nfsmw_controller_support.
bool start_second_player(int device) {
    static const api::ExtraDeviceFn extra_device = api::find_extra_device();
    if (!extra_device) {
        g_log.line("nfsmw_controller_support is not loaded: player 2 has no input");
        return false;
    }
    const int port = extra_device(device == kKeyboard ? api::kSourceKeyboard : device);
    g_log.line("player 2 reads %s", port >= 0 ? "a game input device of their own" : "no game input device (none could be made)");
    return port >= 0;
}

void end_second_player() {
    static const api::ExtraDeviceFn extra_device = api::find_extra_device();
    if (extra_device) extra_device(api::kSourceNone);
}

// A device that is not `taken`: a connected controller, else the keyboard (which may be the same: nothing else is there).
int other_device(int taken) {
    for (int i = 0; i < 4; ++i)
        if (g_connected[i] && i != taken) return i;
    return kKeyboard;
}

void on_world_load() {
    if (!split_screen()) return;
    poll();
    int p1 = g_chosen[0], p2 = g_chosen[1];  // a choice made at a Press Start screen wins over a default
    if (p1 == kNotChosen && p2 == kNotChosen) {
        p1 = other_device(-2);  // the first controller, else the keyboard
        p2 = other_device(p1);
    } else if (p1 == kNotChosen) {
        p1 = other_device(p2);
    } else if (p2 == kNotChosen) {
        p2 = other_device(p1);
    } else if (p1 == p2) {
        g_log.line("both players pressed Start on %s; player 2 gets another device", describe(p1));
        p2 = other_device(p1);
    }
    if (p1 == p2) g_log.line("both players are on the keyboard: they will drive together (connect a controller)");
    g_race[0] = p1;
    g_race[1] = p2;
    fe_database()[0x0D] = 0xFF;  // player 2's joystick port: -1 (see start_second_player)
    give_controls(p1);
    start_second_player(p2);
    g_phase = Phase::kRace;
    g_race_seen = false;
    g_log.line("split screen race: player 1 on %s, player 2 on %s", describe(p1), describe(p2));
}

void on_frame() {
    poll();
    const int view_mode = *reinterpret_cast<int *>(kViewMode);
    if (g_phase == Phase::kRace) {
        if (view_mode == kTwoViews) {
            g_race_seen = true;
        } else if (g_race_seen) {  // the race is over: the menus after it are everybody's again
            g_phase = Phase::kMenus;
            g_race_seen = false;
            give_controls(kNormal);
            end_second_player();
            g_log.line("the race is over: every device controls the menus again");
        }
    } else if (split_screen() && view_mode == kTwoViews) {  // a race started again without loading (a restart)
        g_phase = Phase::kRace;
        g_race_seen = true;
        give_controls(g_race[0]);
        start_second_player(g_race[1]);
        g_log.line("the race went on: player 1 on %s, player 2 on %s", describe(g_race[0]), describe(g_race[1]));
    }
    if (!split_screen() && (g_controls_asked || g_chosen[0] != kNotChosen || g_chosen[1] != kNotChosen)) {
        if (g_controls_asked) give_controls(kNormal);
        end_second_player();
        g_phase = Phase::kMenus;
        g_press_start_screen = nullptr;
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
        int device = kKeyboard;  // nothing recent: the keyboard, which cannot be seen without nfsmw_controller_support
        DWORD newest = 0;
        const auto consider = [&](int candidate, DWORD at) {
            if (at && now - at < kStartWindow && (newest == 0 || static_cast<LONG>(at - newest) > 0)) {
                device = candidate;
                newest = at;
            }
        };
        for (int i = 0; i < 4; ++i) consider(i, g_start_down_at[i]);
        consider(kKeyboard, g_key_start_at);
        if (newest) {  // the press is used up: the next player's own press has to be a new one
            if (device >= 0) g_start_down_at[device] = 0;
            else g_key_start_at = 0;
        }
        if (player == 0 || player == 1) {
            g_chosen[player] = device;
            g_log.line("Press Start: player %d on %s", player + 1, describe(device));
            give_controls(device);  // their menus, their device
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
        g_press_start_screen = screen;
        g_log.line("Press Start screen for player %d: every device can press",
                   *reinterpret_cast<int *>(static_cast<char *>(screen) + 0x2C) + 1);
        give_controls(kEveryone);
    }
    return screen;
}

// uiQRPressStart::~uiQRPressStart (0x7A3F10), thiscall: the screen is gone.
using DestructorFn = void(__fastcall *)(void *);
DestructorFn g_orig_press_start_destructor;
void __fastcall hook_press_start_destructor(void *screen) {
    if (screen == g_press_start_screen) g_press_start_screen = nullptr;
    g_orig_press_start_destructor(screen);
}

}  // namespace

bool split_screen() {
    const unsigned char *db = fe_database();
    return db && db[0] == 2 && (*reinterpret_cast<const unsigned *>(db + 0x12C) & 4);
}

int device(int player) { return player == 0 || player == 1 ? g_race[player] : kKeyboard; }

bool read_keyboard(unsigned char *out) {
    static const api::KeyboardStateFn fn = api::find_keyboard_state();
    return fn && fn(out);
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
        {"pressstartend", "uiQRPressStart::~uiQRPressStart", 0x7A3F10, {0xC7, 0x01, 0xCC, 0x72, 0x8B, 0x00, 0xE9}, false,
         nfsmw::detour(&hook_press_start_destructor), nfsmw::orig(&g_orig_press_start_destructor)},
    };
    nfsmw::install_hooks(hooks, 3, g_log, g_ini, "players");
}

}  // namespace split_screen_support::players
