/*
 * The track streamer (TrackStreamer, 0x9AA9D0) knows two positions it keeps the track sections around: `StreamingPositionEntries[2]`, 0x64 bytes each
 * from +0xC0, with about a dozen loops over exactly two in its code and other fields right behind the array, so it cannot be made bigger. Camera::UpdateAll
 * (0x480C16) calls UpdateCameraMovers (0x47C880), which gives each active view that has a camera mover a position (PredictStreamingPosition, 0x7238D0):
 * views 1 and 2. The views of players 3 and 4 (extra_cameras.cpp) are not active, so the world around them was not kept.
 * With three or four players, after the game has done that, the two positions are chosen again among the cars: the pair that keeps every car nearest to
 * one of them (the two-centre of the cars) in the plane of the track. A position of player 1 or 2 keeps the game's own entry (with its speed and
 * direction); for player 3 or 4 the entry is made with PredictStreamingPosition. Cars far enough apart for one to be out of the range of both will
 * see the world pop in around it: the streamer has room for two places.
 */
#include "extra_streaming.h"

#include <algorithm>
#include <cmath>

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

void __cdecl update_camera_movers(float dt) {
    reinterpret_cast<void(__cdecl *)(float)>(g_update_camera_movers)(dt);
    if (!players::split_screen() || more_players::extra_player_count() == 0) return;

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

void install() {
    if (const uintptr_t previous = nfsmw::chain_call(kUpdateAllCallSite, reinterpret_cast<uintptr_t>(&update_camera_movers)))
        g_update_camera_movers = previous;
    else
        g_log.line("streaming: the call at %#x is not a call; the streamer knows only two places", static_cast<unsigned>(kUpdateAllCallSite));
}

}  // namespace split_screen_support::extra_streaming
