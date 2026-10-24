// Confirmations whose answer is always Yes, and notices whose only button is OK, are answered the moment they would
// be shown: the screen that asked gets the same message the button would have sent. The dialogs are recognized by
// their text, compared with the game's own string for the current language.
//  - Message boxes (DialogInterface::ShowDialog, every Show* variant ends there): the answer is the message of the
//    button labelled Yes, queued to the screen that asked (what feDialogScreen's destructor does).
//  - The save system's messages (MemcardCallbacks::ShowMessage, texts from MEMCARD/LOCALE_<language>.loc): the
//    answer goes to MemoryCard::MessageDone, which queues it for the save system's next update.
//  - The save screen's own prompts (UIMemcardBase::ShowYesNo / ShowOK): the screen goes on the way its Yes / OK
//    button takes it. The two "save the alias" questions (SetupPromptForSave, SetupPromptSaveConfirm) are answered
//    at their setup. "Save complete." is left to the player.
//  - The transmission question of the split screen's car selection: the player's own setting is used.
#include "skip_dialogs.h"

#include <algorithm>

#include "hooks.h"
#include "simplify_confirmations.h"

#define THISCALL __attribute__((thiscall))

namespace custom_menus::skip_dialogs {

namespace {

nfsmw::Logger *g_log = nullptr;
bool g_fewer_popups = false;

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
        if (!g_fewer_popups) break;
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
    simplify_confirmations::rewrite(config);
    return g_orig_show_dialog(config);
}

// ---- the save system's messages -------------------------------------------------------------------------------
// Ids of MemoryCard::GetLocaleString (the .loc file's strings, from 1).
constexpr int kLocaleYes = 3;
constexpr int kLocaleMessages[] = {
    7,   // "Do you want to overwrite previously saved game?" (Yes / No)
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

// ---- the save screen's own prompts ------------------------------------------------------------------------------
// The save screen's state is in global flags (0x91CBAC); the prompt being shown is their top nibble. The Yes (and
// the OK) button's handler is UIMemcardBase::HandleButtonPressed, which reads the prompt from the flags and the
// button from the object's name hash (+0x10), so a stand-in object for the Yes button is enough.
constexpr uintptr_t kMemcardFlags = 0x91CBAC, kAnsweredPrompt = 0x91CBCC;
constexpr unsigned kPromptMask = 0x0F000000, kYesButton = 0xB8A7C6CD, kButtonName = 0x10;

struct Prompt {
    unsigned text;  // language hash of the blurb
    unsigned id;    // the prompt id ShowYesNo / ShowOK is given
};
constexpr Prompt kPrompts[] = {
    {0x7209349F, 0x5000000},  // "Creating a new alias will result in any unsaved changes ... being lost. Are you sure ...?"
    {0x87C7577E, 0x6000000},  // "Loading a new alias will result in the loss of any unsaved changes ... Are you sure ...?"
    {0xB01BE5DD, 0x8000000},  // "Need for Speed Most Wanted uses an autosave feature. ..." (OK)
    {0xB380362F, 0x9000000},  // "Do not reset/switch off the computer while the autosave icon is displayed. ..." (OK)
};

using HandleButtonFn = void(THISCALL *)(void *screen, unsigned msg, void *button, unsigned param1, unsigned param2, bool pad_back);
const auto HandleButtonPressed = at<HandleButtonFn>(0x563380);

using PromptFn = void(FASTCALL *)(void *, void *, unsigned, unsigned);
PromptFn g_orig_show_yes_no, g_orig_show_ok;

// True when the prompt was answered; the screen then shows nothing of it.
bool answer_prompt(void *screen, unsigned text, unsigned id) {
    for (const Prompt &prompt : kPrompts) {
        if (prompt.text != text || prompt.id != id) continue;
        unsigned &flags = *reinterpret_cast<unsigned *>(kMemcardFlags);
        flags = (flags & ~kPromptMask) | id;
        alignas(8) unsigned char button[0x40] = {};
        nfsmw::field<unsigned>(button, kButtonName) = kYesButton;
        g_log->line("answered the save screen's prompt %#x", text);
        HandleButtonPressed(screen, 0, button, 0, 0, false);
        return true;
    }
    return false;
}

void FASTCALL hook_show_yes_no(void *screen, void *edx, unsigned text, unsigned id) {
    if (!answer_prompt(screen, text, id)) g_orig_show_yes_no(screen, edx, text, id);
}

void FASTCALL hook_show_ok(void *screen, void *edx, unsigned text, unsigned id) {
    if (!answer_prompt(screen, text, id)) g_orig_show_ok(screen, edx, text, id);
}

// "Do you want to save the alias <name>?" (asked after the name of a new alias is typed) and "Are you sure you want to
// save the alias named <name>?" (after pressing Save). Only their plain texts are answered; the variants (EA login,
// unsaved changes, saving over an existing alias) are left to the player.
constexpr unsigned kOtherPrompts = 0x8000 | 0x40000 | 0x200000;
constexpr unsigned kSaveConfirmPrompt = 0x4000000;  // ShowYesNo's prompt id, which the Yes handler records
constexpr int kSaveFlowConfirmed = 12;              // where UIMemcardBase::HandleButtonPressed takes the flow on Yes
const auto DoSaveFlow = at<void(THISCALL *)(void *screen, int step)>(0x544370);

using MethodFn = void(FASTCALL *)(void *, void *);
MethodFn g_orig_prompt_for_save, g_orig_save_confirm;

bool answer_save_question(void *screen) {
    if (*reinterpret_cast<unsigned *>(kMemcardFlags) & kOtherPrompts) return false;
    g_log->line("answered Yes to saving the alias");
    *reinterpret_cast<unsigned *>(kAnsweredPrompt) = kSaveConfirmPrompt;
    DoSaveFlow(screen, kSaveFlowConfirmed);
    return true;
}

void FASTCALL hook_prompt_for_save(void *screen, void *edx) {
    if (!answer_save_question(screen)) g_orig_prompt_for_save(screen, edx);
}

void FASTCALL hook_save_confirm(void *screen, void *edx) {
    if (!answer_save_question(screen)) g_orig_save_confirm(screen, edx);
}

// ---- the transmission question of the split screen ---------------------------------------------------------------
// The car selection asks "Select your transmission" when the player's "Prompt for Transmission" is on (a byte of the
// player's settings in the profile). In the split screen the question is skipped: while the screen handles a message
// the byte reads Off, so the game takes the way it has with the prompt off and the player's setting stays as it is.
constexpr uintptr_t kFeDatabase = 0x91CF90;
constexpr unsigned kDatabaseSettings = 0x10, kPlayerSettings = 0x80, kPlayerSettingsSize = 20, kTransmissionPrompt = 6;
constexpr unsigned kCarSelectPlayer = 0x1A8;
const auto IsSplitScreenMode = at<bool(THISCALL *)(void *database)>(0x469120);

using CarSelectMessageFn = void(FASTCALL *)(void *, void *, unsigned, void *, unsigned, unsigned);
CarSelectMessageFn g_orig_car_select_message;
void FASTCALL hook_car_select_message(void *screen, void *edx, unsigned msg, void *object, unsigned param1, unsigned param2) {
    void *database = *reinterpret_cast<void **>(kFeDatabase);
    const int player = nfsmw::field<int>(screen, kCarSelectPlayer);
    if (!database || (player != 0 && player != 1) || !IsSplitScreenMode(database)) {
        g_orig_car_select_message(screen, edx, msg, object, param1, param2);
        return;
    }
    bool &prompt = nfsmw::field<bool>(nfsmw::field<void *>(database, kDatabaseSettings),
                                      kPlayerSettings + kPlayerSettingsSize * player + kTransmissionPrompt);
    const bool asked = prompt;
    prompt = false;
    g_orig_car_select_message(screen, edx, msg, object, param1, param2);
    prompt = asked;
}

}  // namespace

void install_hooks(nfsmw::Logger &log, const char *ini) {
    g_fewer_popups = GetPrivateProfileIntA("features", "fewer_popups", 1, ini) != 0;
    g_log = &log;
    // The message box hook also carries the simplified confirmations (simplify_confirmations.cpp).
    if (g_fewer_popups || simplify_confirmations::enabled()) {
        const nfsmw::Hook hooks[] = {
            {"showdialog", "DialogInterface::ShowDialog", 0x5983C0, {0x56, 0x8B, 0x74, 0x24, 0x08, 0x57, 0x8B}, false,
             nfsmw::detour(&hook_show_dialog), nfsmw::orig(&g_orig_show_dialog)},
        };
        nfsmw::install_hooks(hooks, 1, log, ini, "dialogs");
    }
    if (!g_fewer_popups) return;
    const nfsmw::Hook hooks[] = {
        {"memcardmessage", "MemcardCallbacks::ShowMessage", 0x560980, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A}, false,
         nfsmw::detour(&hook_memcard_message), nfsmw::orig(&g_orig_memcard_message)},
        {"showyesno", "UIMemcardBase::ShowYesNo", 0x543CC0, {0x8B, 0x44, 0x24, 0x04, 0x53, 0x56, 0x50}, false,
         nfsmw::detour(&hook_show_yes_no), nfsmw::orig(&g_orig_show_yes_no)},
        {"showok", "UIMemcardBase::ShowOK", 0x543BE0, {0x8B, 0x44, 0x24, 0x04, 0x53, 0x56, 0x50}, false,
         nfsmw::detour(&hook_show_ok), nfsmw::orig(&g_orig_show_ok)},
        {"promptforsave", "UIMemcardBase::SetupPromptForSave", 0x544210, {0x81, 0xEC, 0x00, 0x02, 0x00, 0x00, 0x56}, false,
         nfsmw::detour(&hook_prompt_for_save), nfsmw::orig(&g_orig_prompt_for_save)},
        {"saveconfirm", "UIMemcardBase::SetupPromptSaveConfirm", 0x543EB0, {0xA1, 0xAC, 0xCB, 0x91, 0x00, 0x81, 0xEC},
         false, nfsmw::detour(&hook_save_confirm), nfsmw::orig(&g_orig_save_confirm)},
        {"carselect", "UIQRCarSelect::NotificationMessage", 0x7C1770, {0x81, 0xEC, 0x24, 0x03, 0x00, 0x00, 0x53}, false,
         nfsmw::detour(&hook_car_select_message), nfsmw::orig(&g_orig_car_select_message)},
    };
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), log, ini, "skipped dialogs");
}

}  // namespace custom_menus::skip_dialogs
