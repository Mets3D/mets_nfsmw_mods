/*
 * The Quick Race menu (UIQRMainMenu::Setup, 0x7AA730) has three entries in the console versions: Quick Play, Custom Race and
 * Split Screen. The PC build of Setup adds only the first two, but the rest is still in the exe: the icon (0xF365B5F5) and
 * the name (0x841D518A, "Split Screen") exist in the game's data, and when the menu closes after QRMode 2 was chosen
 * (UIQRMainMenu::NotificationMessage, 0x7A2E20) it sets the player count to 2 and goes on to the mode select screen.
 *
 * The missing piece is the entry. Setup adds its entries with IconScrollerMenu::AddOption (0x573960); right after the
 * second call (Custom Race) this adds the third, built like the game builds the others: an IconOption (0x4C bytes, constructor
 * 0x586FA0) whose virtual table is Custom Race's with the "pressed" function replaced by one that sets QRMode to 2.
 */
#include "menu_entry.h"

#include "hooks.h"
#include "split_screen_support.h"

namespace split_screen_support::menu_entry {

namespace {

constexpr uintptr_t kNew = 0x652AD0;                    // void *__cdecl operator new(size_t)
constexpr uintptr_t kIconOptionConstructor = 0x586FA0;  // thiscall (texture hash, name hash, description hash)
constexpr uintptr_t kCustomRaceVtable = 0x8B7590;       // { scalar deleting destructor, React }
constexpr uintptr_t kQrMode = 0x9B9E68;                 // 0 Quick Play, 1 Custom Race, 2 Split Screen
constexpr uintptr_t kAfterSecondAddOption = 0x7AA821;   // where UIQRMainMenu::Setup continues after adding Custom Race
constexpr size_t kIconOptionSize = 0x4C;
constexpr unsigned kTexture = 0xF365B5F5, kName = 0x841D518A;
constexpr unsigned kButtonPressed = 0x0C407210;

using NewFn = void *(__cdecl *)(size_t);
using ConstructorFn = void *(__fastcall *)(void *self, void *unused, unsigned texture, unsigned name, unsigned description);
using AddOptionFn = void(__fastcall *)(void *menu, void *unused, void *option);

AddOptionFn g_orig_add_option;
void *g_vtable[2];  // { destructor, React } of our option

// IconOption::React(const char *package, uint32 message, FEObject *object, uint32 param1, uint32 param2)
void __fastcall react(void *, void *, const char *, unsigned message, void *, unsigned, unsigned) {
    if (message != kButtonPressed) return;
    *reinterpret_cast<int *>(kQrMode) = 2;
    g_log.line("Split Screen chosen in the Quick Race menu");
}

void __fastcall hook_add_option(void *menu, void *unused, void *option) {
    const void *caller = __builtin_return_address(0);
    g_orig_add_option(menu, unused, option);
    if (caller != reinterpret_cast<void *>(kAfterSecondAddOption)) return;

    void *entry = reinterpret_cast<NewFn>(kNew)(kIconOptionSize);
    if (!entry) return;
    reinterpret_cast<ConstructorFn>(kIconOptionConstructor)(entry, nullptr, kTexture, kName, 0);
    *static_cast<void **>(entry) = g_vtable;
    g_orig_add_option(menu, unused, entry);
    g_log.line("Quick Race menu: Split Screen entry added");
}

}  // namespace

void install() {
    // Another mod that changed what follows the second AddOption call (the NFSMWExtraOptions split screen fix does) adds
    // the entry itself, and the call no longer returns where it is looked for here.
    static const unsigned char kOriginal[5] = {0x8B, 0x46, 0x10, 0x50, 0xE8};  // mov eax,[esi+0x10]; push eax; call ...
    if (memcmp(reinterpret_cast<void *>(kAfterSecondAddOption), kOriginal, sizeof(kOriginal)) != 0) {
        g_log.line("the Quick Race menu setup (%#x) was changed by another mod; not adding the Split Screen entry",
                   static_cast<unsigned>(kAfterSecondAddOption));
        return;
    }
    g_vtable[0] = *reinterpret_cast<void **>(kCustomRaceVtable);
    g_vtable[1] = reinterpret_cast<void *>(&react);
    const nfsmw::Hook hooks[] = {
        {"addoption", "IconScrollerMenu::AddOption", 0x573960, {0x8B, 0x41, 0x2C, 0x56, 0x8B, 0x74, 0x24}, true,
         nfsmw::detour(&hook_add_option), nfsmw::orig(&g_orig_add_option)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, g_ini, "Quick Race menu");
}

}  // namespace split_screen_support::menu_entry
