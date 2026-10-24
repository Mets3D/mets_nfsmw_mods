// The leaving question of the Options screens (UIOptionsScreen, UIEATraxScreen, CustomTuningScreen) has Yes = message 0x775DBA97 (put the
// old values back, then leave) and No = 0x34DC1BCF (stay). Here Yes sends our message instead, which does what leaving with nothing changed
// does (the exit animation, whose end keeps the values), and No sends the discard message; Back (the dialog's cancel message) still stays.
// The dialog's text is replaced in the config before it is shown, and the answer first goes through cFEng::QueueGameMessage, where our
// message becomes the exit animation (what the screens queue themselves when nothing changed).
#include "simplify_confirmations.h"

#include "hooks.h"
#include "translations.h"

#define THISCALL __attribute__((thiscall))

namespace custom_menus::simplify_confirmations {

namespace {

nfsmw::Logger *g_log = nullptr;
bool g_enabled = false;
unsigned g_save_and_leave = 0;

constexpr unsigned kLeaveText = 0xE9CB802F;  // "Are you sure you want to exit without accepting changes?^All changes will be lost."
constexpr unsigned kKeepText = 0xF98FA043;   // "Do you wish to keep the modified settings?  Selecting "No" will cause you to lose all changes."
constexpr unsigned kYesText = 0x70E01038, kNoText = 0x417B25E4;
constexpr unsigned kDiscardAndLeave = 0x775DBA97;
constexpr unsigned kExitAnimation = 0x587C018B;

// feDialogConfig (see skip_dialogs.cpp): the blurb at +0, {text hash, message} per button, the button that starts selected at +0x21C
// (0 the first; the game gives 1, the second, which would be the one that loses the changes).
constexpr unsigned kButtons = 0x204, kFirstButton = 0x21C, kParentPackage = 0x224;

const auto GetLocalizedString = reinterpret_cast<const char *(*)(unsigned)>(0x56BC10);
using QueueGameMessageFn = void(FASTCALL *)(void *, void *, unsigned, const char *, unsigned);
using QueueMessageFn = void(THISCALL *)(void *feng, unsigned msg, const char *pkg, int object, unsigned port);
const auto QueueMessage = reinterpret_cast<QueueMessageFn>(0x516BE0);
QueueGameMessageFn g_orig_queue_game_message;

void FASTCALL hook_queue_game_message(void *feng, void *edx, unsigned msg, const char *pkg, unsigned port) {
    if (msg != g_save_and_leave) {
        g_orig_queue_game_message(feng, edx, msg, pkg, port);
        return;
    }
    QueueMessage(feng, kExitAnimation, pkg, -4, 0xFF);
}

bool is_text(const char *blurb, unsigned hash) {
    const char *text = GetLocalizedString(hash);
    return text && !strcmp(blurb, text);
}

}  // namespace

bool enabled() { return g_enabled; }

void rewrite(void *config) {
    if (!g_enabled) return;
    char *blurb = static_cast<char *>(config);
    const bool leaving = is_text(blurb, kLeaveText);
    if (!leaving && !is_text(blurb, kKeepText)) return;
    if (leaving) {
        // Yes keeps and leaves, No loses and leaves; only when the buttons are the ones expected.
        unsigned *yes = nullptr, *no = nullptr;
        for (int i = 0; i < 3; ++i) {
            unsigned *button = &nfsmw::field<unsigned>(config, kButtons + 8 * i);
            if (button[0] == kYesText && button[1] == kDiscardAndLeave) yes = button;
            else if (button[0] == kNoText) no = button;
        }
        if (!yes || !no) {
            g_log->line("Save changes: the leaving question has other buttons than expected, left as it is");
            return;
        }
        yes[1] = g_save_and_leave;
        no[1] = kDiscardAndLeave;
        unsigned &first = nfsmw::field<unsigned>(config, kFirstButton);
        if (first == 1) first = 0;
    }
    snprintf(blurb, 0x200, "%s", translations::translate("Save changes?"));
}

void install_hooks(nfsmw::Logger &log, const char *ini) {
    g_enabled = GetPrivateProfileIntA("features", "simplify_confirmations", 1, ini) != 0;
    if (!g_enabled) return;
    g_log = &log;
    g_save_and_leave = nfsmw::bstring_hash("NFSMW_SAVE_CHANGES_AND_LEAVE");
    const nfsmw::Hook hooks[] = {
        {"queuegamemessage", "cFEng::QueueGameMessage", 0x516C50, {0x8B, 0x44, 0x24, 0x0C, 0x8B, 0x54, 0x24}, false,
         nfsmw::detour(&hook_queue_game_message), nfsmw::orig(&g_orig_queue_game_message)},
    };
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), log, ini, "simplified confirmations");
}

}  // namespace custom_menus::simplify_confirmations
