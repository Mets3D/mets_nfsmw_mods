#include "players.h"

#include "bindings.h"
#include "controller_api.h"
#include "max_players.h"

namespace pad::players {

namespace {

namespace api = nfsmw::controller_api;

constexpr uintptr_t kIoModule = 0x920DE0;  // +8 devices (16 pointers), by port
constexpr int kMaxDevices = 16;

struct Player {
    volatile LONG source = api::kSourceNone;
    volatile LONG port = -1;
};
struct Players {
    Player player[nfsmw::kMaxPlayers];
    Players() {
        player[0].source = api::kDefault;
        player[0].port = 0;
    }
} g;

bool valid(int player) { return player >= 0 && player < nfsmw::kMaxPlayers; }

}  // namespace

int source(int player) { return valid(player) ? g.player[player].source : api::kSourceNone; }

int controller(int player) { return std::max(source(player), -1); }

int of_port(int port) {
    for (int p = 1; p < nfsmw::kMaxPlayers; ++p)
        if (port > 0 && g.player[p].port == port) return p;
    return -1;
}

int on_keyboard() {
    for (int p = 0; p < nfsmw::kMaxPlayers; ++p)
        if (g.player[p].source == api::kSourceKeyboard) return p;
    return -1;
}

float value(int player, int entry) {
    const int port = valid(player) && player > 0 ? g.player[player].port : -1;
    if (port <= 0 || port >= kMaxDevices || entry < 0 || entry >= bindings::kBindingCount) return 0.0f;
    void *device = field<void *>(reinterpret_cast<void *>(kIoModule), 8 + port * 4);
    return device ? field<float *>(device, 0x20)[entry] : 0.0f;
}

}  // namespace pad::players

// For other mods (common/controller_api.h).
extern "C" __declspec(dllexport) void mets_controller_support_player_device(int player, int source, int port) {
    using namespace pad;
    if (player < 0 || player >= nfsmw::kMaxPlayers) return;
    auto &entry = players::g.player[player];
    InterlockedExchange(&entry.port, port);
    if (InterlockedExchange(&entry.source, source) == source) return;
    if (source >= 0) log_line("player %d: XInput controller %d (as another mod says)", player + 1, source + 1);
    else log_line("player %d: %s (as another mod says)", player + 1, source == nfsmw::controller_api::kSourceKeyboard ? "the keyboard" : source == nfsmw::controller_api::kDefault ? "every device" : "no controller");
}
