/*
 * Each human-driven car has an InputPlayer, whose FetchInput (0x6A8870, called with its IInputPlayer part as `this`) runs
 * every simulation step: it points its action queue at the player's joystick port, takes the actions waiting there
 * and turns what it has into the car's controls. Player 2's port is -1 in a split screen race (players.cpp), so there
 * are no actions; this fills in what the actions would have, from player 2's controller, before the game's code runs:
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
 * Buttons, in [player2] of the ini (A B X Y LB RB LS RS Back, or none): handbrake=B nitrous=A look_back=Y
 * shift_up=RB shift_down=LB. RT is the gas, LT the brake, the left stick steers.
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
WORD g_previous_buttons = 0;

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

// Before the game's FetchInput: what player 2's controller says. Returns the buttons just pressed.
WORD apply(void *self) {
    xinput::Gamepad p = {};
    const bool connected = players::read_pad(1, &p);
    const float steer = connected ? stick(p.lx) : 0.0f;
    at<float>(self, 0x2A0) = steer > 0.0f ? steer : 0.0f;
    at<float>(self, 0x2A4) = steer < 0.0f ? -steer : 0.0f;
    at<float>(self, 0x2A8) = p.right_trigger / 255.0f;
    at<float>(self, 0x2AC) = p.left_trigger / 255.0f;
    at<float>(self, 0x2B0) = (p.buttons & g_buttons.look_back) && g_buttons.look_back ? 1.0f : 0.0f;
    void *input = static_cast<char *>(self) - 0x2C;
    char *controls = call_virtual<char *>(input, 0x08);
    if (controls) {
        at<float>(controls, 0x1C) = (p.buttons & g_buttons.handbrake) && g_buttons.handbrake ? 1.0f : 0.0f;
        at<bool>(controls, 0x21) = (p.buttons & g_buttons.nitrous) && g_buttons.nitrous;
    }
    const WORD pressed = p.buttons & ~g_previous_buttons;
    g_previous_buttons = p.buttons;
    return pressed;
}

using FetchInputFn = void(__fastcall *)(void *self, void *);
FetchInputFn g_orig_fetch_input;
void __fastcall hook_fetch_input(void *self, void *edx) {
    if (!players::split_screen() || !is_player2(self)) return g_orig_fetch_input(self, edx);
    static bool logged = false;
    if (!logged) {
        logged = true;
        g_log.line("player 2's car found (input %p), driven from %s", self,
                   players::pad(1) >= 0 ? "its controller" : "nothing: player 2 has no controller");
    }
    const WORD pressed = apply(self);
    g_orig_fetch_input(self, edx);
    const int shift = (pressed & g_buttons.shift_up) && g_buttons.shift_up ? 1 : (pressed & g_buttons.shift_down) && g_buttons.shift_down ? -1 : 0;
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
    const nfsmw::Hook hooks[] = {
        {"fetchinput", "InputPlayer::FetchInput", 0x6A8870, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A}, false,
         nfsmw::detour(&hook_fetch_input), nfsmw::orig(&g_orig_fetch_input)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, g_ini, "player 2 input");
}

}  // namespace split_screen_support::player2_input
