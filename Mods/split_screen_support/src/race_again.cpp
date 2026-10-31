/*
 * After a split screen race the race options come back (after_race.cpp). Their Accept (UIQRTrackOptions::NotificationMessage 0x7AB430: it makes the race,
 * then ends in a tail jump to QueuePackageSwitch("PressStart.fng") at 0x7AB56D) starts the next race at once with the cars and devices of the last one
 * (more_players keeps every player's pick) when the Players row still says as many players; otherwise the Press Start and Car Select screens come as the
 * first time. A new split screen game (the entry of the Quick Race menu) forgets the cars.
 *
 * "2 Select Car" in the hint bar (a clone of the "1 Defaults" hint, common/fe_hint.h) opens the Car Select of the player whose device pressed 2 (X on a
 * controller: mets_controller_support shows the button), and it comes back to the race options. The game cannot tell the devices apart (every one is
 * port 0), so the press is read here, as players.cpp reads Start; a click on the hint is the keyboard's player's.
 */
#include "race_again.h"

#include <windows.h>

#include "fe_hint.h"
#include "hooks.h"
#include "more_players.h"
#include "patch.h"
#include "player_count.h"
#include "players.h"
#include "split_screen_support.h"
#include "xinput.h"

namespace split_screen_support::race_again {

namespace {

namespace hint = nfsmw::fe_hint;

constexpr uintptr_t kNotify = 0x7AB430, kAcceptSwitchJump = 0x7AB56D;
constexpr unsigned kScreenTick = 0xC98356BA, kExitComplete = 0xE1FDE1D1, kScreenPackage = 0x10;
constexpr unsigned kClickMsg = 0xCA3E5C01, kHintName = 0xCA3E5CA0, kHintGuidBase = 0xCA3E5D00;
constexpr int kKey2 = 0x03, kNumpad2 = 0x50;  // DirectInput codes
constexpr int kXInputSlots = 4;

hint::Hint g_hint;
void *g_screen = nullptr;  // the race options screen the hint was tried on
bool g_shown = false;
bool g_was_down[kXInputSlots + 1];  // the controllers' X, the keyboard's 2 (last)
bool g_changing = false;            // a Car Select was asked for from this screen

bool kept() { return players::split_screen() && more_players::cars_kept(player_count::wanted()); }

void select_car(int device) {
    const int player = more_players::kept_player_of_device(device);
    if (player < 0) {
        g_log.line("Select Car: %s plays no one", device == players::kKeyboard ? "the keyboard" : "that controller");
        return;
    }
    g_changing = true;
    more_players::change_car(player);
}

// Which devices let go of X / 2 since the last frame (`act` false: only what is held now, at the screen's first tick).
void poll(bool act) {
    for (int i = 0; i <= kXInputSlots; ++i) {
        bool down = false;
        if (i < kXInputSlots) {
            nfsmw::xinput::Gamepad p;
            down = nfsmw::xinput::read_slot(i, &p) && (p.buttons & nfsmw::xinput::kButtonX);
        } else {
            unsigned char keys[256];
            down = players::read_keyboard(keys) && ((keys[kKey2] | keys[kNumpad2]) & 0x80);
        }
        const bool released = g_was_down[i] && !down;
        g_was_down[i] = down;
        if (released && act && !g_changing) select_car(i < kXInputSlots ? i : players::kKeyboard);
    }
}

void tick(void *screen) {
    if (screen != g_screen) {  // the screen's first tick: its layout is live
        g_screen = screen;
        g_changing = false;
        g_shown = true;
        poll(false);  // a press that began before the screen is not one
        const hint::Look look = {"Select Car", hint::kPcKey2Texture, kClickMsg, kHintName, kHintGuidBase};
        const char *why = "";
        if (g_hint.add(screen, nfsmw::field<const char *>(screen, kScreenPackage), look, &why)) g_log.line("race options: the Select Car hint is there");
        else g_log.line("race options: no Select Car hint (%s)", why);
    }
    g_hint.sync(screen);
    const bool show = kept();
    if (g_hint.on(screen) && show != g_shown) {  // (the Players row was changed)
        g_shown = show;
        reinterpret_cast<void (*)(void *)>(show ? 0x514CC0 : 0x514C70)(g_hint.root());  // FEngSetVisible / FEngSetInvisible
    }
    if (show) poll(true);
}

using NotifyFn = void(__fastcall *)(void *, void *, unsigned, void *, unsigned, unsigned);
NotifyFn g_orig_notify;
void __fastcall hook_notify(void *screen, void *edx, unsigned msg, void *obj, unsigned p1, unsigned p2) {
    if (players::split_screen()) {
        if (msg == kClickMsg || (msg == hint::kButton5Msg && obj && g_hint.contains(obj))) {  // a click on our hint
            if (kept() && !g_changing) select_car(players::kKeyboard);
            return;
        }
        if (msg == kScreenTick) tick(screen);
        if (msg == kExitComplete && screen == g_screen) {  // the screen goes (another one may be made where it was)
            g_screen = nullptr;
            g_hint.clear();
        }
    }
    g_orig_notify(screen, edx, msg, obj, p1, p2);
}

// The Accept's QueuePackageSwitch("PressStart.fng", 0, 0, false), reached by a jump with the screen's arguments in place (thiscall: callee pops 16 bytes).
uintptr_t g_accept_switch = 0;
void __fastcall accept_switch(void *feng, void *edx, const char *package, int a, unsigned b, bool c) {
    if (kept() && !g_changing) {
        more_players::start_race_again();
        return;
    }
    more_players::forget_cars();  // the players choose again (another number of players)
    reinterpret_cast<void(__fastcall *)(void *, void *, const char *, int, unsigned, bool)>(g_accept_switch)(feng, edx, package, a, b, c);
}

}  // namespace

void test_select_car(int device) {
    if (g_screen && kept() && !g_changing) select_car(device);
    else g_log.line("Select Car (test): no race options with kept cars showing");
}

void install() {
    if (!ini_flag("options", "race_again", true)) return;
    const nfsmw::Hook hooks[] = {
        {"raceoptions", "UIQRTrackOptions::NotificationMessage", kNotify, {0x8B, 0x44, 0x24, 0x10, 0x8B, 0x54, 0x24}, false, nfsmw::detour(&hook_notify),
         nfsmw::orig(&g_orig_notify)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, g_ini, "race again");
    g_accept_switch = nfsmw::chain_jump(kAcceptSwitchJump, reinterpret_cast<uintptr_t>(&accept_switch));
    if (!g_accept_switch) g_log.line("race again: the jump at %#x is not as expected; Accept goes to Press Start as in the game", static_cast<unsigned>(kAcceptSwitchJump));
}

}  // namespace split_screen_support::race_again
