/*
 * Nothing is changed here. Two things are logged so that a split screen game that does not work can be read:
 *  - a race is about to load (events.cpp): the FEDatabase says how many players, which devices and mode (after
 *    players.cpp has set player 2's port).
 *  - MaybeChangeViewMode (0x6CFAD0), which runs every frame: each change of the view mode (EVIEWMODE: 0 none, 1 one
 *    view, 2 one view and the rear view mirror, 3 two views one above the other, 4 side by side).
 */
#include "state_log.h"

#include "events.h"
#include "hooks.h"
#include "split_screen_support.h"

namespace split_screen_support::state_log {

namespace {

constexpr uintptr_t kFeDatabase = 0x91CF90;
constexpr uintptr_t kRequestedViewMode = 0x982B44, kWantedViewMode = 0x982B3C, kViewMode = 0x982B40;
constexpr uintptr_t kQrMode = 0x9B9E68;

void log_world_load() {
    const unsigned char *db = *reinterpret_cast<unsigned char **>(kFeDatabase);
    if (db)
        g_log.line("world load: players %u, joystick ports %d and %d, game mode flags %#x, Quick Race menu choice %d",
                   db[0], static_cast<signed char>(db[0x0C]), static_cast<signed char>(db[0x0D]),
                   *reinterpret_cast<const unsigned *>(db + 0x12C), *reinterpret_cast<const int *>(kQrMode));
}

void (*g_orig_maybe_change_view_mode)();
void hook_maybe_change_view_mode() {
    static int last[3] = {-1, -1, -1};
    g_orig_maybe_change_view_mode();
    const int now[3] = {*reinterpret_cast<int *>(kRequestedViewMode), *reinterpret_cast<int *>(kWantedViewMode),
                        *reinterpret_cast<int *>(kViewMode)};
    if (memcmp(now, last, sizeof(now)) == 0) return;
    memcpy(last, now, sizeof(now));
    g_log.line("view mode: requested %d, wanted %d, current %d", now[0], now[1], now[2]);
}

}  // namespace

void install() {
    events::on_world_load(&log_world_load);
    const nfsmw::Hook hooks[] = {
        {"viewmode", "MaybeChangeViewMode", 0x6CFAD0, {0xA1, 0x44, 0x2B, 0x98, 0x00, 0x85, 0xC0}, false,
         nfsmw::detour(&hook_maybe_change_view_mode), nfsmw::orig(&g_orig_maybe_change_view_mode)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, nullptr, "state log");
}

}  // namespace split_screen_support::state_log
