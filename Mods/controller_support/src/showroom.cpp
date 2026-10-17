// The showroom is the garage screen (GarageMainScreen, agent_docs/menus-and-camera.md) with a SelectCarCameraMover. The mover's zoom is
// a speed (SetZoomSpeed 0x476FF0, -1..1, positive zooms out): the radius changes by it every update, so an analog value zooms
// as fast as it is pushed. The game feeds it from its action queue (HandleJoyEvents 0x7A2310, once per tick), a digital value; this hook runs
// after that and sets the speed from the controller instead: RT / LT by how far they are pulled, RB / LB like a trigger pulled
// fully down (the same as the world map, whose RT and RB zoom in).
#include "showroom.h"

#include "hooks.h"
#include "input.h"

namespace pad::showroom {

namespace {

using SetZoomSpeedFn = void(__attribute__((thiscall)) *)(void *, float);  // SelectCarCameraMover::SetZoomSpeed (this, speed), ret 4

MethodFn g_orig_joy_events = nullptr;

constexpr uintptr_t kSetZoomSpeed = 0x476FF0;
constexpr uintptr_t kCarGuysCamera = 0x926134;  // byte: the free camera, which is allowed everywhere
constexpr uintptr_t kTicksSinceMoved = 0x9B9E58, kTicksBeforeReturn = 0x905E24;  // ints: the camera returns to its view when this runs out
// GarageMainScreen members on the PC (the decomp's from +0x78 are 4 too high).
constexpr unsigned kMover = 0x4C, kUserRotate = 0x78, kZoom = 0x84;
constexpr BYTE kTriggerDeadzone = 30;
constexpr float kMaxSpeed = 0.5f;  // the speed with a trigger fully pulled (the keyboard's 1 is quick for a controller)

float trigger(BYTE v) { return v <= kTriggerDeadzone ? 0.0f : (v - kTriggerDeadzone) / (255.0f - kTriggerDeadzone); }

float g_ours = 0.0f;  // the speed we set last, so that letting go stops it (and only it)

void FASTCALL hook_joy_events(void *self, void *edx) {
    g_orig_joy_events(self, edx);

    void *const mover = field<void *>(self, kMover);
    if (!mover) return;
    xinput::Gamepad p = {};
    float out = 0.0f;
    if (input::read_merged(&p) && (field<bool>(self, kUserRotate) || *reinterpret_cast<const BYTE *>(kCarGuysCamera))) {
        const float in = trigger(p.right_trigger) + ((p.buttons & xinput::kRightShoulder) ? 1.0f : 0.0f);
        const float away = trigger(p.left_trigger) + ((p.buttons & xinput::kLeftShoulder) ? 1.0f : 0.0f);
        out = (std::fmin(1.0f, away) - std::fmin(1.0f, in)) * kMaxSpeed;
    }
    if (out == 0.0f && g_ours == 0.0f) return;
    if (out == 0.0f && field<float>(self, kZoom) != g_ours) {  // the game has set something else since (the keyboard)
        g_ours = 0.0f;
        return;
    }
    g_ours = out;
    reinterpret_cast<SetZoomSpeedFn>(kSetZoomSpeed)(mover, out);
    field<float>(self, kZoom) = out;
    if (out != 0.0f) *reinterpret_cast<int *>(kTicksSinceMoved) = *reinterpret_cast<const int *>(kTicksBeforeReturn);
}

}  // namespace

void install_hooks() {
    const nfsmw::Hook hooks[] = {
        {"showroom", "GarageMainScreen::HandleJoyEvents", 0x7A2310, {0xA1, 0x90, 0xCF, 0x91, 0x00, 0x83, 0xEC}, false,
         nfsmw::detour(&hook_joy_events), nfsmw::orig(&g_orig_joy_events)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, nullptr, "showroom");
}

}  // namespace pad::showroom
