#include "previous_camera.h"

#include "binding_table.h"
#include "controller_api.h"
#include "hooks.h"
#include "patch.h"

namespace camera_settings::previous_camera {

namespace bt = nfsmw::binding_table;
namespace opt = nfsmw::custom_menus;
using nfsmw::field;

namespace {

// An entry of the game's binding table that nothing refers to (SC_NUMPAD5), taken over by renaming it: the Controls screen
// shows an entry under the language string keyed bStringHash(name). Its bindings are kept in our ini ([previous_camera]), since
// the profile saves only the game's entries 0-18.
constexpr int kEntry = bt::kPreviousCameraEntry;
constexpr const char *kOldName = "SC_NUMPAD5", *kName = "CAM_ACTION_PREVIOUS";
constexpr const char *kText = "Previous Camera View";
// The game's own row becomes "Next Camera View" now that there is a previous one; ours goes right under it.
constexpr const char *kChangeName = "CAM_ACTION_CHANGE", *kNextText = "Next Camera View";
constexpr int kChangeCameraRow = 15;  // its row in the game's Controls lists (= its entry)

nfsmw::Logger *g_log = nullptr;
const char *g_ini = nullptr;
bool g_ok = false;          // the default table's entry was ours to take
bool g_live_ready = false;  // the live table's entry is renamed and has its saved bindings
bt::Slot g_saved[4];        // what the ini holds

bool named(uintptr_t table, const char *name) {
    const char *now = bt::name_of(table, kEntry);
    return now && (strcmp(now, kOldName) == 0 || strcmp(now, name) == 0);
}

// The live table (built by the game before we can act on it): renamed, with the bindings saved in the ini, or none.
void ensure_live() {
    if (!g_ok || g_live_ready) return;
    if (!named(bt::kLive, kName)) {
        g_log->line("previous camera: live binding entry %d is \"%s\", not %s; left alone", kEntry,
                    bt::name_of(bt::kLive, kEntry) ? bt::name_of(bt::kLive, kEntry) : "?", kOldName);
        g_ok = false;
        return;
    }
    bt::set_name(bt::kLive, kEntry, kName);
    char text[128] = {};
    GetPrivateProfileStringA("previous_camera", "binding", "", text, sizeof(text), g_ini);
    const bool saved = text[0] && bt::parse_slots(text, g_saved);
    if (!saved) memcpy(g_saved, bt::slots_of(bt::kDefault, kEntry), sizeof(g_saved));
    bt::set_slots(bt::kLive, kEntry, g_saved, 0, 4);
    g_live_ready = true;
    g_log->line("previous camera: binding from %s", saved ? "the ini" : "the defaults");
}

nfsmw::controller_api::LivePadFn g_live_pad = nullptr;

// The Controls screen refreshed (it opened, or a binding changed): save our entry's bindings if they differ. While the screen
// shows another controller's bindings (mets_controller_support keeps one set per controller) they are not ours to save.
void on_controls_refresh() {
    ensure_live();
    if (!g_live_ready) return;
    if (!g_live_pad) g_live_pad = nfsmw::controller_api::find_live_pad();
    if (g_live_pad && g_live_pad() != 0) return;
    const bt::Slot *now = bt::slots_of(bt::kLive, kEntry);
    if (memcmp(now, g_saved, sizeof(g_saved)) == 0) return;
    memcpy(g_saved, now, sizeof(g_saved));
    char text[128];
    bt::format_slots(g_saved, text, sizeof(text));
    WritePrivateProfileStringA("previous_camera", "binding", text, g_ini);
    g_log->line("previous camera: binding saved");
}

// ---- the action ----
// The game turns the camera change action into PlayerSettings::ScrollDriveCam(settings, 1) inside InputPlayer::FetchInput
// (0x6A8AEE), which only runs for a human player in a race, so a press in a menu (or while rebinding) changes nothing. We do
// the same with -1 on a press (the rising edge) of the entry's value on the player's device. The player and their settings
// come the way FetchInput gets them: the owner at this-0x40, whose vtable +0x20 is the player, whose +0x14 is the settings and
// +0x34 the controller port. The port is the index of the player's device in the game's input module.
using FetchFn = void(FASTCALL *)(void *, void *);
FetchFn g_orig_fetch;
using ScrollFn = void(FASTCALL *)(void *, void *, int);
const auto ScrollDriveCam = reinterpret_cast<ScrollFn>(0x5A3850);  // PlayerSettings::ScrollDriveCam(this, dir)

constexpr uintptr_t kIOModule = 0x920DE0;     // +4 device count, +8 device pointers
constexpr uintptr_t kGameDeviceVtable = 0x8A7B9C;
constexpr int kMaxPorts = 16;

template <typename R>
R vcall(void *obj, unsigned offset) {
    return reinterpret_cast<R(FASTCALL *)(void *, void *)>((*reinterpret_cast<void ***>(obj))[offset / 4])(obj, nullptr);
}

bool g_was_down[kMaxPorts], g_was_next[kMaxPorts];

// What the device at `port` says for an entry now (0..1); 0 for anything that is not a GameDevice.
float entry_value(int port, int entry) {
    const int count = field<int>(reinterpret_cast<void *>(kIOModule), 4);
    if (port < 0 || port >= count || port >= kMaxPorts) return 0.0f;
    void *device = field<void *>(reinterpret_cast<void *>(kIOModule), 8 + port * 4);
    if (!device || field<uintptr_t>(device, 0) != kGameDeviceVtable) return 0.0f;
    const float *current = field<float *>(device, 0x20);
    return current ? current[entry] : 0.0f;
}

// The game's scroll asks IsPlayerCameraSelectable (0x599A50) in both directions and goes on to the next camera when it says no,
// so only the four console cameras can be reached. NFSMWExtraOptions' EnableHiddenCameraModes removes the check going forward
// (0x5A38AA, je -> nop) but not backward (0x5A390D), where Previous Camera View would still skip the others. When the forward
// check is gone the backward one goes too (looked at on the first press: the other mod may load later than we do).
void match_forward_scroll() {
    static bool done = false;
    if (done) return;
    done = true;
    const unsigned char forward[2] = {0x90, 0x90}, backward_check[2] = {0x74, 0xB1}, nop[2] = {0x90, 0x90};
    if (memcmp(reinterpret_cast<void *>(0x5A38AA), forward, 2) != 0) return;
    g_log->line("previous camera: %s", nfsmw::patch_bytes(0x5A390D, backward_check, nop, 2)
                                           ? "another mod lets Next reach every camera view, so Previous does too"
                                           : "another mod lets Next reach every camera view, but the check going backward was changed: left alone");
}

void FASTCALL hook_fetch(void *self, void *edx) {
    void *owner = *reinterpret_cast<void **>(static_cast<char *>(self) - 0x40);
    void *player = owner ? vcall<void *>(owner, 0x20) : nullptr;
    void *settings = player ? vcall<void *>(player, 0x14) : nullptr;
    const int before = settings ? field<int>(settings, 0xC) : -1;
    g_orig_fetch(self, edx);
    ensure_live();
    if (!settings || !g_live_ready) return;
    const int port = vcall<int>(player, 0x34);
    if (port < 0 || port >= kMaxPorts) return;
    match_forward_scroll();
    // The game's own scroll (Next Camera View) ran inside the call above: log what a press of it did.
    const bool next = entry_value(port, kChangeCameraRow) > 0.5f;
    const int after = field<int>(settings, 0xC);
    if (next && !g_was_next[port]) g_log->line("camera view: Next Camera View pressed, view %d -> %d", before, after);
    g_was_next[port] = next;
    const bool down = entry_value(port, kEntry) > 0.5f;
    if (down && !g_was_down[port]) {
        ScrollDriveCam(settings, nullptr, -1);
        g_log->line("camera view: Previous Camera View pressed, view %d -> %d", after, field<int>(settings, 0xC));
    }
    g_was_down[port] = down;
}

}  // namespace

void init(nfsmw::Logger &log, const char *ini) {
    g_log = &log;
    g_ini = ini;
    if (!named(bt::kDefault, kName)) {
        log.line("previous camera: default binding entry %d is \"%s\", not %s; no Previous Camera View", kEntry,
                 bt::name_of(bt::kDefault, kEntry) ? bt::name_of(bt::kDefault, kEntry) : "?", kOldName);
        return;
    }
    // The default table is what Defaults and the start-up copy: renamed, and no keyboard key (the entry had a numpad key). The
    // controller slots are left to mets_controller_support's default layout, if it is installed (nothing otherwise).
    const bt::Slot none[4] = {};
    bt::set_name(bt::kDefault, kEntry, kName);
    bt::set_slots(bt::kDefault, kEntry, none, 0, 2);
    g_ok = true;
}

void provide(const opt::Host *host) {
    if (!g_ok) return;
    ensure_live();
    if (!g_live_ready) return;
    static const opt::ControlsRow row = {kEntry, kChangeCameraRow};
    host->register_text(nfsmw::bstring_hash(kName), kText);
    host->register_text(nfsmw::bstring_hash(kChangeName), kNextText);
    host->register_controls_rows(&row, 1);
    host->add_controls_refresh(&on_controls_refresh);
}

void install_hooks(nfsmw::Logger &log, const char *ini) {
    if (!g_ok) return;
    const nfsmw::Hook hooks[] = {
        {"fetchinput", "InputPlayer::FetchInput", 0x6A8870, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A}, false,
         nfsmw::detour(&hook_fetch), nfsmw::orig(&g_orig_fetch)},
    };
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), log, ini, "previous camera");
}

}  // namespace camera_settings::previous_camera
