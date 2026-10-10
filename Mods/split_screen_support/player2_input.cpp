/*
 * Each human-driven car has an InputPlayer, whose FetchInput (0x6A8870, called with its IInputPlayer part as `this`) runs
 * every simulation step: it points its action queue at the player's joystick port, takes the actions waiting there
 * and turns what it has into the car's controls. Player 2's port is -1 in a split screen race (players.cpp), so there
 * are no actions; this fills in what the actions would have, from player 2's device (a controller, or the keyboard),
 * before the game's code runs:
 *
 *   this+0x2A0 / +0x2A4   steer right / left (0..1; the steering is right - left)
 *   this+0x2A8 / +0x2AC   gas / brake (0..1; braking at a standstill reverses, the game's DoAutoReverse)
 *   this+0x2B0            look back
 *   controls+0x1C, +0x21  handbrake (float), nitrous (bool); controls = the InputControls of the IInput part
 *                         (this-0x2C, virtual function 2)
 *
 * and shifts with DoShifting (0x69CE20, on this-0x74) when a shift button is pressed. Which car is player 2's: the one
 * whose player (owner at this-0x40, virtual GetPlayer +0x20, then GetControllerPort +0x34) does not have player 1's port.
 *
 * Controller: RT is the gas, LT the brake, the left stick steers, and in [player2] of the ini: handbrake=B nitrous=A
 * look_back=Y shift_up=RB shift_down=LB (A B X Y LB RB LS RS Back, or none).
 * Keyboard (when player 2 is the keyboard player; the game itself does not see the keyboard then): the keys of
 * [player2_keys], by default the arrow keys, Space for the handbrake, Left Alt nitrous, L look back, Left Shift / Left
 * Ctrl shift up / down. Names: A-Z, 0-9, Up Down Left Right, Space, Enter, Tab, LShift RShift, LCtrl RCtrl, LAlt RAlt,
 * or the DirectInput key code as a number.
 */
#include "player2_input.h"

#include <cmath>

#include "hooks.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::player2_input {

namespace xinput = nfsmw::xinput;

