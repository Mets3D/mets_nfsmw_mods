#include "confirm_changes.h"

namespace pad::confirm_changes {

namespace {

constexpr unsigned kPadLeftShoulder = 0x5073EF13;   // PAD_LTRIGGER
constexpr unsigned kPadRightShoulder = 0xD9FEEC59;  // PAD_RTRIGGER
constexpr unsigned kPadBack = 0x911AB364;           // PAD_BACK: the screen starts to close
constexpr unsigned kKeepChanges = 0xB850B019;       // the game's question's Yes: keeps the changes, then the device switch
constexpr unsigned kDialogClosed = 0x34DC1BCF;      // the question was left with Back
constexpr unsigned kQuestionText = 0xF98FA043;      // the game's "Do you wish to keep the modified settings? ..." (nfsmw_custom_menus shortens it)
constexpr unsigned kYesLabel = 0x70E01038, kNoLabel = 0x417B25E4;
constexpr unsigned kInGame = 0xE0;  // the pause menu's Controls screen
// UIOptionsPCController::BindingsChanged is true when nothing changed since the screen showed the device; sub_51D590 puts the
// bindings the screen showed back.
constexpr uintptr_t kBindingsUnchanged = 0x51D4B0, kRestoreBindings = 0x51D590, kShowTwoButtons = 0x598C00;
constexpr uintptr_t kPauseDialog = 0x89736C, kMenuDialog = 0x89E580;  // "InGameDialog.fng", "Dialog.fng"

bool g_enabled = false;
bool g_leaving = false;  // the question was answered: the Back that follows goes through
unsigned g_leave_yes = 0, g_leave_no = 0;

void ask_before_leaving(void *screen) {
    using AskFn = int (*)(const char *, const char *, int, unsigned, unsigned, unsigned, unsigned, unsigned, int, unsigned);
    const char *dialog = reinterpret_cast<const char *>(field<unsigned char>(screen, kInGame) ? kPauseDialog : kMenuDialog);
    reinterpret_cast<AskFn>(kShowTwoButtons)(field<const char *>(screen, 0x10), dialog, 1, kYesLabel, kNoLabel, g_leave_yes, g_leave_no,
                                              kDialogClosed, 1, kQuestionText);
}

}  // namespace

void load_settings() {
    g_enabled = GetPrivateProfileIntA("controls", "confirm_keybinds", 0, g_ini) != 0;
    g_leave_yes = nfsmw::bstring_hash("NFSMW_CONTROLS_LEAVE_YES");
    g_leave_no = nfsmw::bstring_hash("NFSMW_CONTROLS_LEAVE_NO");
    log_line("controls: confirm_keybinds=%d", g_enabled);
}

unsigned filter_message(void *screen, unsigned message) {
    // A device switch with changes: the game asks, or (not asking) takes the Yes of its question.
    if (message == kPadLeftShoulder || message == kPadRightShoulder) return g_enabled ? message : kKeepChanges;
    if (message == g_leave_yes || message == g_leave_no) {
        if (message == g_leave_no) reinterpret_cast<MethodFn>(kRestoreBindings)(screen, nullptr);
        g_leaving = true;
        return kPadBack;
    }
    if (message != kPadBack) return message;
    const bool answered = g_leaving;
    g_leaving = false;
    if (!g_enabled || answered || reinterpret_cast<bool(FASTCALL *)(void *, void *)>(kBindingsUnchanged)(screen, nullptr)) return message;
    ask_before_leaving(screen);
    return 0;
}

}  // namespace pad::confirm_changes
