/*
 * A split screen race has a HUD for each of its two local players (FEngHud, types 3 and 4: HUD_Player1.fng with the minimap and HUD_Player2.fng without;
 * a drag race has types 5 and 6, HUD_Drag_Player1.fng and HUD_Drag_Player2.fng, which are only placed in their cells of the screen), made by the game.
 * Every player gets a HUD like player 1's, scaled into their cell of the screen (hud_views.cpp):
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
#include "max_players.h"
#include "hud_packages.h"
#include "hud_views.h"
#include "minimap.h"
#include "more_players.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::hud_players {

namespace {

constexpr int kHudSplit1 = 3, kHudSplit2 = 4;  // ePlayerHudType
constexpr int kHudDrag1 = 5, kHudDrag2 = 6;    // a drag race's two split screen HUDs (players 3 and up get kHudSplit2 in a drag race too: a player without a HUD crashes the race over message)
constexpr const char *kSource = "HUD_Player1.fng";
constexpr const char *kCopies[] = {"HUD_PlayerB.fng", "HUD_PlayerC.fng", "HUD_PlayerD.fng", "HUD_PlayerE.fng", "HUD_PlayerF.fng", "HUD_PlayerG.fng", "HUD_PlayerH.fng"};  // for players 2 and up (same length as the source)
constexpr const char *kDragSource = "HUD_Drag_Player1.fng";
constexpr const char *kDragCopies[] = {"HUD_Drag_PlayerB.fng", "HUD_Drag_PlayerC.fng", "HUD_Drag_PlayerD.fng", "HUD_Drag_PlayerE.fng", "HUD_Drag_PlayerF.fng", "HUD_Drag_PlayerG.fng", "HUD_Drag_PlayerH.fng"};
constexpr int kMaxHuds = nfsmw::kMaxPlayers;
static_assert(sizeof(kCopies) / sizeof(kCopies[0]) >= nfsmw::kMaxPlayers - 1, "a HUD package copy for every player but the first");
// The two kinds of split screen HUD: the game decides which parts of a HUD show from the race (a drag race shows the drag parts only), so the HUDs of a drag race
// are copies of the first player's drag HUD, and players 3 and up get the second type of it.
struct Family {
    int first, second;
    const char *source;
    const char *const *copies;
    bool minimap;  // the HUD has a minimap (the second type is given one when it is a copy)
};
const Family kFamilies[] = {{kHudSplit1, kHudSplit2, kSource, kCopies, true}, {kHudDrag1, kHudDrag2, kDragSource, kDragCopies, false}};
const Family *family_of(int type) {
    for (const Family &f : kFamilies)
        if (type == f.first || type == f.second) return &f;
    return nullptr;
}
const Family *g_family = nullptr;  // the kind of this race
constexpr uintptr_t kMalloc = 0x7C66D0, kMinimapCtor = 0x59D500;
constexpr unsigned kMinimapSize = 0x338, kHudMinimap = 0x320, kHudCountdown = 0x318;
constexpr unsigned kPlayerSetHudSlot = 0x24 / 4;  // IPlayer's virtual table
constexpr unsigned kHudFeatures = 0x18;            // FEngHud: the 64 bits of what the HUD shows (FEngHud::SetHudFeatures)

constexpr float kMinimapRise = -75.0f;  // FE units: the minimap sits on the line between the halves of the screen's layout, where the HUD's half ends
constexpr unsigned kColours[kMaxHuds] = {0x00FF9040, 0x002090FF, 0x0040E060, 0x00C050FF, 0x00F0E040, 0x0040E0E0, 0x00F060A0, 0x00A0A0A0};  // the marker of each player on the minimaps: orange, blue, green, purple, yellow, cyan, pink, grey

void(__fastcall *g_orig_begin_countdown)(void *, void *);
bool g_in_countdown = false;
long g_countdown_offset = -1;  // set once the race has told a HUD to count down (the offset of the pointer the call has from the element's start)

void *g_huds[kMaxHuds];     // by player: 0 = player 1
void *g_players[kMaxHuds];  // their IPlayer
const char *g_packages[kMaxHuds];
int g_hud_count = 0;     // HUDs of this race so far
int g_copies_used = 0;
bool g_extra_huds_made = false;

bool active() { return players::split_screen(); }

int index_of(void *hud) {
    for (int i = 0; i < g_hud_count; ++i)
        if (g_huds[i] == hud) return i;
    return -1;
}

// Each minimap has its player's colour for its own marker, and shows every other player in theirs.
void set_markers() {
    for (int i = 0; i < g_hud_count; ++i) {
        void *minimap = *reinterpret_cast<void **>(static_cast<char *>(g_huds[i]) + kHudMinimap);
        if (!minimap) continue;
        const void *others[kMaxHuds];
        unsigned colours[kMaxHuds];
        int count = 0;
        for (int j = 0; j < g_hud_count; ++j)
            if (j != i) {
                others[count] = g_players[j];
                colours[count++] = kColours[j];
            }
        minimap::set_markers(minimap, g_packages[i], kColours[i], others, colours, count);
    }
}

void hud_filter(int &type, const char *&package) {
    if (!active()) return;
    const Family *family = family_of(type);
    if (!family) return;
    if (type == family->first) {  // player 1's HUD is made first: a new race
        g_hud_count = g_copies_used = 0;
        g_countdown_offset = -1;
        g_extra_huds_made = false;
        g_family = family;
    }
    if (type != family->second || g_copies_used >= kMaxHuds - 1) return;
    if (!hud_packages::ensure_alias(family->source, family->copies[g_copies_used])) return;
    package = family->copies[g_copies_used++];
}

void hud_made(void *hud, int type, const char *package, void *player, int number) {
    g_log.line("hud: type %d, package %s, player %d", type, package ? package : "?", number);
    if (package && ini_flag("options", "dump_hud", false)) minimap::dump_package(package);
    if (!package) return;
    if (!active()) {  // a game with one player: its HUD, whichever it is, goes by the player's HUD scale
        hud_views::assign(package, 0, 1);
        hud_views::classify(package);
        if (void *minimap = *reinterpret_cast<void **>(static_cast<char *>(hud) + kHudMinimap)) minimap::for_each_object(minimap, &hud_views::classify_as_minimap);
        return;
    }
    const Family *family = family_of(type);
    if (g_hud_count >= kMaxHuds || !family) return;
    const int index = g_hud_count++;
    g_huds[index] = hud;
    g_players[index] = player;
    g_packages[index] = package;
    hud_views::assign(package, index, more_players::race_players());
    if (family->minimap && type == family->second && strcmp(package, family->source) != 0) {  // (a package that is not a copy: making it failed: has no minimap objects)
        void *memory = reinterpret_cast<void *(__cdecl *)(size_t)>(kMalloc)(kMinimapSize);
        if (memory) {
            void *minimap = reinterpret_cast<void *(__fastcall *)(void *, void *, const char *, int)>(kMinimapCtor)(memory, nullptr, package, number);
            *reinterpret_cast<void **>(static_cast<char *>(hud) + kHudMinimap) = minimap;
            g_log.line("hud: the HUD of player %d (%s) has a minimap (%p)", index + 1, package, minimap);
        }
    }
    if (void *minimap = *reinterpret_cast<void **>(static_cast<char *>(hud) + kHudMinimap)) minimap::place(minimap, package, kMinimapRise);
    hud_views::classify(package);
    if (void *minimap = *reinterpret_cast<void **>(static_cast<char *>(hud) + kHudMinimap)) minimap::for_each_object(minimap, &hud_views::classify_as_minimap);
    set_markers();
}

int g_updating = -1;  // the player whose HUD the game is updating
int g_pause_requester = -1;

void hud_destroy(void *hud) {
    const int index = index_of(hud);
    if (index < 0) return;
    for (int i = index; i + 1 < g_hud_count; ++i) g_huds[i] = g_huds[i + 1];
    if (--g_hud_count == 0) {
        g_copies_used = 0;
        g_extra_huds_made = false;
        g_family = nullptr;
        g_pause_requester = -1;  // the race is over: the Options of the main menu are player 1's
        hud_views::clear();
    }
}

void hud_before_update(void *hud, void *player) {
    g_updating = index_of(hud);
    more_players::lend_view(player, true);
}
void hud_after_update(void *, void *player) {
    g_updating = -1;
    more_players::lend_view(player, false);
}

// FEngHud::JoyHandle(IPlayer *) reads the player's buttons for the HUD, the pause among them, but not while the HUD has no features: that is what a HUD of a car that
// is totaled or has blown its engine has (nothing to show), and the player could not open the pause menu in a race that goes on for the others.
void(__fastcall *g_orig_joy_handle)(void *, void *, void *);
void __fastcall hook_joy_handle(void *hud, void *edx, void *player) {
    unsigned *features = reinterpret_cast<unsigned *>(static_cast<char *>(hud) + kHudFeatures);
    const bool none = active() && features[0] == 0 && features[1] == 0;
    if (none) features[0] = 1;
    g_orig_joy_handle(hud, edx, player);
    if (none) features[0] = 0;
}

// FEManager::IsOkayToRequestPauseSimulation(player, ?, ?) (0x542670, cdecl) is asked by the EPause event (0x62F220) before the pause menu opens. In a race it looks at the
// racer of the requesting local player and of the other one (index 1 - player: the local list of the game has two): while a racer has one of the flags +0x1C to +0x21
// (finished, engine blown, totaled, knocked out) the request is refused when the other has one too, and the extra players' requests come with the settings slot of player
// 1 or 2, so they are judged by the wrong racers. A split screen race goes on for the others: the flags are put away for the request.
constexpr uintptr_t kIsOkayToRequestPause = 0x542670, kGetRacerInfo = 0x5E8B20, kLocalPlayers = 0x92D87C, kLocalPlayerCount = 0x92D884;
constexpr uintptr_t kRaceStatusPointer = 0x91E000, kFrontendState = 0x925E90, kFeManager = 0x91CB20;
constexpr unsigned kRacerFlags = 0x1C, kRacerFlagCount = 6;
int(__cdecl *g_orig_okay_to_pause)(int, int, int);
int g_pause_refusals_logged = 0;

unsigned char *racer_flags_of_local(int index) {
    void **players = *reinterpret_cast<void ***>(kLocalPlayers);
    const int count = *reinterpret_cast<int *>(kLocalPlayerCount);
    void *status = *reinterpret_cast<void **>(kRaceStatusPointer);
    if (!players || index >= count || !players[index] || !status) return nullptr;
    void **vtable = *reinterpret_cast<void ***>(players[index]);
    void *simable = reinterpret_cast<void *(__fastcall *)(void *, void *)>(vtable[1])(players[index], nullptr);  // IPlayer::GetSimable
    if (!simable) return nullptr;
    unsigned char *info = reinterpret_cast<unsigned char *(__thiscall *)(void *, void *)>(kGetRacerInfo)(status, simable);
    return info ? info + kRacerFlags : nullptr;
}

int __cdecl hook_okay_to_pause(int player, int a, int b) {
    const int ok = g_orig_okay_to_pause(player, a, b) & 0xFF;
    if (ok || !active()) return ok;
    unsigned char saved[2][kRacerFlagCount];
    unsigned char *flags[2] = {racer_flags_of_local(0), racer_flags_of_local(1)};
    for (int i = 0; i < 2; ++i)
        if (flags[i]) {
            memcpy(saved[i], flags[i], kRacerFlagCount);
            memset(flags[i], 0, kRacerFlagCount);
        }
    const int retry = g_orig_okay_to_pause(player, a, b) & 0xFF;
    for (int i = 0; i < 2; ++i)
        if (flags[i]) memcpy(flags[i], saved[i], kRacerFlagCount);
    if (g_pause_refusals_logged++ < 20) {
        const unsigned char *fe = *reinterpret_cast<unsigned char **>(kFeManager);
        g_log.line("pause: the request of player %d was refused%s (state %d, front end flags %d %d; racer flags of players 1 and 2: %02x%02x%02x%02x%02x%02x %02x%02x%02x%02x%02x%02x)", player + 1,
                   retry ? ", allowed without the racers' flags" : " for another reason", *reinterpret_cast<int *>(kFrontendState), fe ? fe[0x1E] : -1, fe ? fe[0x1F] : -1,
                   saved[0][0], saved[0][1], saved[0][2], saved[0][3], saved[0][4], saved[0][5], saved[1][0], saved[1][1], saved[1][2], saved[1][3], saved[1][4], saved[1][5]);
    }
    return retry;
}

// EPause::EPause(settings index, ?, ?): the pause request of a player's HUD (FEngHud::JoyHandle) is made while that HUD updates.
void(__fastcall *g_orig_epause)(void *, void *, int, int, int);
void __fastcall hook_epause(void *self, void *edx, int player, int b, int c) {
    g_orig_epause(self, edx, player, b, c);
    g_pause_requester = g_updating >= 0 ? g_updating : -1;
}

// Players 3 and 4: once the game has made the HUDs of players 1 and 2.
void make_extra_huds() {
    if (g_extra_huds_made || !active() || g_hud_count < 2 || more_players::extra_player_count() == 0) return;
    g_extra_huds_made = true;
    for (int i = 0; i < more_players::extra_player_count(); ++i) {
        void *player = more_players::extra_player(i);
        void **vtable = *reinterpret_cast<void ***>(player);
        reinterpret_cast<void(__fastcall *)(void *, void *, int)>(vtable[kPlayerSetHudSlot])(player, nullptr, g_family ? g_family->second : kHudSplit2);
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

int player_of_hud(void *hud) { return index_of(hud); }
int pause_requester() { return g_pause_requester; }

void install() {
    const nfsmw::Hook hooks[] = {
        {"pause", "EPause::EPause", 0x623A90, {0x8B, 0x54, 0x24, 0x08, 0x8B, 0xC1, 0x8B}, false, nfsmw::detour(&hook_epause), nfsmw::orig(&g_orig_epause)},
        {"countdown", "Countdown::BeginCountdown", 0x567F10, {0x68, 0x00, 0x05, 0x8A, 0x00, 0x6A, 0x08}, false, nfsmw::detour(&hook_begin_countdown),
         nfsmw::orig(&g_orig_begin_countdown)},
        {"joyhandle", "FEngHud::JoyHandle", 0x57C660, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A}, false, nfsmw::detour(&hook_joy_handle), nfsmw::orig(&g_orig_joy_handle)},
        {"okaypause", "FEManager::IsOkayToRequestPauseSimulation", kIsOkayToRequestPause, {0x51, 0x83, 0x3D, 0x90, 0x5E, 0x92, 0x00}, false, nfsmw::detour(&hook_okay_to_pause),
         nfsmw::orig(&g_orig_okay_to_pause)},
    };
    nfsmw::install_hooks(hooks, 4, g_log, nullptr, "hud players");
    events::on_hud_construct(&hud_filter);
    events::on_hud_made(&hud_made);
    events::on_hud_destroy(&hud_destroy);
    events::on_hud_update(&hud_before_update, &hud_after_update);
    events::on_frame(&make_extra_huds);
}

}  // namespace split_screen_support::hud_players
