/*
 * The Quick Race menu (UIQRMainMenu::Setup, 0x7AA730) has three entries in the console versions: Quick Play, Custom Race and
 * Split Screen. The PC build of Setup adds only the first two, but the rest is still in the exe: the icon (0xF365B5F5) and
 * the name (0x841D518A, "Split Screen") exist in the game's data, and when the menu closes after QRMode 2 was chosen
 * (UIQRMainMenu::NotificationMessage, 0x7A2E20) it sets the player count to 2 and goes on to the mode select screen.
 *
 * The missing piece is the entry. Setup adds its entries with IconScrollerMenu::AddOption (0x573960); right after the
 * second call (Custom Race) this adds the third, built like the game builds the others: an IconOption (0x4C bytes, constructor
 * 0x586FA0) whose virtual table is Custom Race's with the "pressed" function replaced by one that sets QRMode to 2.
 *
 * It is the second call's own instruction that is redirected, not AddOption: nfsmw_custom_menus hooks AddOption too, and two
 * plugins hooking one function at the same time can overwrite each other's hook, which left this entry out.
 */
#include "menu_entry.h"

#include "patch.h"
#include "split_screen_support.h"

namespace split_screen_support::menu_entry {

namespace {

constexpr uintptr_t kNew = 0x652AD0;                    // void *__cdecl operator new(size_t)
constexpr uintptr_t kIconOptionConstructor = 0x586FA0;  // thiscall (texture hash, name hash, description hash)
constexpr uintptr_t kCustomRaceVtable = 0x8B7590;       // { scalar deleting destructor, React }
constexpr uintptr_t kQrMode = 0x9B9E68;                 // 0 Quick Play, 1 Custom Race, 2 Split Screen
constexpr uintptr_t kSecondAddOption = 0x7AA81C;        // the call of AddOption in UIQRMainMenu::Setup that adds Custom Race
constexpr uintptr_t kAddOption = 0x573960;
constexpr size_t kIconOptionSize = 0x4C;
constexpr unsigned kTexture = 0xF365B5F5, kName = 0x841D518A;
constexpr unsigned kButtonPressed = 0x0C407210;

using NewFn = void *(__cdecl *)(size_t);
using ConstructorFn = void *(__fastcall *)(void *self, void *unused, unsigned texture, unsigned name, unsigned description);
using AddOptionFn = void(__fastcall *)(void *menu, void *unused, void *option);

void *g_vtable[2];  // { destructor, React } of our option

// IconOption::React(const char *package, uint32 message, FEObject *object, uint32 param1, uint32 param2)
void __fastcall react(void *, void *, const char *, unsigned message, void *, unsigned, unsigned) {
    if (message != kButtonPressed) return;
    *reinterpret_cast<int *>(kQrMode) = 2;
    g_log.line("Split Screen chosen in the Quick Race menu");
}

void __fastcall add_option_and_ours(void *menu, void *unused, void *option) {
    const AddOptionFn add_option = reinterpret_cast<AddOptionFn>(kAddOption);
    add_option(menu, unused, option);

    void *entry = reinterpret_cast<NewFn>(kNew)(kIconOptionSize);
    if (!entry) return;
    reinterpret_cast<ConstructorFn>(kIconOptionConstructor)(entry, nullptr, kTexture, kName, 0);
    *static_cast<void **>(entry) = g_vtable;
    add_option(menu, unused, entry);
    g_log.line("Quick Race menu: Split Screen entry added");
}

}  // namespace

void install() {
    // Another mod that changed this call (the NFSMWExtraOptions split screen fix adds the entry itself) leaves it as it is.
    const auto call_to = [](uintptr_t site, uintptr_t target, unsigned char *out) {
        out[0] = 0xE8;
        const intptr_t rel = static_cast<intptr_t>(target) - static_cast<intptr_t>(site + 5);
        memcpy(out + 1, &rel, 4);
    };
    unsigned char original[5], patched[5];
    call_to(kSecondAddOption, kAddOption, original);
    call_to(kSecondAddOption, reinterpret_cast<uintptr_t>(&add_option_and_ours), patched);
    g_vtable[0] = *reinterpret_cast<void **>(kCustomRaceVtable);
    g_vtable[1] = reinterpret_cast<void *>(&react);
    if (!nfsmw::patch_bytes(kSecondAddOption, original, patched, sizeof(original))) {
        g_log.line("the Quick Race menu setup (%#x) was changed by another mod; not adding the Split Screen entry",
                   static_cast<unsigned>(kSecondAddOption));
        return;
    }
    g_log.line("Quick Race menu: the Split Screen entry will be added");
}

}  // namespace split_screen_support::menu_entry
