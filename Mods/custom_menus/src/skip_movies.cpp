// The PC build lets Accept / Start skip a movie only under conditions: the story movies (FEAnyMovieScreen) and the in-game ones
// (InGameAnyMovieScreen) check a profile setting and how long the movie has played (or the "skip movies" byte), and the tutorials
// (FEAnyTutorialScreen, InGameAnyTutorialScreen) only enable the screen's input (message 0x59291F95) when the tutorial has been
// seen before. Here Accept / Start end the movie the way its own end does ("movie finished", 0xC3960EB9), and the tutorials
// always get the message that enables the input.
#include "skip_movies.h"

#include "hooks.h"

#define THISCALL __attribute__((thiscall))

namespace custom_menus::skip_movies {

namespace {

constexpr unsigned kAccept = 0x406415E3, kStart = 0xB5AF2461, kMovieFinished = 0xC3960EB9, kEnableInput = 0x59291F95;
constexpr uintptr_t kFEng = 0x91CADC;  // *kFEng is the cFEng
constexpr unsigned kPackage = 0x10;    // MenuScreen: the screen's package name

const auto QueuePackageMessage = reinterpret_cast<void(THISCALL *)(void *feng, unsigned msg, const char *pkg, void *object)>(0x516C90);

using NotificationFn = void(FASTCALL *)(void *, void *, unsigned, void *, unsigned, unsigned);
NotificationFn g_orig_story_notification, g_orig_ingame_notification;

unsigned as_finished(unsigned msg) { return msg == kAccept || msg == kStart ? kMovieFinished : msg; }

void FASTCALL hook_story_notification(void *self, void *edx, unsigned msg, void *obj, unsigned p1, unsigned p2) {
    g_orig_story_notification(self, edx, as_finished(msg), obj, p1, p2);
}

void FASTCALL hook_ingame_notification(void *self, void *edx, unsigned msg, void *obj, unsigned p1, unsigned p2) {
    g_orig_ingame_notification(self, edx, as_finished(msg), obj, p1, p2);
}

using CtorFn = void *(FASTCALL *)(void *, void *, void *);
CtorFn g_orig_tutorial_ctor, g_orig_ingame_tutorial_ctor;

void enable_input(void *screen) {
    void *feng = *reinterpret_cast<void **>(kFEng);
    const char *package = *reinterpret_cast<const char **>(static_cast<char *>(screen) + kPackage);
    if (feng && package) QueuePackageMessage(feng, kEnableInput, package, nullptr);
}

void *FASTCALL hook_tutorial_ctor(void *self, void *edx, void *data) {
    void *screen = g_orig_tutorial_ctor(self, edx, data);
    enable_input(screen);
    return screen;
}

void *FASTCALL hook_ingame_tutorial_ctor(void *self, void *edx, void *data) {
    void *screen = g_orig_ingame_tutorial_ctor(self, edx, data);
    enable_input(screen);
    return screen;
}

}  // namespace

void install_hooks(nfsmw::Logger &log, const char *ini) {
    if (!GetPrivateProfileIntA("features", "skippable_cutscenes", 1, ini)) return;
    const nfsmw::Hook hooks[] = {
        {"storymovie", "FEAnyMovieScreen::NotificationMessage", 0x559880, {0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C, 0x8D}, false,
         nfsmw::detour(&hook_story_notification), nfsmw::orig(&g_orig_story_notification)},
        {"ingamemovie", "InGameAnyMovieScreen::NotificationMessage", 0x57E400, {0x56, 0x8B, 0x74, 0x24, 0x08, 0x57, 0x8D}, false,
         nfsmw::detour(&hook_ingame_notification), nfsmw::orig(&g_orig_ingame_notification)},
        {"tutorial", "FEAnyTutorialScreen::FEAnyTutorialScreen", 0x54EF70, {0x6A, 0xFF, 0x68, 0x02, 0x07, 0x87, 0x00}, false,
         nfsmw::detour(&hook_tutorial_ctor), nfsmw::orig(&g_orig_tutorial_ctor)},
        {"ingametutorial", "InGameAnyTutorialScreen::InGameAnyTutorialScreen", 0x58F320, {0x6A, 0xFF, 0x68, 0xC2, 0x27, 0x87, 0x00}, false,
         nfsmw::detour(&hook_ingame_tutorial_ctor), nfsmw::orig(&g_orig_ingame_tutorial_ctor)},
    };
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), log, ini, "skip movies");
}

}  // namespace custom_menus::skip_movies
