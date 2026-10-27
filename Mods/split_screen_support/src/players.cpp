/*
 * Which device is whose.
 *
 * The Press Start screen (uiQRPressStart) is shown once per player. The game itself cannot tell who pressed: the PC
 * version has one input device, port 0, for the keyboard and the controllers, and which of them ends in its Start
 * message depends on the bindings. So the game's own Start messages are ignored here, and the screen is told by this part:
 * every frame the four XInput slots and the keyboard (through mets_controller_support, which sees it) are read, and when a
 * controller lets go of Start, or Enter, numpad Enter or Space is let go, the screen gets the Start message (0xEBFCDA65)
 * and that device becomes that player's.
 *
 * Every device always reaches the game's menus (the Press Start screens, the car select, the pause menu with its Controls
 * screen). What is told to mets_controller_support (common/controller_api.h) is only who drives the vehicles:
 *  - when the race loads: player 1's vehicle (and its camera, HUD actions and pause request) takes its input from player 1's
 *    device alone. Player 2's joystick port is set to -1 and they read a game input device of their own, made by
 *    mets_controller_support from the bindings of player 2's device (without it they have no input). A device that already
 *    has a player does nothing on the other player's Press Start screen; a player who somehow did not press Start gets
 *    another device: a free controller, else the keyboard;
 *  - back in the menus (no longer a split screen game): every device drives the vehicle again.
 * Without mets_controller_support every device drives player 1's vehicle, and player 2 has no input.
 */
#include "players.h"

#include "controller_api.h"
#include "events.h"
#include "hooks.h"
#include "more_players.h"
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
constexpr int kStartKeys[] = {0x1C, 0x39, 0x9C};  // DirectInput codes: Enter, Space, numpad Enter

bool g_start_was_down[4];  // XInput slots
bool g_key_was_down = false;
bool g_connected[4];
DWORD g_next_scan = 0;
int g_chosen[2] = {kNotChosen, kNotChosen};  // at the Press Start screens
int g_race[4] = {kKeyboard, kKeyboard, kKeyboard, kKeyboard};  // in the race
int g_extra_port[2] = {-1, -1};               // the game input device ports of players 3 and 4
bool g_controls_asked = false;                // mets_controller_support was told what reaches the game
void *g_press_start_screen = nullptr;         // the Press Start screen showing, if any
int g_pressed_by = kNotChosen;  // the device whose Start the screen is being told about (see press_start)
enum class Phase { kMenus, kRace } g_phase = Phase::kMenus;  // kRace: the race of this split screen game is on
bool g_race_seen = false;                                    // ...and has been seen on screen (two views)

unsigned char *fe_database() { return *reinterpret_cast<unsigned char **>(kFeDatabase); }

void __fastcall hook_press_start(void *screen, void *edx, unsigned message, void *object, unsigned param1, unsigned param2);

const char *describe(int device) {
    static const char *const names[] = {"controller 0", "controller 1", "controller 2", "controller 3"};
    return device >= 0 && device < 4 ? names[device] : "the keyboard";
}

// The Press Start screen is told that a device let go of Start.
void press_start(int device) {
    if (!g_press_start_screen || !split_screen()) return;
    g_pressed_by = device;
    hook_press_start(g_press_start_screen, nullptr, kStartReleased, nullptr, 1, 0);  // joystick 0, as every press is
    g_pressed_by = kNotChosen;
}

void poll() {
    const DWORD now = GetTickCount();
    const bool scan = static_cast<LONG>(now - g_next_scan) >= 0;
    if (scan) g_next_scan = now + 2000;
    int released = kNotChosen;
    for (int i = 0; i < 4; ++i) {
        if (!g_connected[i] && !scan) continue;  // asking an empty slot is slow
        xinput::Gamepad p;
        const bool was = g_connected[i];
        g_connected[i] = xinput::read_slot(i, &p);
        if (g_connected[i] != was) g_log.line("XInput controller %d %s", i, g_connected[i] ? "connected" : "disconnected");
        const bool down = g_connected[i] && (p.buttons & xinput::kStart);
        if (g_start_was_down[i] && !down && released == kNotChosen) released = i;
        g_start_was_down[i] = down;
    }
    unsigned char keys[256];
    if (read_keyboard(keys)) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            g_log.line("the keyboard can be seen (through mets_controller_support)");
        }
        bool down = false;
        for (const int key : kStartKeys) down |= (keys[key] & 0x80) != 0;
        if (g_key_was_down && !down && released == kNotChosen) released = kKeyboard;
        g_key_was_down = down;
    }
    if (released != kNotChosen) press_start(released);
}

