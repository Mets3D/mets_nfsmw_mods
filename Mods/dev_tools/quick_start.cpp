/*
 * FEngMovieStarter::Callback 0x559680 checks the byte at 0x926144 before starting a movie: if set, it posts the same
 * "movie finished" message (0xc3960eb9) the real movie posts when it ends, instead of opening it - so whatever movie
 * it was (the EA logo, the PSA, the attract movie, or any later in-game one) is torn down the normal way, never shown.
 * NFSMWExtraOptions's SkipMovies and the widescreen fix's SkipIntro set the same byte for the same reason. This is a
 * race against the game reaching each movie's check: our plugin loads from a worker thread the ASI loader starts
 * sometime after it finds every .asi (about 2 seconds in, normally), and whichever movie that
 * happens to land before stays unskippable, independent of this tool. (Hooking MovieScreen's constructor instead let
 * the real movie start, since the constructor runs before Callback does, and the later skip crashed.)
 *
 * With the movies skipped, a movie screen gets "movie finished" more than once: the switch to the next screen is only
 * queued, so the old screen lives on for a frame or two and each late message runs MovieScreen::NotificationMessage
 * 0x57DDA0 -> ChangeToNextBootFlowScreen again, advancing past the splash screen and MC_Main.fng (which loads the
 * profile): a main menu with no profile and no 3D. So "movie finished" reaches a movie screen only while its package
 * (MenuScreen +0x10) is the boot flow's current screen; this guard is installed before the byte is set.
 *
 * Auto load (`[quick_start] auto_load_alias=<save name>`, only if that save exists): the splash screen
 * (MW_LS_Splash.fng, "click to continue") moves on by itself - SplashScreen::NotificationMessage 0x5A32A0 sets
 * bAllowContinue on FEHASH_INITCOMPLETE and advances the boot flow (BootFlowManager::ChangeToNextBootFlowScreen
 * 0x57DBB0) on a button press once that is set; we make that call on INITCOMPLETE, once (it is posted more than once,
 * and each advance skips a screen). Then MC_Main.fng loads that save: UIMemcardMain::ListDone 0x563E90 loads the only
 * save on its own and lists them when there are several; with the alias among them, we do what it does for one save.
 *
 * PC layouts: BootFlowManager (*0x91CF48) current screen +0xC, BootFlowScreen name +0xC. UIMemcardMain: file name
 * +0x40, the saves found +0x7C (list; a node's name at +8, after MemoryCard::GetPrefixLength characters).
 * MemoryCardSetup (0x91CBAC): operation +0 (& 0xF0 == 0x20: the boot flow's), in boot flow +0x30.
 */
#include "quick_start.h"

#include "dev_tools.h"
#include "hooks.h"

