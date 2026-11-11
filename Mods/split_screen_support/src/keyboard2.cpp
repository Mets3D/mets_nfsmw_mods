#include "keyboard2.h"

#include "binding_table.h"
#include "controller_api.h"
#include "events.h"
#include "keys.h"
#include "split_screen_support.h"

namespace split_screen_support::keyboard2 {
namespace {

namespace api = nfsmw::controller_api;

struct Binding {
    int entry;  // of the game's binding table
    const char *setting;
    const char *default_key;
    int defaults = 0;
    int keys[2] = {};
};

Binding g_bindings[] = {
    {0, "accelerate", "Up"}, {1, "brake", "Down"}, {2, "steer_left", "Left"}, {3, "steer_right", "Right"},
    {4, "handbrake", "NumpadDecimal"}, {5, "speedbreaker", "None"}, {6, "nitrous", "Numpad0"},
    {7, "shift_down", "NumpadMinus"}, {8, "shift_up", "NumpadPlus"}, {9, "reset_car", "Numpad7"},
    {10, "engage_event", "None"}, {11, "blacklist", "None"}, {12, "world_map", "None"}, {13, "sms", "None"},
    {15, "change_camera", "Numpad2"}, {16, "look_back", "Numpad3"}, {17, "pull_camera_back", "None"},
    {26, "look_left", "None"}, {28, "look_right", "None"}, {25, "look_up", "None"}, {27, "look_down", "None"},  // mets_controller_support's look entries
    {nfsmw::binding_table::kPreviousCameraEntry, "previous_camera", "Numpad1"},
};
constexpr int kPauseEntry = 18, kNumpadEnter = 0x9C;  // the pause key is not a row of the Controls screen
constexpr char kDefaults[] = "keyboard_2", kChanged[] = "keyboard_2_bindings";
bool g_enabled = false;

// The DirectInput code of a key's name, 0 for None, -1 for a name that is not known.
int scan_code(const char *name) {
    if (!_stricmp(name, "None")) return 0;
    // The keypad digits have their own physical scan codes, independent of Num Lock.
    if (!_strnicmp(name, "Numpad", 6) && name[6] >= '0' && name[6] <= '9' && !name[7]) {
        constexpr int scans[] = {0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49};
        return scans[name[6] - '0'];
    }
    // Navigation keys are extended DirectInput codes. Their numpad counterparts share the low byte.
    const struct { const char *name; int scan; } navigation[] = {
        {"Up", 0xC8}, {"Down", 0xD0}, {"Left", 0xCB}, {"Right", 0xCD},
        {"Home", 0xC7}, {"End", 0xCF}, {"PageUp", 0xC9}, {"PageDown", 0xD1},
        {"Insert", 0xD2}, {"Delete", 0xD3}, {"NumpadEnter", kNumpadEnter}, {"NumpadDecimal", 0x53},
        {"NumpadPlus", 0x4E}, {"NumpadMinus", 0x4A},
    };
    for (const auto &key : navigation)
        if (!_stricmp(name, key.name)) return key.scan;
    const struct { const char *name; int vk; } names[] = {
        {"Space", VK_SPACE},
        {"Tab", VK_TAB}, {"LeftShift", VK_LSHIFT}, {"RightShift", VK_RSHIFT}, {"LeftCtrl", VK_LCONTROL}, {"RightCtrl", VK_RCONTROL},
    };
    int vk = 0;
    for (const auto &key : names)
        if (!_stricmp(name, key.name)) vk = key.vk;
    if (!vk) vk = nfsmw::key_from_name(name);
    if (vk <= 0 || vk > 255) return -1;
    const UINT scan = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC_EX);
    if (!scan || scan > 0xE0FF || (scan > 0x7F && (scan >> 8) != 0xE0)) return -1;
    return (scan & 0x7F) | (scan > 0x7F ? 0x80 : 0);
}

void load() {
    for (auto &binding : g_bindings) {
        char text[32];
        GetPrivateProfileStringA(kDefaults, binding.setting, binding.default_key, text, sizeof(text), g_ini);
        binding.defaults = scan_code(text);
        if (binding.defaults < 0) {
            g_log.line("Keyboard 2: unknown key '%s' for %s, using %s", text, binding.setting, binding.default_key);
            binding.defaults = scan_code(binding.default_key);
        }
        binding.keys[0] = binding.defaults;
        binding.keys[1] = 0;
        int first, second;
        GetPrivateProfileStringA(kChanged, binding.setting, "", text, sizeof(text), g_ini);
        if (sscanf(text, "%d %d", &first, &second) == 2 && first >= 0 && first < 256 && second >= 0 && second < 256) {
            binding.keys[0] = first;
            binding.keys[1] = second;
        }
    }
}

Binding *binding_of(int entry) {
    for (auto &binding : g_bindings)
        if (binding.entry == entry) return &binding;
    return nullptr;
}

bool keys(int entry, bool defaults, int out[2]) {
    const Binding *binding = binding_of(entry);
    if (!binding) return false;
    out[0] = defaults ? binding->defaults : binding->keys[0];
    out[1] = defaults ? 0 : binding->keys[1];
    return true;
}

void set_keys(int entry, const int keys[2]) {
    Binding *binding = binding_of(entry);
    if (!binding) return;
    binding->keys[0] = keys[0];
    binding->keys[1] = keys[1];
    char text[32];
    snprintf(text, sizeof(text), "%d %d", keys[0], keys[1]);
    WritePrivateProfileStringA(kChanged, binding->setting, text, g_ini);
}

const api::SecondKeyboard kKeyboard = {kName, &keys, &set_keys};

// The Controls screen is mets_controller_support's: with it the keys can be changed there. On the first frame: it is loaded by then, whichever of the
// two the loader took first.
void provide() {
    static bool tried = false;
    if (tried) return;
    tried = true;
    const api::SecondKeyboardFn second_keyboard = api::find_second_keyboard();
    const bool shown = second_keyboard && second_keyboard(&kKeyboard);
    g_log.line("Keyboard 2: on; its keys are %s", shown ? "also on the Controls screen" : "the ini's (the Controls screen shows them with mets_controller_support)");
}

}  // namespace

void install() {
    load();
    g_enabled = true;
    events::on_frame(&provide);
}

bool enabled() { return g_enabled; }

void actions(const unsigned char *keyboard, float *values) {
    const auto down = [keyboard](int key) { return key > 0 && key < 256 && (keyboard[key] & 0x80) != 0; };
    for (const auto &binding : g_bindings) values[binding.entry] = down(binding.keys[0]) || down(binding.keys[1]) ? 1.0f : 0.0f;
    values[kPauseEntry] = down(kNumpadEnter) ? 1.0f : 0.0f;
}

bool uses_key(int key) {
    if (key <= 0) return false;
    if (key == kNumpadEnter) return true;
    for (const auto &binding : g_bindings)
        if (binding.keys[0] == key || binding.keys[1] == key) return true;
    return false;
}

}  // namespace split_screen_support::keyboard2
