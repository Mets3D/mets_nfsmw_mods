/*
 * A "Players" row (2, 3 or 4) in the race options that follow the track select (UIQRTrackOptions, Track_Options.fng) of a split screen
 * race. The screen builds its rows in Setup (0x7BA7C0), which calls one of SetupCircuit / SetupSprint ... and sets the first row as the
 * cursor's; after it returns our row is added the way nfsmw_custom_menus builds the rows of its pages: an FEToggleWidget (constructor
 * 0x589300, 0x5C bytes) whose virtual table is PODriveCam's (the table those rows use) with Act and Draw replaced, handed to
 * UIWidgetMenu::AddToggleOption (0x588570). The row's texts are language strings registered through nfsmw_custom_menus (without it
 * there is no row, and the race has two players).
 * Left and right change the value: 2, 3 or 4 (with 3 the second round of Press Start / Car Select is for one player: see more_players.cpp).
 */
#include "player_count.h"

#include "hooks.h"
#include "patch.h"
#include "more_players.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::player_count {

namespace {

constexpr uintptr_t kNew = 0x652AD0, kAddToggleOption = 0x588570, kToggleWidgetCtor = 0x589300, kToggleDeletingDestructor = 0x589370,
                    kRowVtable = 0x89C048, kSetLanguageHash = 0x515C00, kRefreshWidgets = 0x588D20;
constexpr unsigned kWidgetSize = 0xB0;  // the largest widget of the Options screens is 0x9D bytes
constexpr unsigned kChanged = 0x2A, kTitle = 0x2C, kData = 0x30;  // FEToggleWidget: changed flag, label and value strings
constexpr unsigned kPadLeft = 0x9120409E, kPadRight = 0xB5971BF1;
constexpr int kBlinkSlot = 14, kDrawSlot = 3;

const unsigned kTitleHash = nfsmw::bstring_hash("SPLIT_PLAYERS");
const unsigned kValueHash[5] = {0, 0, nfsmw::bstring_hash("SPLIT_PLAYERS_2"), nfsmw::bstring_hash("SPLIT_PLAYERS_3"), nfsmw::bstring_hash("SPLIT_PLAYERS_4")};

int g_wanted = 2;
bool g_texts = false;
void *g_vtable[15];

template <typename T>
T &field(void *object, unsigned offset) { return *reinterpret_cast<T *>(static_cast<char *>(object) + offset); }

void set_hash(void *string, unsigned hash) { reinterpret_cast<void (*)(void *, unsigned)>(kSetLanguageHash)(string, hash); }

// FEToggleWidget::Draw
void __fastcall draw(void *self, void *) {
    set_hash(field<void *>(self, kTitle), kTitleHash);
    set_hash(field<void *>(self, kData), kValueHash[g_wanted]);
}

// FEToggleWidget::Act(const char *package, uint32 pad message)
void __fastcall act(void *self, void *, const char *, unsigned data) {
    if (data != kPadLeft && data != kPadRight) return;
    g_wanted = data == kPadRight ? (g_wanted == 4 ? 2 : g_wanted + 1) : (g_wanted == 2 ? 4 : g_wanted - 1);
    field<unsigned char>(self, kChanged) = 1;
    void **vtable = field<void **>(self, 0);
    reinterpret_cast<void(__fastcall *)(void *, void *, unsigned)>(vtable[kBlinkSlot])(self, nullptr, data);  // the arrow blinks
    reinterpret_cast<void(__fastcall *)(void *, void *)>(vtable[kDrawSlot])(self, nullptr);
    g_log.line("race options: %d players", g_wanted);
}

void add_row(void *screen) {
    void *widget = reinterpret_cast<void *(*)(size_t)>(kNew)(kWidgetSize);
    if (!widget) return;
    memset(widget, 0, kWidgetSize);
    reinterpret_cast<void *(__fastcall *)(void *, void *, int)>(kToggleWidgetCtor)(widget, nullptr, 1);
    field<void **>(widget, 0) = g_vtable;
    reinterpret_cast<unsigned(__fastcall *)(void *, void *, void *, int)>(kAddToggleOption)(screen, nullptr, widget, 1);
    reinterpret_cast<void(__fastcall *)(void *, void *)>(kRefreshWidgets)(screen, nullptr);  // Draw() on every row: until then the layout's placeholder texts show
}

constexpr uintptr_t kSetup = 0x7BA7C0;          // UIQRTrackOptions::Setup
constexpr uintptr_t kSetupCallSite = 0x7BF73D;  // its only call, in the screen's constructor (0x7BF6C0)

using SetupFn = void(__fastcall *)(void *, void *);

uintptr_t g_setup = kSetup;  // what the constructor called before the redirect: Setup, or another mod's function that calls it

// What the constructor calls instead.
void __fastcall setup_and_row(void *screen, void *edx) {
    reinterpret_cast<SetupFn>(g_setup)(screen, edx);
    more_players::reset();  // a new race setup: the next race starts with the first round of players
    g_log.line("race options built: split screen %d, texts %d", players::split_screen(), g_texts);
    if (!players::split_screen() || !g_texts) return;
    add_row(screen);
    g_log.line("race options: the Players row was added");
}

}  // namespace

int wanted() { return g_wanted; }

void register_texts(const nfsmw::custom_menus::Host *host) {
    if (!ini_flag("options", "more_players", false)) return;
    host->register_text(kTitleHash, "Players");
    host->register_text(kValueHash[2], "2");
    host->register_text(kValueHash[3], "3");
    host->register_text(kValueHash[4], "4");
    g_texts = true;
}

void install() {
    void **source = reinterpret_cast<void **>(kRowVtable);
    for (int i = 0; i < 15; ++i) g_vtable[i] = source[i];
    g_vtable[0] = reinterpret_cast<void *>(kToggleDeletingDestructor);
    g_vtable[1] = reinterpret_cast<void *>(&act);
    g_vtable[kDrawSlot] = reinterpret_cast<void *>(&draw);
    // The call is redirected, not the function hooked: other mods change functions too, and a hook on Setup was never reached.
    if (const uintptr_t previous = nfsmw::chain_call(kSetupCallSite, reinterpret_cast<uintptr_t>(&setup_and_row))) {
        g_setup = previous;
        g_log.line("race options: the Players row will be added (the constructor called %#x%s)", static_cast<unsigned>(previous),
                   previous == kSetup ? ", Setup" : ", not Setup: another mod redirected it");
    } else {
        g_log.line("race options: the call at %#x is not a call; no Players row", static_cast<unsigned>(kSetupCallSite));
    }
}

}  // namespace split_screen_support::player_count
