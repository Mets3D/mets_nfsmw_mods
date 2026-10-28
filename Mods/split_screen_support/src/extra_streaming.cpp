/*
 * The track streamer (TrackStreamer, 0x9AA9D0) knows two positions it keeps the track sections around: `StreamingPositionEntries[2]`, 0x64 bytes each
 * from +0xC0, with about a dozen loops over exactly two in its code and other fields right behind the array, so it cannot be made bigger. Camera::UpdateAll
 * (0x480C16) calls UpdateCameraMovers (0x47C880), which gives each active view that has a camera mover a position (PredictStreamingPosition, 0x7238D0):
 * views 1 and 2. The views of players 3 and 4 (extra_cameras.cpp) are not active, so the world around them was not kept.
 * With three or four players, after the game has done that, the two positions are chosen again among the cars: the pair that keeps every car nearest to
 * one of them (the two-centre of the cars) in the plane of the track. A position of player 1 or 2 keeps the game's own entry (with its speed and
 * direction); for player 3 or 4 the entry is made with PredictStreamingPosition.
 * The cars that have no position of the two get an extra place: the game works out which sections a position wants from its zone (DetermineStreamingSections
 * 0x72DD70: `CurrentZone` of an entry, in the list of loading sections 0x9A3B70 or the list of drivable sections 0x9A3B60, then AddCurrentStreamingSections
 * 0x728060 with the entry's number, which sets that bit in the sections' `CurrentlyVisible`), and what the zone is comes from the entry's position and speed
 * (GetPredictedZone 0x72DB80, which uses nothing else). So an extra place is an entry of our own, its zone worked out the same way (DetermineCurrentZones
 * 0x72DF20 is hooked: it says "the zones are different" when an extra's zone changes, which makes the game switch zones, and gives the extras' zones a short delay
 * like its own), and, once DetermineStreamingSections is done, the sections of that zone are added, as the entry of the nearest of the two positions (the
 * loading priority of a section is from the positions that have its bit; every section in the list loads). 54 sections (213 MB, of a pool of 242 MB) are loaded
 * for two places, of which 188 MB are the three that every place has (X0, Y0, Z0): each place of its own costs 8 to 18 MB.
 * The memory the streamer keeps what it has loaded in is its pool: streaming_pool.cpp.
 */
#include "extra_streaming.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "hooks.h"
#include "max_players.h"
#include "more_players.h"
#include "patch.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::extra_streaming {