// Which device drives player 1's vehicle: one device (a controller 0-3 or kKeyboard), or every device. All devices reach
// the game's menus in any case.
void give_controls(int device) {
    static const api::VehicleSourceFn vehicle = api::find_vehicle_source();
    static bool warned = false;
    g_controls_asked = device != kNormal;
    if (!vehicle) {
        if (!warned && g_controls_asked) {
            warned = true;
            g_log.line("mets_controller_support is not loaded: every device drives player 1's vehicle, and player 2 has no input");
        }
        return;
    }
    vehicle(device == kKeyboard ? api::kSourceKeyboard : device >= 0 ? device : api::kDefault);
}

// Player 2's input: a game input device of their own. mets_controller_support makes it, fills it with what the Controls
// menu bindings of player 2's device ask for, and gives it to the player whose joystick port is -1 (their database port
// stays -1: the menus' input knows no other port and crashes on one), so the game's own input path serves them: steering,
// camera change, pause, rumble, the mouse look of a keyboard player. Returns false without mets_controller_support.
bool start_second_player(int device) {
    static const api::ExtraDeviceFn extra_device = api::find_extra_device();
    if (!extra_device) {
        g_log.line("mets_controller_support is not loaded: player 2 has no input");
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

// A device that is none of those of the first `count` players of the race.
int free_device(int count) {
    for (int i = 0; i < 4; ++i) {
        bool taken = false;
        for (int p = 0; p < count; ++p) taken = taken || g_race[p] == i;
        if (g_connected[i] && !taken) return i;
    }
    return kKeyboard;
}

// Players 3 and 4 read game input devices of their own, like player 2 (slots 1 and 2 of mets_controller_support's extra devices).
void start_extra_players() {
    static const api::ExtraDeviceSlotFn slot = api::find_extra_device_slot();
    for (int i = 0; i < 2; ++i) {
        int device = more_players::pick_device(2 + i);
        for (int p = 0; p < 2 + i; ++p)
            if (device == g_race[p]) device = kNotChosen;  // a device that has a player already
        if (device == kNotChosen) device = free_device(2 + i);
        g_race[2 + i] = device;
        g_extra_port[i] = slot ? slot(1 + i, device == kKeyboard ? api::kSourceKeyboard : device) : -1;
        g_log.line("player %d reads %s (%s)", 3 + i, describe(device), g_extra_port[i] >= 0 ? "a game input device of their own" : "no game input device");
    }
}

void end_extra_players() {
    static const api::ExtraDeviceSlotFn slot = api::find_extra_device_slot();
    for (int i = 0; i < 2; ++i) {
        if (slot) slot(1 + i, api::kSourceNone);
        g_extra_port[i] = -1;
    }
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
    if (more_players::four_player_race()) start_extra_players();
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
            end_extra_players();
            g_log.line("the race is over: every device controls the menus again");
        }
    } else if (split_screen() && view_mode == kTwoViews) {  // a race started again without loading (a restart)
        g_phase = Phase::kRace;
        g_race_seen = true;
        give_controls(g_race[0]);
        start_second_player(g_race[1]);
        if (more_players::four_player_race()) start_extra_players();
        g_log.line("the race went on: player 1 on %s, player 2 on %s", describe(g_race[0]), describe(g_race[1]));
    }
    if (!split_screen() && (g_controls_asked || g_chosen[0] != kNotChosen || g_chosen[1] != kNotChosen)) {
        if (g_controls_asked) give_controls(kNormal);
        end_second_player();
        end_extra_players();
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
    if (message == kStartReleased && split_screen()) {
        if (g_pressed_by == kNotChosen) return;  // the game's own Start message: it cannot tell the devices apart, press_start() does
        const int player = *reinterpret_cast<int *>(static_cast<char *>(screen) + 0x2C);
        if ((player == 0 || player == 1) && g_chosen[1 - player] == g_pressed_by) {
            g_log.line("Press Start: %s already plays player %d; ignored", describe(g_pressed_by), 2 - player);
            return;  // a device that already has a player does nothing: the screen stays
        }
        if (more_players::round() == 1 && more_players::device_used_in_first_round(g_pressed_by)) {
            g_log.line("Press Start: %s already plays in the first round; ignored", describe(g_pressed_by));
            return;
        }
        if (player == 0 || player == 1) {
            g_chosen[player] = g_pressed_by;
            g_log.line("Press Start: player %d on %s", player + 1, describe(g_pressed_by));
            give_controls(g_pressed_by);  // their car
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
        more_players::on_press_start_screen(screen);
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

void tell_press_start(int device) { press_start(device); }

void get_chosen(int out[2]) {
    out[0] = g_chosen[0];
    out[1] = g_chosen[1];
}

void set_chosen(const int in[2]) {
    g_chosen[0] = in[0];
    g_chosen[1] = in[1];
}

int device(int player) { return player >= 0 && player < 4 ? g_race[player] : kKeyboard; }

int extra_port(int extra) { return extra >= 0 && extra < 2 ? g_extra_port[extra] : -1; }

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
    nfsmw::install_hooks(hooks, 3, g_log, nullptr, "players");
}

}  // namespace split_screen_support::players
