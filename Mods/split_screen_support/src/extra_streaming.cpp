/*
 * The track streamer (TrackStreamer, 0x9AA9D0) knows two positions it keeps the track sections around: `StreamingPositionEntries[2]`, 0x64 bytes each
 * from +0xC0, with about a dozen loops over exactly two in its code and other fields right behind the array, so it cannot be made bigger. Camera::UpdateAll
 * (0x480C16) calls UpdateCameraMovers (0x47C880), which gives each active view that has a camera mover a position (PredictStreamingPosition, 0x7238D0):
 * views 1 and 2. Players 3 and 4 have views of our own and no mover, so the world around them was not kept.
 * With three or four players, after the game has done that, the two positions are chosen again among the cars: the pair that keeps every car nearest to
 * one of them (the two-centre of the cars) in the plane of the track. A position of player 1 or 2 keeps the game's own entry (with its speed and
 * direction); for player 3 or 4 the entry is made with PredictStreamingPosition. Cars far enough apart for one to be out of the range of both will
 * see the world pop in around it: the streamer has room for two places.
 */
#include "extra_streaming.h"

#include <algorithm>

#include "more_players.h"
#include "patch.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::extra_streaming {

namespace {

constexpr uintptr_t kUpdateAllCallSite = 0x480C16, kUpdateCameraMovers = 0x47C880, kStreamer = 0x9AA9D0, kPredictStreamingPosition = 0x7238D0;
constexpr unsigned kEntries = 0xC0, kEntrySize = 0x64, kEntryFlag = 0x1D;

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

void __cdecl update_camera_movers(float dt) {
    reinterpret_cast<void(__cdecl *)(float)>(g_update_camera_movers)(dt);
    if (!players::split_screen() || more_players::extra_player_count() == 0) return;

    const int count = 2 + more_players::extra_player_count();
    float position[4][3], forward[4][3];
    for (int i = 0; i < count; ++i) {
        float sim_position[3], sim_forward[3];
        if (!more_players::player_car_state(i, sim_position, sim_forward)) return;
        to_render(sim_position, position[i]);
        to_render(sim_forward, forward[i]);
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
    if (first == 0 && second == 1) return;  // the game own two

    unsigned char *entries = reinterpret_cast<unsigned char *>(kStreamer) + kEntries;
    unsigned char own[2][kEntrySize];
    memcpy(own[0], entries, kEntrySize);
    memcpy(own[1], entries + kEntrySize, kEntrySize);
    const int chosen[2] = {first, second};
    for (int slot = 0; slot < 2; ++slot) {
        const int player = chosen[slot];
        if (player < 2) {
            memcpy(entries + slot * kEntrySize, own[player], kEntrySize);
            continue;
        }
        const float velocity[3] = {0, 0, 0};
        reinterpret_cast<void(__fastcall *)(void *, void *, int, const float *, const float *, const float *, int)>(kPredictStreamingPosition)(
            reinterpret_cast<void *>(kStreamer), nullptr, slot, position[player], velocity, forward[player], own[0][kEntryFlag]);
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