namespace dev_tools::quick_start {

namespace {

constexpr uintptr_t kBootFlowManager = 0x91CF48, kMemoryCard = 0x91CB20, kMemcardSetup = 0x91CBAC;
constexpr unsigned kInitComplete = 0x35F8620B, kMovieFinished = 0xC3960EB9;

using ChangeScreenFn = void(__fastcall *)(void *, void *, int);
using NotificationFn = void(__fastcall *)(void *, void *, unsigned, void *, unsigned, unsigned);
using MethodFn = void(__fastcall *)(void *, void *);
using PrefixLengthFn = int(__fastcall *)(void *, void *);
using SetMessageFn = void(__fastcall *)(void *, void *, int, unsigned);
using LoadFn = void(__fastcall *)(void *, void *, const char *);

NotificationFn g_orig_movie_notification, g_orig_splash_notification;
MethodFn g_orig_list_done;
bool g_splash_advanced = false;
char g_alias[64];

bool save_exists(const char *alias) {
    char home[MAX_PATH], path[MAX_PATH];
    if (!GetEnvironmentVariableA("USERPROFILE", home, sizeof(home))) return false;
    snprintf(path, sizeof(path), "%s\\Documents\\NFS Most Wanted\\%s\\%s", home, alias, alias);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

const char *current_screen() {
    char *manager = *reinterpret_cast<char **>(kBootFlowManager);
    char *screen = manager ? *reinterpret_cast<char **>(manager + 0xC) : nullptr;
    return screen ? *reinterpret_cast<const char **>(screen + 0xC) : nullptr;
}

void __fastcall hook_movie_notification(void *self, void *edx, unsigned msg, void *obj, unsigned param1, unsigned param2) {
    if (msg == kMovieFinished) {
        const char *package = *reinterpret_cast<const char **>(static_cast<char *>(self) + 0x10);
        const char *current = current_screen();
        if (!package || !current || _stricmp(package, current) != 0) return;  // no current screen: the boot flow is over
    }
    g_orig_movie_notification(self, edx, msg, obj, param1, param2);
}

void __fastcall hook_splash_notification(void *self, void *edx, unsigned msg, void *obj, unsigned param1, unsigned param2) {
    g_orig_splash_notification(self, edx, msg, obj, param1, param2);
    void *manager = *reinterpret_cast<void **>(kBootFlowManager);
    if (msg != kInitComplete || g_splash_advanced || !manager) return;
    g_splash_advanced = true;
    reinterpret_cast<ChangeScreenFn>(0x57DBB0)(manager, nullptr, 0xff);  // BootFlowManager::ChangeToNextBootFlowScreen
}

void __fastcall hook_list_done(void *self, void *edx) {
    char *card = *reinterpret_cast<char **>(kMemoryCard);
    if (card && (*reinterpret_cast<unsigned *>(kMemcardSetup) & 0xF0) == 0x20) {
        char *list = static_cast<char *>(self) + 0x7C;
        const int prefix = reinterpret_cast<PrefixLengthFn>(0x518350)(card, nullptr);  // MemoryCard::GetPrefixLength
        for (char *node = *reinterpret_cast<char **>(list); node && node != list; node = *reinterpret_cast<char **>(node)) {
            const char *name = node + 8 + prefix;
            if (_stricmp(name, g_alias) != 0) continue;
            // What ListDone does for a single save: show the memory card's messages, then load it.
            card[0x1A] = 1;
            reinterpret_cast<SetMessageFn>(0x7F3CA0)(*reinterpret_cast<void **>(card + 0xC8), nullptr, 0, ~0u);
            *reinterpret_cast<unsigned char *>(kMemcardSetup + 0x30) = 1;
            char *file = static_cast<char *>(self) + 0x40;
            strcpy(file, name);
            reinterpret_cast<LoadFn>(0x5189F0)(card, nullptr, file);  // MemoryCard::Load
            g_log.line("quick start: loading \"%s\"", file);
            return;
        }
    }
    g_orig_list_done(self, edx);
}

// Also when another mod skips the movies (the widescreen fix's SkipIntro): its late messages reach a boot flow that is over
// (*0x91CF48 null) and crash ChangeToNextBootFlowScreen, more so once the auto load has moved on quickly.
bool install_movie_guard() {
    const nfsmw::Hook guard[] = {
        {"moviefinished", "MovieScreen::NotificationMessage", 0x57DDA0, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A}, true,
         nfsmw::detour(&hook_movie_notification), nfsmw::orig(&g_orig_movie_notification)},
    };
    return nfsmw::install_hooks(guard, 1, g_log, g_ini, "skip intros").ok;
}

void skip_intros() {
    constexpr uintptr_t kSkipMovies = 0x926144;
    *reinterpret_cast<unsigned char *>(kSkipMovies) = 1;
    g_log.line("skip intros: movies will be skipped (byte at %#x set)", kSkipMovies);
}

void install_auto_load() {
    GetPrivateProfileStringA("quick_start", "auto_load_alias", "", g_alias, sizeof(g_alias), g_ini);
    if (!g_alias[0]) return;
    if (!save_exists(g_alias)) {
        g_log.line("quick start: there is no save called \"%s\", so no auto load", g_alias);
        return;
    }
    const nfsmw::Hook auto_load[] = {
        {"splash", "SplashScreen::NotificationMessage", 0x5A32A0, {0x51, 0x8B, 0x44, 0x24, 0x08, 0x3D, 0x37}, true,
         nfsmw::detour(&hook_splash_notification), nfsmw::orig(&g_orig_splash_notification)},
        {"listdone", "UIMemcardMain::ListDone", 0x563E90, {0xA1, 0x20, 0xCB, 0x91, 0x00, 0x56, 0x57}, true,
         nfsmw::detour(&hook_list_done), nfsmw::orig(&g_orig_list_done)},
    };
    if (nfsmw::install_hooks(auto_load, 2, g_log, g_ini, "auto load").ok) g_log.line("quick start: auto load of \"%s\"", g_alias);
}

}  // namespace

void install() {
    const bool guarded = install_movie_guard();
    if (ini_flag("quick_start", "skip_intros")) {
        if (guarded) skip_intros();
        else g_log.line("skip intros: movies are not skipped (their guard could not be installed)");
    }
    install_auto_load();
}

}  // namespace dev_tools::quick_start
