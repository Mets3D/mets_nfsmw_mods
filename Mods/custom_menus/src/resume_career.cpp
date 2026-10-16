// The main menu's Career entry opens the Career main menu (Resume Career / Start New Career / Load Career), which the
// Alias Manager makes redundant. When there is a career to resume, the entry is relabelled "Resume Career" and does what
// the Career main menu's Resume Career does (CResumeCareer::React), and Back from the safe house (uiCareerCrib) goes
// to the main menu instead of the Career main menu. Without a career to resume the entry is the game's own.
#include "resume_career.h"

#include <cstring>

#include "hooks.h"

#define THISCALL __attribute__((thiscall))

namespace custom_menus::resume_career {

namespace {

nfsmw::Logger *g_log = nullptr;

constexpr unsigned kCareerIcon = 0x03704F3D;                           // MainCareer's icon texture
constexpr unsigned kCareerText = 0x5815A2B5, kResumeText = 0xE072DB21;  // "Career", "Resume Career"
constexpr unsigned kButtonPressed = 0x0C407210, kExitComplete = 0xE1FDE1D1;
constexpr unsigned kOptionIcon = 0xC, kOptionName = 0x2C;  // IconOption

// cFrontendDatabase (*0x91CF90): the user profile at +0x10, whose CareerSettings (+0xA8) keep their flags at +4.
constexpr uintptr_t kFEDatabase = 0x91CF90;
constexpr unsigned kProfile = 0x10, kCareerFlags = 0xA8 + 4;
constexpr unsigned kCareerStarted = 1u << 0, kGameOver = 1u << 11;

bool can_resume() {
    void *db = *reinterpret_cast<void **>(kFEDatabase);
    void *profile = db ? nfsmw::field<void *>(db, kProfile) : nullptr;
    if (!profile) return false;
    const unsigned flags = nfsmw::field<unsigned>(profile, kCareerFlags);
    return (flags & kCareerStarted) && !(flags & kGameOver);
}

void *g_menu = nullptr;    // the main menu
void *g_option = nullptr;  // its Career entry
bool g_swallow = false;    // the next switch to the Career main menu is not wanted

const auto RefreshHeader = reinterpret_cast<void(THISCALL *)(void *menu)>(0x573870);  // IconScrollerMenu::RefreshHeader
using ReactFn = void(THISCALL *)(void *self, const char *pkg, unsigned data, void *obj, unsigned p1, unsigned p2);
const auto ResumeCareerReact = reinterpret_cast<ReactFn>(0x532650);  // CResumeCareer::React, which does not use `self`

// UIMain::UpdateProfileData runs at the end of the screen's setup and again when the profile has been loaded (the game
// loads the last one after the main menu appears): the label follows what can be resumed.
using MethodFn = void(FASTCALL *)(void *, void *);
MethodFn g_orig_update_profile = nullptr;
void FASTCALL hook_update_profile(void *menu, void *edx) {
    g_orig_update_profile(menu, edx);
    if (menu != g_menu || !g_option) return;
    const unsigned text = can_resume() ? kResumeText : kCareerText;
    if (nfsmw::field<unsigned>(g_option, kOptionName) == text) return;
    nfsmw::field<unsigned>(g_option, kOptionName) = text;
    RefreshHeader(menu);
}

// The safe house's Back: it makes the Career main menu (MainMenu_Sub.fng in career manager mode) again; the main menu instead.
// Only the screen's own exit does that, so the redirect is on while that message is handled.
using NotifyFn = void(FASTCALL *)(void *, void *, unsigned, void *, unsigned, unsigned);
NotifyFn g_orig_crib_notify = nullptr;
bool g_crib_exit = false;
void FASTCALL hook_crib_notify(void *self, void *edx, unsigned msg, void *obj, unsigned p1, unsigned p2) {
    g_crib_exit = msg == kExitComplete;
    g_orig_crib_notify(self, edx, msg, obj, p1, p2);
    g_crib_exit = false;
}

// MainCareer::React
using ReactHookFn = void(FASTCALL *)(void *, void *, const char *, unsigned, void *, unsigned, unsigned);
ReactHookFn g_orig_react = nullptr;
void FASTCALL hook_react(void *self, void *edx, const char *pkg, unsigned data, void *obj, unsigned p1, unsigned p2) {
    if (data == kButtonPressed && self == g_option && can_resume()) {
        g_log->line("main menu: resuming the career");
        ResumeCareerReact(self, pkg, data, obj, p1, p2);
        g_swallow = true;  // the screen's exit would take the (now career) game mode to the Career main menu
        return;
    }
    g_orig_react(self, edx, pkg, data, obj, p1, p2);
}

}  // namespace

void option_added(void *menu, void *option) {
    if (!g_log || nfsmw::field<unsigned>(option, kOptionIcon) != kCareerIcon) return;
    g_menu = menu;
    g_option = option;
}

bool swallow_switch(const char *pkg) {
    if (!g_swallow) return false;
    g_swallow = false;
    return pkg && strcmp(pkg, "MainMenu_Sub.fng") == 0;
}

const char *redirect_switch(const char *pkg) {
    if (!g_crib_exit || !pkg || strcmp(pkg, "MainMenu_Sub.fng") != 0) return pkg;
    g_log->line("safe house: Back goes to the main menu");
    return "MainMenu.fng";
}

void install_hooks(nfsmw::Logger &log, const char *ini) {
    if (!GetPrivateProfileIntA("features", "skip_career_menu", 1, ini)) return;
    g_log = &log;
    const nfsmw::Hook hooks[] = {
        {"updateprofile", "UIMain::UpdateProfileData", 0x528BC0, {0x83, 0xEC, 0x08, 0x53, 0x56, 0x8B, 0xF1}, false,
         nfsmw::detour(&hook_update_profile), nfsmw::orig(&g_orig_update_profile)},
        {"careercrib", "uiCareerCrib::NotificationMessage", 0x531EA0, {0x8B, 0x44, 0x24, 0x10, 0x8B, 0x54, 0x24}, false,
         nfsmw::detour(&hook_crib_notify), nfsmw::orig(&g_orig_crib_notify)},
        {"maincareer", "MainCareer::React", 0x51AC80, {0x81, 0x7C, 0x24, 0x08, 0x10, 0x72, 0x40}, false,
         nfsmw::detour(&hook_react), nfsmw::orig(&g_orig_react)},
    };
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), log, ini, "resume career");
}

}  // namespace custom_menus::resume_career
