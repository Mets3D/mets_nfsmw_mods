// Confirmations whose answer is always Yes, and notices whose only button is OK, are answered the moment they would
// be shown: the screen that asked gets the same message the button would have sent. The dialogs are recognized by
// their text, compared with the game's own string for the current language.
//  - Message boxes (DialogInterface::ShowDialog, every Show* variant ends there): the answer is the message of the
//    button labelled Yes, queued to the screen that asked (what feDialogScreen's destructor does).
//  - The save system's messages (MemcardCallbacks::ShowMessage, texts from MEMCARD/LOCALE_<language>.loc): the
//    answer goes to MemoryCard::MessageDone, which queues it for the save system's next update.
//  - The save screen's "save the alias named ...?" (UIMemcardBase::SetupPromptSaveConfirm): the save flow goes on
//    the way the screen's Yes button takes it.
#include "skip_dialogs.h"

#include <algorithm>

#include "hooks.h"

#define THISCALL __attribute__((thiscall))

namespace custom_menus::skip_dialogs {

namespace {

nfsmw::Logger *g_log = nullptr;

template <typename Fn>
Fn at(uintptr_t addr) { return reinterpret_cast<Fn>(addr); }

// ---- message boxes ----------------------------------------------------------------------------------------------
constexpr unsigned kFeDialogs[] = {
    0x71D9E710,  // "Do you wish to apply these items to your vehicle?" (car customization, outside the career)
    0x892CB612,  // "Do you wish to add all maximum unlocked performance packages to your shopping cart?"
    0xEB694C0C,  // "Are you sure you want to leave the Safe House to resume free roam?"
    0x77CF03C5,  // "Are you sure you want to launch this event?"
};
constexpr unsigned kYesText = 0x70E01038;  // the Yes button's label

// feDialogConfig: the blurb (already formatted) at +0, then {text hash, message} per button.
constexpr unsigned kButtons = 0x204, kParentPackage = 0x224, kButtonCount = 0x22C;

const auto GetLocalizedString = at<const char *(*)(unsigned hash)>(0x56BC10);
const auto QueueGameMessage = at<void(THISCALL *)(void *feng, unsigned msg, const char *pkg, unsigned port)>(0x516C50);
constexpr uintptr_t kFEng = 0x91CADC;  // cFEng*

using ShowDialogFn = int (*)(void *config);
ShowDialogFn g_orig_show_dialog;
int hook_show_dialog(void *config) {
    const char *blurb = static_cast<const char *>(config);
    for (unsigned hash : kFeDialogs) {
        const char *text = GetLocalizedString(hash);
        if (!text || strcmp(blurb, text) != 0) continue;
        const int buttons = std::min(nfsmw::field<int>(config, kButtonCount), 3);
        for (int i = 0; i < buttons; ++i) {
            if (nfsmw::field<unsigned>(config, kButtons + 8 * i) != kYesText) continue;
            const unsigned answer = nfsmw::field<unsigned>(config, kButtons + 8 * i + 4);
            QueueGameMessage(*reinterpret_cast<void **>(kFEng), answer, nfsmw::field<const char *>(config, kParentPackage),
                             0xFF);
            g_log->line("answered Yes to dialog %#x", hash);
            return 0;  // the handle; none of these callers keep it
        }
    }
    return g_orig_show_dialog(config);
}

// ---- the save system's messages -------------------------------------------------------------------------------
// Ids of MemoryCard::GetLocaleString (the .loc file's strings, from 1).
constexpr int kLocaleYes = 3;
constexpr int kLocaleMessages[] = {
    7,   // "Do you want to overwrite previously saved game?" (Yes / No)
    8,   // "Save complete."
    10,  // "Load complete."
};

using Text = const unsigned short *;  // UTF-16
const auto GetLocaleString = at<Text (*)(int id)>(0x518380);
const auto MessageDone = at<void(THISCALL *)(void *card, int choice)>(0x518410);  // choice: the button, from 1
constexpr uintptr_t kMemoryCard = 0x91CB20;  // MemoryCard*
constexpr unsigned kWaitingForAnswer = 0x14;  // bool, set by ShowMessage, read and cleared by MessageDone

bool same(Text a, Text b) {
    if (!a || !b) return false;
    while (*a && *a == *b) ++a, ++b;
    return *a == *b;
}

// The text for the log (ASCII; anything else as '?').
void narrow(Text text, char *out, size_t size) {
    size_t n = 0;
    for (; text && *text && n + 1 < size; ++text) out[n++] = *text < 0x80 ? static_cast<char>(*text) : '?';
    out[n] = 0;
}

// The button that answers `text`, from 1, or 0 when it is not one of ours.
int answer_of(Text text, unsigned count, const Text *buttons) {
    for (int id : kLocaleMessages) {
        if (!same(text, GetLocaleString(id))) continue;
        if (count == 1) return 1;
        for (unsigned i = 0; i < count; ++i)
            if (same(buttons[i], GetLocaleString(kLocaleYes))) return static_cast<int>(i) + 1;
    }
    return 0;
}

using MemcardMessageFn = void(FASTCALL *)(void *, void *, Text, unsigned, const Text *);
MemcardMessageFn g_orig_memcard_message;
void FASTCALL hook_memcard_message(void *callbacks, void *edx, Text text, unsigned count, const Text *buttons) {
    char shown[128];
    narrow(text, shown, sizeof(shown));
    const int answer = count ? answer_of(text, count, buttons) : 0;
    void *card = *reinterpret_cast<void **>(kMemoryCard);
    if (!answer || !card) {
        if (count) g_log->line("save system message: \"%s\" (%u buttons)", shown, count);
        g_orig_memcard_message(callbacks, edx, text, count, buttons);
        return;
    }
    g_log->line("answered button %d to \"%s\"", answer, shown);
    nfsmw::field<bool>(card, kWaitingForAnswer) = true;
    MessageDone(card, answer);
}

// ---- the save screen's alias prompt -----------------------------------------------------------------------------
// The save screen's state is in global flags (0x91CBAC); these pick another text for the prompt (EA login, unsaved
// changes, saving over an existing alias), which is left to the player.
constexpr uintptr_t kMemcardFlags = 0x91CBAC, kAnsweredPrompt = 0x91CBCC;
constexpr unsigned kOtherPrompts = 0x8000 | 0x40000 | 0x200000;
constexpr unsigned kSaveConfirmPrompt = 0x4000000;  // ShowYesNo's prompt id, which the Yes handler records
constexpr int kSaveFlowConfirmed = 12;              // where UIMemcardBase::HandleButtonPressed takes the flow on Yes
const auto DoSaveFlow = at<void(THISCALL *)(void *screen, int step)>(0x544370);

using MethodFn = void(FASTCALL *)(void *, void *);
MethodFn g_orig_save_confirm;
void FASTCALL hook_save_confirm(void *screen, void *edx) {
    if (*reinterpret_cast<unsigned *>(kMemcardFlags) & kOtherPrompts) {
        g_orig_save_confirm(screen, edx);
        return;
    }
    g_log->line("answered Yes to saving the alias");
    *reinterpret_cast<unsigned *>(kAnsweredPrompt) = kSaveConfirmPrompt;
    DoSaveFlow(screen, kSaveFlowConfirmed);
}

}  // namespace

void install_hooks(nfsmw::Logger &log, const char *ini) {
    if (!GetPrivateProfileIntA("features", "skip_dialogs", 1, ini)) return;
    g_log = &log;
    const nfsmw::Hook hooks[] = {
        {"showdialog", "DialogInterface::ShowDialog", 0x5983C0, {0x56, 0x8B, 0x74, 0x24, 0x08, 0x57, 0x8B}, false,
         nfsmw::detour(&hook_show_dialog), nfsmw::orig(&g_orig_show_dialog)},
        {"memcardmessage", "MemcardCallbacks::ShowMessage", 0x560980, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A}, false,
         nfsmw::detour(&hook_memcard_message), nfsmw::orig(&g_orig_memcard_message)},
        {"saveconfirm", "UIMemcardBase::SetupPromptSaveConfirm", 0x543EB0, {0xA1, 0xAC, 0xCB, 0x91, 0x00, 0x81, 0xEC},
         false, nfsmw::detour(&hook_save_confirm), nfsmw::orig(&g_orig_save_confirm)},
    };
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), log, ini, "skipped dialogs");
}

}  // namespace custom_menus::skip_dialogs
