#include "keyboard2.h"

#include "bindings.h"
#include "hooks.h"

namespace pad::keyboard2 {
namespace {

using namespace bindings;

const nfsmw::controller_api::SecondKeyboard *g_keyboard = nullptr;
bool g_save_hooked = false;
bool g_editing = false;
Slot g_primary[kBindingCount][2];  // the keyboard's own slots, while the table holds the second keyboard's

Slot key_slot(int scan) { return scan > 0 ? Slot{kKey, scan, 0} : Slot{}; }

int key_of(const Slot &slot) {
    return slot.type == kKey && slot.device > 0 && slot.device < 256 ? slot.device : 0;
}

// The profile saves the table: the keyboard's own slots, also while it holds the second keyboard's.
using SaveFn = unsigned char *(__cdecl *)(unsigned char *, unsigned char *);
SaveFn g_orig_save;
unsigned char *__cdecl save_primary(unsigned char *buffer, unsigned char *end) {
    if (!g_editing) return g_orig_save(buffer, end);
    Slot editor[kBindingCount][2];
    for (int entry = 0; entry < kBindingCount; ++entry) {
        Slot *live = slots_of(kBindingTable, entry);
        for (int s = 0; s < 2; ++s) {
            editor[entry][s] = live[s];
            live[s] = g_primary[entry][s];
        }
    }
    auto *result = g_orig_save(buffer, end);
    for (int entry = 0; entry < kBindingCount; ++entry)
        memcpy(slots_of(kBindingTable, entry), editor[entry], sizeof(editor[entry]));
    return result;
}

}  // namespace

// Not without the save hook: the profile would save the second keyboard's keys as the keyboard's.
bool provide(const nfsmw::controller_api::SecondKeyboard *keyboard) {
    if (!keyboard || !g_save_hooked || g_editing) return false;
    g_keyboard = keyboard;
    log_line("Controls menu: a second keyboard (\"%s\"), kept by another mod", keyboard->name);
    return true;
}

bool enabled() { return g_keyboard != nullptr; }
const char *name() { return g_keyboard ? g_keyboard->name : ""; }
bool editing() { return g_editing; }

void begin_edit() {
    if (!g_keyboard) return;
    log_binding_changes(true);
    for (int entry = 0; entry < kBindingCount; ++entry)
        memcpy(g_primary[entry], slots_of(kBindingTable, entry), sizeof(g_primary[entry]));
    for (int entry = 0; entry < kBindingCount; ++entry) {
        int keys[2];
        if (!g_keyboard->keys(entry, false, keys)) continue;
        for (int s = 0; s < 2; ++s) slots_of(kBindingTable, entry)[s] = key_slot(keys[s]);
    }
    g_editing = true;
    resync_changes();
}

void end_edit() {
    if (!g_editing) return;
    for (int entry = 0; entry < kBindingCount; ++entry) {
        int keys[2];
        if (!g_keyboard->keys(entry, false, keys)) continue;
        const Slot *live = slots_of(kBindingTable, entry);
        const int now[2] = {key_of(live[0]), key_of(live[1])};
        if (now[0] != keys[0] || now[1] != keys[1]) g_keyboard->set_keys(entry, now);
    }
    for (int entry = 0; entry < kBindingCount; ++entry)
        memcpy(slots_of(kBindingTable, entry), g_primary[entry], sizeof(g_primary[entry]));
    g_editing = false;
    resync_changes();
}

void reset_editor() {
    for (int entry = 0; entry < kBindingCount; ++entry) {
        int keys[2];
        if (!g_keyboard->keys(entry, true, keys)) continue;
        for (int s = 0; s < 2; ++s) slots_of(kBindingTable, entry)[s] = key_slot(keys[s]);
    }
}

void install_hooks() {
    const nfsmw::Hook hooks[] = {
        {"keyboard2_profile_save", "UserProfile::BindingsIO_A", 0x56E180, {0x53, 0x55, 0x56, 0x57, 0xE8, 0xE7, 0x61}, true,
         nfsmw::detour(&save_primary), nfsmw::orig(&g_orig_save)},
    };
    g_save_hooked = nfsmw::install_hooks(hooks, 1, g_log, nullptr, "second keyboard").created == 1;
}

}  // namespace pad::keyboard2

// For other mods (common/controller_api.h).
extern "C" __declspec(dllexport) int mets_controller_support_second_keyboard(const nfsmw::controller_api::SecondKeyboard *keyboard) {
    return pad::keyboard2::provide(keyboard);
}
