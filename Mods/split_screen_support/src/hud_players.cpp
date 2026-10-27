/*
 * Two players have a HUD each (FEngHud, types 3 and 4: HUD_Player1.fng with the minimap and HUD_Player2.fng without), made by the game for the local
 * players. With more players every player gets a HUD like player 1's, scaled into their cell of the screen (hud_views.cpp):
 *   - each HUD needs a package of its own (the HUD finds its objects by the package's name), so the HUDs of players 2, 3 and 4 get copies of
 *     HUD_Player1.fng under other names (hud_packages.cpp), swapped in for the package name the game hands the constructor;
 *   - a HUD of type 4 has no minimap (the game makes one for types 1 and 3 only): the copy has the objects, so a Minimap is made on it and put in the
 *     HUD's minimap slot (+0x320), where the HUD's update, its feature switches and its destructor find it;
 *   - players 3 and 4 are not in the local player list (more_players.cpp), so the game makes no HUD for them: the HUD is made by LocalPlayer::SetHud
 *     (IPlayer virtual table +0x24) on their entity, which the game then updates with the others (its HUD task). The HUD code asks the player's
 *     render port for the view's camera mover (FEngHud::DetermineHudFeatures: without a camera that drives the car it shows nothing), so the
 *     player has a render port during their HUD's update.
 *   - the race start tells the HUDs of the local players to begin the 3-2-1-Go countdown (Countdown::BeginCountdown, 0x567F10: the HUD's
 *     countdown element is at +0x318); the others are told as well.
 * Objects layouts are the PC version's: see agent_docs/split-screen.md.
 */
#include "hud_players.h"

#include <cstdlib>
#include <cstring>

#include "events.h"
#include "hooks.h"
#include "hud_packages.h"
#include "hud_views.h"
#include "minimap.h"
#include "more_players.h"
#include "split_screen_support.h"