namespace {

constexpr uintptr_t kUpdateAllCallSite = 0x480C16, kUpdateCameraMovers = 0x47C880, kStreamer = 0x9AA9D0, kPredictStreamingPosition = 0x7238D0;
constexpr unsigned kEntries = 0xC0, kEntrySize = 0x64, kEntryFlag = 0x1D;
constexpr unsigned kEntryPlace = 0x20, kEntryPredictedZoneValidTime = 0x24;  // an entry: position (+0), elevation, velocity (+0xC), direction (+0x14), position set (+0x1C), following a car (+0x1D); then its zones (+0x20: current, predicted, the time the prediction is good until) and its loading counters
constexpr int kPlaces = 2;  // what the game's track streamer has: two places it keeps the track around

uintptr_t g_update_camera_movers = kUpdateCameraMovers;

constexpr uintptr_t kDetermineStreamingSections = 0x72DD70, kDetermineCurrentZones = 0x72DF20, kAddCurrentStreamingSections = 0x728060, kGetPredictedZone = 0x72DB80,
                    kGetSectionsToLoad = 0x72C750, kVisibleSectionManager = 0x9A3B50, kLoadingSections = 0x9A3B70, kDrivableSections = 0x9A3B60;
constexpr unsigned kEntryZone = 0x1E;  // an entry's CurrentZone
constexpr int kMaxSectionsToLoad = 0x180;
constexpr int kZoneDelay = 8;  // the frames an extra's new zone must be the prediction before it is taken

// The players that have no position of the streamer's two, and where they are (the entries are the streamer's own layout, 0x64 bytes).
struct Extra {
    int player = -1;
    int slot = 0;                    // the position (0 or 1) whose bit its sections get: the nearest
    short zone = 0;                  // the zone its sections are for (0: none yet)
    short pending = 0;
    int pending_frames = 0;
    unsigned char entry[kEntrySize] = {};
};
Extra g_extras[nfsmw::kMaxPlayers];
int g_extra_count = 0;
bool g_extras_changed = false;  // who has an extra place has changed

// The game render space: (x, y, z) of the simulation become (z, -x, y); the streamer works in its x, y plane.
void to_render(const float *sim, float *out) {
    out[0] = sim[2];
    out[1] = -sim[0];
    out[2] = sim[1];
}

float distance_squared(const float *a, const float *b) {
    const float dx = a[0] - b[0], dy = a[1] - b[1];
    return dx * dx + dy * dy;
}

// Where each car was at the last update (render space), for its velocity.
float g_last_position[nfsmw::kMaxPlayers][3];
bool g_last_known[nfsmw::kMaxPlayers];

// The sections of a zone, as DetermineStreamingSections 0x72DD70 finds them (a loading section lists zones; else the zone is a drivable section with its own list).
int sections_of_zone(short zone, short *out) {
    for (void *node = *reinterpret_cast<void **>(kLoadingSections); node != reinterpret_cast<void *>(kLoadingSections); node = *reinterpret_cast<void **>(node)) {
        const int count = *reinterpret_cast<short *>(static_cast<char *>(node) + 0x18);
        const short *zones = reinterpret_cast<short *>(static_cast<char *>(node) + 0x1A);
        for (int i = 0; i < count; ++i)
            if (zones[i] == zone)
                return reinterpret_cast<int(__fastcall *)(void *, void *, void *, short *, int)>(kGetSectionsToLoad)(reinterpret_cast<void *>(kVisibleSectionManager), nullptr, node, out,
                                                                                                                      kMaxSectionsToLoad);
    }
    for (void *node = *reinterpret_cast<void **>(kDrivableSections); node != reinterpret_cast<void *>(kDrivableSections); node = *reinterpret_cast<void **>(node)) {
        if (*reinterpret_cast<short *>(static_cast<char *>(node) + 0xC) != zone) continue;
        const int count = std::min<int>(*reinterpret_cast<short *>(static_cast<char *>(node) + 0x10), kMaxSectionsToLoad);
        memcpy(out, static_cast<char *>(node) + 0x12, count * sizeof(short));
        return count;
    }
    return 0;
}

// Works out the zones of the extra places; true when one of them has changed (or who has a place has).
bool update_extra_zones() {
    bool changed = g_extras_changed;
    g_extras_changed = false;
    for (int k = 0; k < g_extra_count; ++k) {
        Extra &extra = g_extras[k];
        const short predicted = reinterpret_cast<short(__fastcall *)(void *, void *, void *)>(kGetPredictedZone)(reinterpret_cast<void *>(kStreamer), nullptr, extra.entry);
        if (predicted == extra.zone) {
            extra.pending_frames = 0;
        } else if (extra.zone == 0) {
            extra.zone = predicted;
            changed = true;
        } else if (predicted == extra.pending) {
            if (++extra.pending_frames >= kZoneDelay) {
                extra.zone = predicted;
                extra.pending_frames = 0;
                changed = true;
            }
        } else {
            extra.pending = predicted;
            extra.pending_frames = 1;
        }
    }
    return changed;
}

unsigned char(__fastcall *g_orig_determine_zones)(void *, void *, short *);
unsigned char __fastcall hook_determine_zones(void *self, void *edx, short *zones) {
    const unsigned char changed = g_orig_determine_zones(self, edx, zones);
    if (g_extra_count == 0 || !players::split_screen()) return changed;
    return update_extra_zones() ? 1 : changed;
}

void(__fastcall *g_orig_determine_sections)(void *, void *);
void __fastcall hook_determine_sections(void *self, void *edx) {
    g_orig_determine_sections(self, edx);
    if (g_extra_count == 0 || !players::split_screen()) return;
    short sections[kMaxSectionsToLoad];
    for (int k = 0; k < g_extra_count; ++k) {
        if (g_extras[k].zone <= 0) continue;
        const int count = sections_of_zone(g_extras[k].zone, sections);
        if (count <= 0) continue;
        reinterpret_cast<void(__fastcall *)(void *, void *, short *, int, int)>(kAddCurrentStreamingSections)(self, nullptr, sections, count, g_extras[k].slot);
        static int logged = 0;
        if (logged++ < 20) g_log.line("streaming: the extra place of player %d: zone %d, %d sections", g_extras[k].player + 1, g_extras[k].zone, count);
    }
}

void __cdecl update_camera_movers(float dt) {
    reinterpret_cast<void(__cdecl *)(float)>(g_update_camera_movers)(dt);
    if (!players::split_screen() || more_players::extra_player_count() == 0) {
        g_extra_count = 0;
        return;
    }

    const int count = nfsmw::kPlayersPerRound + more_players::extra_player_count();
    float position[nfsmw::kMaxPlayers][3], forward[nfsmw::kMaxPlayers][3], velocity[nfsmw::kMaxPlayers][3] = {};
    for (int i = 0; i < count; ++i) {
        float sim_position[3], sim_forward[3];
        if (!more_players::player_car_state(i, sim_position, sim_forward)) return;
        to_render(sim_position, position[i]);
        to_render(sim_forward, forward[i]);
        // The game gives a car's entry the camera's velocity: the streamer looks that far ahead (GetPredictedZone) to have the next zone loaded in time.
        if (g_last_known[i] && dt > 0.0001f)
            for (int axis = 0; axis < 3; ++axis) velocity[i][axis] = (position[i][axis] - g_last_position[i][axis]) / dt;
        memcpy(g_last_position[i], position[i], sizeof(g_last_position[i]));
        g_last_known[i] = true;
    }
    int first = 0, second = 1;
    float best = 3.0e38f;
    for (int a = 0; a < count; ++a)
        for (int b = a + 1; b < count; ++b) {
            float worst = 0;
            for (int i = 0; i < count; ++i) {
                const float nearest = std::min(distance_squared(position[i], position[a]), distance_squared(position[i], position[b]));
                worst = std::max(worst, nearest);
            }
            if (worst < best) {
                best = worst;
                first = a;
                second = b;
            }
        }
    // The others have extra places.
    {
        int extras = 0;
        for (int i = 0; i < count; ++i) {
            if (i == first || i == second) continue;
            Extra &extra = g_extras[extras++];
            if (extra.player != i) {
                extra.player = i;
                extra.zone = extra.pending = 0;
                extra.pending_frames = 0;
                g_extras_changed = true;
            }
            extra.slot = distance_squared(position[i], position[first]) <= distance_squared(position[i], position[second]) ? 0 : 1;
            float *place = reinterpret_cast<float *>(extra.entry);  // position x, y, elevation, velocity x, y, direction x, y: as PredictStreamingPosition makes an entry
            place[0] = position[i][0];
            place[1] = position[i][1];
            place[2] = position[i][2];
            place[3] = velocity[i][0];
            place[4] = velocity[i][1];
            place[5] = forward[i][0];
            place[6] = forward[i][1];
            extra.entry[0x1C] = 1;  // set
            extra.entry[0x1D] = 1;  // following a car
        }
        if (g_extra_count != extras) {
            g_extra_count = extras;
            g_extras_changed = true;
        }
    }
    static int slot_player[kPlaces] = {0, 1};  // whose position each slot has been given
    unsigned char *entries = reinterpret_cast<unsigned char *>(kStreamer) + kEntries;
    if (first == 0 && second == 1) {  // the game's own two
        for (int slot = 0; slot < kPlaces; ++slot)
            if (slot_player[slot] != slot) {
                slot_player[slot] = slot;
                *reinterpret_cast<int *>(entries + slot * kEntrySize + kEntryPredictedZoneValidTime) = -1;
            }
        return;
    }

    unsigned char own[kPlaces][kEntrySize];
    memcpy(own[0], entries, kEntrySize);
    memcpy(own[1], entries + kEntrySize, kEntrySize);
    const int chosen[kPlaces] = {first, second};
    for (int slot = 0; slot < kPlaces; ++slot) {
        const int player = chosen[slot];
        unsigned char *entry = entries + slot * kEntrySize;
        if (player < 2) {
            memcpy(entry, own[player], kEntryPlace);  // the place, speed and direction; what the streamer keeps in the rest (its zones, what it has loaded) stays with the slot
        } else {
            reinterpret_cast<void(__fastcall *)(void *, void *, int, const float *, const float *, const float *, int)>(kPredictStreamingPosition)(
                reinterpret_cast<void *>(kStreamer), nullptr, slot, position[player], velocity[player], forward[player], own[0][kEntryFlag]);
        }
        if (slot_player[slot] != player) {  // another car: the zone predicted for the last one is not this one's
            slot_player[slot] = player;
            *reinterpret_cast<int *>(entry + kEntryPredictedZoneValidTime) = -1;
        }
    }
    static bool logged = false;
    if (!logged) {
        logged = true;
        g_log.line("streaming: players %d and %d keep the track around them (they are the best pair for the %d cars)", first + 1, second + 1, count);
    }
}

}  // namespace