namespace {

constexpr uintptr_t kFeDatabase = 0x91CF90;
constexpr uintptr_t kDoShifting = 0x69CE20;
constexpr float kStickDeadzone = 0.15f;

struct Buttons {
    WORD handbrake = xinput::kButtonB, nitrous = xinput::kButtonA, look_back = xinput::kButtonY;
    WORD shift_up = xinput::kRightShoulder, shift_down = xinput::kLeftShoulder;
} g_buttons;

struct Keys {  // DirectInput key codes
    int steer_left = 0xCB, steer_right = 0xCD, gas = 0xC8, brake = 0xD0;
    int handbrake = 0x39, nitrous = 0x38, look_back = 0x26, shift_up = 0x2A, shift_down = 0x1D;
} g_keys;

// What the player is doing, from either device.
struct Controls {
    float steer = 0.0f;  // -1 left .. 1 right
    float gas = 0.0f, brake = 0.0f;
    bool handbrake = false, nitrous = false, look_back = false;
    bool shift_up = false, shift_down = false;  // held; a shift happens when it goes down
};

WORD button_from_ini(const char *key, WORD def) {
    char text[16];
    GetPrivateProfileStringA("player2", key, "", text, sizeof(text), g_ini);
    if (!*text) return def;
    static const struct {
        const char *name;
        WORD bit;
    } names[] = {{"A", xinput::kButtonA},       {"B", xinput::kButtonB},           {"X", xinput::kButtonX},
                 {"Y", xinput::kButtonY},       {"LB", xinput::kLeftShoulder},     {"RB", xinput::kRightShoulder},
                 {"LS", xinput::kLeftThumb},    {"RS", xinput::kRightThumb},       {"Back", xinput::kBack},
                 {"none", 0}};
    for (const auto &n : names)
        if (_stricmp(text, n.name) == 0) return n.bit;
    g_log.line("[player2] %s=%s: not a button name (A B X Y LB RB LS RS Back none); using the default", key, text);
    return def;
}

// DirectInput key codes of the keys by name.
int key_code(const char *name, int def, const char *key) {
    static const struct {
        const char *name;
        int code;
    } names[] = {{"Up", 0xC8},     {"Down", 0xD0},   {"Left", 0xCB},  {"Right", 0xCD}, {"Space", 0x39}, {"Enter", 0x1C},
                 {"Tab", 0x0F},    {"LShift", 0x2A}, {"RShift", 0x36}, {"LCtrl", 0x1D}, {"RCtrl", 0x9D}, {"LAlt", 0x38},
                 {"RAlt", 0xB8},   {"Backspace", 0x0E}, {"A", 0x1E}, {"B", 0x30}, {"C", 0x2E}, {"D", 0x20}, {"E", 0x12},
                 {"F", 0x21},      {"G", 0x22},      {"H", 0x23},     {"I", 0x17},     {"J", 0x24},    {"K", 0x25},
                 {"L", 0x26},      {"M", 0x32},      {"N", 0x31},     {"O", 0x18},     {"P", 0x19},    {"Q", 0x10},
                 {"R", 0x13},      {"S", 0x1F},      {"T", 0x14},     {"U", 0x16},     {"V", 0x2F},    {"W", 0x11},
                 {"X", 0x2D},      {"Y", 0x15},      {"Z", 0x2C},     {"1", 0x02},     {"2", 0x03},    {"3", 0x04},
                 {"4", 0x05},      {"5", 0x06},      {"6", 0x07},     {"7", 0x08},     {"8", 0x09},    {"9", 0x0A},
                 {"0", 0x0B}};
    if (!*name) return def;
    for (const auto &n : names)
        if (_stricmp(name, n.name) == 0) return n.code;
    char *end = nullptr;
    const long number = strtol(name, &end, 0);
    if (*end == 0 && number > 0 && number < 256) return static_cast<int>(number);
    g_log.line("[player2_keys] %s=%s: not a key name; using the default", key, name);
    return def;
}

int key_from_ini(const char *key, int def) {
    char text[16];
    GetPrivateProfileStringA("player2_keys", key, "", text, sizeof(text), g_ini);
    return key_code(text, def, key);
}

template <typename T>
T &at(void *base, int offset) {
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

template <typename R>
R call_virtual(void *object, int offset) {
    using Fn = R(__fastcall *)(void *, void *);
    return reinterpret_cast<Fn>((*static_cast<void ***>(object))[offset / 4])(object, nullptr);
}

bool is_player2(void *self) {
    void *owner = at<void *>(self, -0x40);
    if (!owner) return false;
    void *player = call_virtual<void *>(owner, 0x20);
    if (!player) return false;
    const int port = call_virtual<int>(player, 0x34);
    const unsigned char *db = *reinterpret_cast<unsigned char **>(kFeDatabase);
    return db && port != static_cast<signed char>(db[0x0C]);
}

float stick(SHORT value) {
    float v = value / 32767.0f;
    if (v < -1.0f) v = -1.0f;
    const float magnitude = std::fabs(v);
    if (magnitude <= kStickDeadzone) return 0.0f;
    return std::copysign((magnitude - kStickDeadzone) / (1.0f - kStickDeadzone), v);
}

bool held(WORD buttons, WORD button) { return button && (buttons & button); }

Controls controls_from_pad(const xinput::Gamepad &p) {
    Controls c;
    c.steer = stick(p.lx);
    c.gas = p.right_trigger / 255.0f;
    c.brake = p.left_trigger / 255.0f;
    c.handbrake = held(p.buttons, g_buttons.handbrake);
    c.nitrous = held(p.buttons, g_buttons.nitrous);
    c.look_back = held(p.buttons, g_buttons.look_back);
    c.shift_up = held(p.buttons, g_buttons.shift_up);
    c.shift_down = held(p.buttons, g_buttons.shift_down);
    return c;
}

Controls controls_from_keys(const unsigned char *keys) {
    const auto down = [keys](int code) { return (keys[code] & 0x80) != 0; };
    Controls c;
    c.steer = (down(g_keys.steer_right) ? 1.0f : 0.0f) - (down(g_keys.steer_left) ? 1.0f : 0.0f);
    c.gas = down(g_keys.gas) ? 1.0f : 0.0f;
    c.brake = down(g_keys.brake) ? 1.0f : 0.0f;
    c.handbrake = down(g_keys.handbrake);
    c.nitrous = down(g_keys.nitrous);
    c.look_back = down(g_keys.look_back);
    c.shift_up = down(g_keys.shift_up);
    c.shift_down = down(g_keys.shift_down);
    return c;
}

// Player 2's device, as it is now: a controller that is not there, or a keyboard the game cannot show, is idle.
Controls read_controls() {
    xinput::Gamepad p;
    unsigned char keys[256];
    if (players::device(1) == players::kKeyboard) return players::read_keyboard(keys) ? controls_from_keys(keys) : Controls();
    return players::read_pad(1, &p) ? controls_from_pad(p) : Controls();
}

// Before the game's FetchInput: what player 2's device says. Returns the shift (1 up, -1 down, 0 none) just asked for.
int apply(void *self) {
    static bool shift_up_was = false, shift_down_was = false;
    const Controls c = read_controls();
    at<float>(self, 0x2A0) = c.steer > 0.0f ? c.steer : 0.0f;
    at<float>(self, 0x2A4) = c.steer < 0.0f ? -c.steer : 0.0f;
    at<float>(self, 0x2A8) = c.gas;
    at<float>(self, 0x2AC) = c.brake;
    at<float>(self, 0x2B0) = c.look_back ? 1.0f : 0.0f;
    void *input = static_cast<char *>(self) - 0x2C;
    char *controls = call_virtual<char *>(input, 0x08);
    if (controls) {
        at<float>(controls, 0x1C) = c.handbrake ? 1.0f : 0.0f;
        at<bool>(controls, 0x21) = c.nitrous;
    }
    const int shift = (c.shift_up && !shift_up_was) ? 1 : (c.shift_down && !shift_down_was) ? -1 : 0;
    shift_up_was = c.shift_up;
    shift_down_was = c.shift_down;
    return shift;
}

using FetchInputFn = void(__fastcall *)(void *self, void *);
FetchInputFn g_orig_fetch_input;
void __fastcall hook_fetch_input(void *self, void *edx) {
    if (!players::split_screen() || !is_player2(self)) return g_orig_fetch_input(self, edx);
    static bool logged = false;
    if (!logged) {
        logged = true;
        g_log.line("player 2's car found (input %p), driven from %s", self,
                   players::device(1) == players::kKeyboard ? "the keyboard" : "its controller");
    }
    const int shift = apply(self);
    g_orig_fetch_input(self, edx);
    if (shift) {
        using DoShiftingFn = bool(__fastcall *)(void *, void *, int);
        reinterpret_cast<DoShiftingFn>(kDoShifting)(static_cast<char *>(self) - 0x74, nullptr, shift);
    }
}

}  // namespace

void install() {
    g_buttons.handbrake = button_from_ini("handbrake", g_buttons.handbrake);
    g_buttons.nitrous = button_from_ini("nitrous", g_buttons.nitrous);
    g_buttons.look_back = button_from_ini("look_back", g_buttons.look_back);
    g_buttons.shift_up = button_from_ini("shift_up", g_buttons.shift_up);
    g_buttons.shift_down = button_from_ini("shift_down", g_buttons.shift_down);
    g_keys.steer_left = key_from_ini("steer_left", g_keys.steer_left);
    g_keys.steer_right = key_from_ini("steer_right", g_keys.steer_right);
    g_keys.gas = key_from_ini("gas", g_keys.gas);
    g_keys.brake = key_from_ini("brake", g_keys.brake);
    g_keys.handbrake = key_from_ini("handbrake", g_keys.handbrake);
    g_keys.nitrous = key_from_ini("nitrous", g_keys.nitrous);
    g_keys.look_back = key_from_ini("look_back", g_keys.look_back);
    g_keys.shift_up = key_from_ini("shift_up", g_keys.shift_up);
    g_keys.shift_down = key_from_ini("shift_down", g_keys.shift_down);
    const nfsmw::Hook hooks[] = {
        {"fetchinput", "InputPlayer::FetchInput", 0x6A8870, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A}, false,
         nfsmw::detour(&hook_fetch_input), nfsmw::orig(&g_orig_fetch_input)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, g_ini, "player 2 input");
}

}  // namespace split_screen_support::player2_input