namespace split_screen_support::hud_players {

namespace {

constexpr int kHudSplit1 = 3, kHudSplit2 = 4;  // ePlayerHudType
constexpr const char *kSource = "HUD_Player1.fng";
constexpr const char *kCopies[] = {"HUD_PlayerB.fng", "HUD_PlayerC.fng", "HUD_PlayerD.fng"};  // for players 2, 3 and 4
constexpr int kMaxHuds = 4;
constexpr uintptr_t kMalloc = 0x7C66D0, kMinimapCtor = 0x59D500;
constexpr unsigned kMinimapSize = 0x338, kHudMinimap = 0x320, kHudCountdown = 0x318;
constexpr unsigned kPlayerSetHudSlot = 0x24 / 4;  // IPlayer's virtual table

constexpr float kMinimapRise = -75.0f;  // FE units: the minimap sits on the line between the halves of the screen's layout, where the HUD's half ends
constexpr unsigned kColours[kMaxHuds] = {0x00FF9040, 0x002090FF, 0x0040E060, 0x00C050FF};  // the marker of each player on the minimaps: orange, blue, green, purple

void(__fastcall *g_orig_begin_countdown)(void *, void *);
bool g_in_countdown = false;
long g_countdown_offset = -1;  // set once the race has told a HUD to count down (the offset of the pointer the call has from the element's start)

void *g_huds[kMaxHuds];     // by player: 0 = player 1
void *g_players[kMaxHuds];  // their IPlayer
int g_hud_count = 0;     // HUDs of this race so far
int g_copies_used = 0;
bool g_extra_huds_made = false;

bool active() { return more_players::four_player_race(); }

int index_of(void *hud) {
    for (int i = 0; i < g_hud_count; ++i)
        if (g_huds[i] == hud) return i;
    return -1;
}

// Each minimap has its player's colour for its own marker, and shows player 2 (player 1's minimap) or player 1 (the others) as its second one.
void set_markers() {
    for (int i = 0; i < g_hud_count; ++i) {
        void *minimap = *reinterpret_cast<void **>(static_cast<char *>(g_huds[i]) + kHudMinimap);
        const int other = i == 0 ? 1 : 0;
        if (minimap && other < g_hud_count) minimap::set_markers(minimap, kColours[i], g_players[other], kColours[other]);
    }
}

void hud_filter(int &type, const char *&package) {
    if (!active()) return;
    if (type == kHudSplit1) {  // player 1's HUD is made first: a new race
        g_hud_count = g_copies_used = 0;
        g_countdown_offset = -1;
        g_extra_huds_made = false;
    }
    if (type != kHudSplit2 || g_copies_used >= 3) return;
    if (!hud_packages::ensure_alias(kSource, kCopies[g_copies_used])) return;
    package = kCopies[g_copies_used++];
}

void hud_made(void *hud, int type, const char *package, void *player, int number) {
    if (!active() || g_hud_count >= kMaxHuds) return;
    if (type != kHudSplit1 && type != kHudSplit2) return;
    const int index = g_hud_count++;
    g_huds[index] = hud;
    g_players[index] = player;
    hud_views::assign(package, index, 4);
    if (type == kHudSplit2 && strcmp(package, kSource) != 0) {  // (a package that is not a copy: making it failed: has no minimap objects)
        void *memory = reinterpret_cast<void *(__cdecl *)(size_t)>(kMalloc)(kMinimapSize);
        if (memory) {
            void *minimap = reinterpret_cast<void *(__fastcall *)(void *, void *, const char *, int)>(kMinimapCtor)(memory, nullptr, package, number);
            *reinterpret_cast<void **>(static_cast<char *>(hud) + kHudMinimap) = minimap;
            g_log.line("hud: the HUD of player %d (%s) has a minimap (%p)", index + 1, package, minimap);
        }
    }
    if (void *minimap = *reinterpret_cast<void **>(static_cast<char *>(hud) + kHudMinimap)) minimap::place(minimap, package, kMinimapRise);
    set_markers();
}

void hud_destroy(void *hud) {
    const int index = index_of(hud);
    if (index < 0) return;
    for (int i = index; i + 1 < g_hud_count; ++i) g_huds[i] = g_huds[i + 1];
    if (--g_hud_count == 0) {
        g_copies_used = 0;
        g_extra_huds_made = false;
        hud_views::clear();
    }
}

void hud_before_update(void *, void *player) { more_players::lend_view(player, true); }
void hud_after_update(void *, void *player) { more_players::lend_view(player, false); }

// Players 3 and 4: once the game has made the HUDs of players 1 and 2.
void make_extra_huds() {
    if (g_extra_huds_made || !active() || g_hud_count < 2 || more_players::extra_player_count() != 2) return;
    g_extra_huds_made = true;
    for (int i = 0; i < 2; ++i) {
        void *player = more_players::extra_player(i);
        void **vtable = *reinterpret_cast<void ***>(player);
        reinterpret_cast<void(__fastcall *)(void *, void *, int)>(vtable[kPlayerSetHudSlot])(player, nullptr, kHudSplit2);
    }
    if (g_countdown_offset >= 0) {  // the race counted down already: they start late, with the HUDs' own count
        g_in_countdown = true;
        for (int i = 2; i < g_hud_count; ++i)
            if (char *element = *reinterpret_cast<char **>(static_cast<char *>(g_huds[i]) + kHudCountdown)) g_orig_begin_countdown(element + g_countdown_offset, nullptr);
        g_in_countdown = false;
    }
    g_log.line("hud: %d HUDs after the HUDs of players 3 and 4 were made (countdown %s)", g_hud_count, g_countdown_offset >= 0 ? "already begun" : "not begun");
}


// `self` is the countdown element of one HUD as the game's call has it (the pointer to the interface inside it): the others get the same offset.
void __fastcall hook_begin_countdown(void *self, void *edx) {
    g_orig_begin_countdown(self, edx);
    if (g_in_countdown || !active()) return;
    long offset = -1;
    for (int i = 0; i < g_hud_count && offset < 0; ++i) {
        const char *element = *reinterpret_cast<char **>(static_cast<char *>(g_huds[i]) + kHudCountdown);
        if (element && static_cast<char *>(self) >= element && static_cast<char *>(self) < element + 0x80) offset = static_cast<char *>(self) - element;
    }
    if (offset < 0) return;
    g_countdown_offset = offset;
    g_in_countdown = true;
    for (int i = 0; i < g_hud_count; ++i) {
        char *element = *reinterpret_cast<char **>(static_cast<char *>(g_huds[i]) + kHudCountdown);
        if (element && element + offset != self) g_orig_begin_countdown(element + offset, edx);
    }
    g_in_countdown = false;
}

}  // namespace

void install() {
    const nfsmw::Hook hooks[] = {
        {"countdown", "Countdown::BeginCountdown", 0x567F10, {0x68, 0x00, 0x05, 0x8A, 0x00, 0x6A, 0x08}, false, nfsmw::detour(&hook_begin_countdown),
         nfsmw::orig(&g_orig_begin_countdown)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, nullptr, "hud players");
    events::on_hud_construct(&hud_filter);
    events::on_hud_made(&hud_made);
    events::on_hud_destroy(&hud_destroy);
    events::on_hud_update(&hud_before_update, &hud_after_update);
    events::on_frame(&make_extra_huds);
}

}  // namespace split_screen_support::hud_players