int extra_places(int *players, int *zones, int *slots, int max) {
    const int count = std::min(g_extra_count, max);
    for (int k = 0; k < count; ++k) {
        players[k] = g_extras[k].player;
        zones[k] = g_extras[k].zone;
        slots[k] = g_extras[k].slot;
    }
    return count;
}

void install() {
    const nfsmw::Hook hooks[] = {
        {"streamzones", "TrackStreamer::DetermineCurrentZones", kDetermineCurrentZones, {0x51, 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x10}, false, nfsmw::detour(&hook_determine_zones),
         nfsmw::orig(&g_orig_determine_zones)},
        {"streamsections", "TrackStreamer::DetermineStreamingSections", kDetermineStreamingSections, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x81}, false,
         nfsmw::detour(&hook_determine_sections), nfsmw::orig(&g_orig_determine_sections)},
    };
    nfsmw::install_hooks(hooks, 2, g_log, g_ini, "extra streaming");
    if (const uintptr_t previous = nfsmw::chain_call(kUpdateAllCallSite, reinterpret_cast<uintptr_t>(&update_camera_movers)))
        g_update_camera_movers = previous;
    else
        g_log.line("streaming: the call at %#x is not a call; the streamer knows only two places", static_cast<unsigned>(kUpdateAllCallSite));
}

}  // namespace split_screen_support::extra_streaming
