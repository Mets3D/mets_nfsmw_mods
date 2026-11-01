/*
 * The world map (the pause menu's map of a race, `WorldMap` 0x5614D0) of a split screen race. `WorldMap::AddPlayerCar` (0x530F40) puts one marker on it, the car of the first local
 * player (a `MapItem`, 0x38 bytes, around the FEImage 0xDD9EF5FF of WorldMapMain.fng), so only player 1 was there. After it runs the markers of the minimap are added: the first
 * player's marker takes their colour, and every other player gets a copy of it in theirs, then the opponents (the game's list of AI racers, as the minimap shows them: smaller
 * and white). Each is a `MapItem` of its own, made and linked in as AddPlayerCar does (the list of items at map +0x154: a circular list, the link at item +4, the FEObject at +0xC).
 */
#include "world_map.h"

#include "fe_clone.h"
#include "fe_objects.h"
#include "hooks.h"
#include "hud_players.h"
#include "max_players.h"
#include "more_players.h"
#include "players.h"
#include "split_screen_support.h"

namespace split_screen_support::world_map {

namespace {

using namespace fe;

constexpr uintptr_t kAddPlayerCar = 0x530F40, kGetVehicleVectors = 0x579840, kConvertPos = 0x511D90, kConvertRot = 0x511DF0, kMapItemConstructor = 0x52F500, kNew = 0x652AD0,
                    kFindObjectByHash = 0x5B8550, kGetColor = 0x51A180, kSetColor = 0x5157E0, kVehicleLists = 0x92CD18;
constexpr unsigned kIconHash = 0xDD9EF5FF, kMapPackage = 0x10, kMapItems = 0x154, kItemSize = 0x38;
constexpr int kPlayerCarItem = 1;  // eWorldMapItemType
constexpr unsigned kAiRacerList = 3, kAiRgb = 0x00E8E8E8;
constexpr float kAiScale = 0.75f;  // as on the minimap
constexpr int kMaxOpponents = 16;

void (__fastcall *g_orig_add_player_car)(void *, void *);

void colour(void *object, unsigned rgb) {
    const unsigned alpha = reinterpret_cast<unsigned(__cdecl *)(void *)>(kGetColor)(object) & 0xFF000000u;
    reinterpret_cast<void(__cdecl *)(void *, unsigned)>(kSetColor)(object, alpha | rgb);
}

void *virtual_entry(void *object, int n) { return (*reinterpret_cast<void ***>(object))[n]; }

// One more marker for a car (`AddPlayerCar` for the simable): the position on the map, the way the car faces, and the item linked in at the end of the map's list.
void add_item(void *map, void *icon, void *simable) {
    float position[2] = {}, direction[2] = {};
    reinterpret_cast<void(__cdecl *)(float *, float *, void *)>(kGetVehicleVectors)(position, direction, simable);
    float world[2] = {position[0], position[1]};
    reinterpret_cast<void(__fastcall *)(void *, void *, float *)>(kConvertPos)(map, nullptr, position);
    const float rotation = reinterpret_cast<float(__fastcall *)(void *, void *, float *)>(kConvertRot)(map, nullptr, direction);
    void *item = reinterpret_cast<void *(__cdecl *)(unsigned)>(kNew)(kItemSize);
    if (!item) return;
    reinterpret_cast<void *(__fastcall *)(void *, void *, int, void *, float *, float *, float, void *)>(kMapItemConstructor)(item, nullptr, kPlayerCarItem, icon, position, world, rotation,
                                                                                                                             nullptr);
    char *list = static_cast<char *>(map) + kMapItems;
    void **node = reinterpret_cast<void **>(static_cast<char *>(item) + 4);
    void **tail = *reinterpret_cast<void ***>(list + 4);
    *tail = node;
    *reinterpret_cast<void **>(list + 4) = node;
    node[0] = list;
    node[1] = tail;
}

// A copy of the first player's icon, in the colour given (and the size, as a factor).
void *make_icon(void *package, void *icon, unsigned rgb, float scale) {
    void *copy = clone_object(icon);
    if (!copy) return nullptr;
    if (!add_beside(package, icon, copy)) return nullptr;
    colour(copy, rgb);
    if (scale != 1.0f) {
        float *size = &field<float>(field<void *>(copy, 0x2C), 0x38);
        size[0] *= scale;
        size[1] *= scale;
    }
    field<unsigned>(copy, 0x1C) |= kFlagDirty;
    return copy;
}

void __fastcall hook_add_player_car(void *map, void *edx) {
    g_orig_add_player_car(map, edx);
    if (!players::split_screen()) return;
    const char *name = field<const char *>(map, kMapPackage);
    void *package = name ? find_package(name) : nullptr;
    void *icon = package ? reinterpret_cast<void *(__fastcall *)(void *, void *, unsigned)>(kFindObjectByHash)(package, nullptr, kIconHash) : nullptr;
    if (!icon || type_of(icon) != kTypeImage) {
        g_log.line("world map: the player's icon was not found: only the first player is shown");
        return;
    }
    colour(icon, hud_players::player_colour(0));
    int players = 1, opponents = 0, failed = 0;
    for (int i = 1; i < nfsmw::kMaxPlayers; ++i) {
        void *player = more_players::game_player(i);
        void *simable = player ? reinterpret_cast<void *(__fastcall *)(void *, void *)>(virtual_entry(player, 1))(player, nullptr) : nullptr;
        if (!simable) continue;
        void *copy = make_icon(package, icon, hud_players::player_colour(i), 1.0f);
        if (!copy) {
            ++failed;
            continue;
        }
        add_item(map, copy, simable);
        ++players;
    }
    const unsigned char *list = reinterpret_cast<const unsigned char *>(kVehicleLists + kAiRacerList * 0x60);
    void *const *vehicles = *reinterpret_cast<void *const *const *>(list + 4);
    const int count = *reinterpret_cast<const int *>(list + 0xC);
    for (int i = 0; vehicles && i < count && opponents < kMaxOpponents; ++i) {
        void *vehicle = vehicles[i];
        if (!vehicle || !reinterpret_cast<bool(__fastcall *)(void *, void *)>(virtual_entry(vehicle, 0x88 / 4))(vehicle, nullptr)) continue;
        void *simable = reinterpret_cast<void *(__fastcall *)(void *, void *)>(virtual_entry(vehicle, 1))(vehicle, nullptr);
        void *copy = simable ? make_icon(package, icon, kAiRgb, kAiScale) : nullptr;
        if (!copy) {
            ++failed;
            break;
        }
        add_item(map, copy, simable);
        ++opponents;
    }
    g_log.line("world map: markers for %d players and %d opponents%s", players, opponents, failed ? " (some could not be made: see the log above)" : "");
}

}  // namespace

void install() {
    if (!ini_flag("options", "show_all_racers_on_world_map", true)) return;
    const nfsmw::Hook hooks[] = {
        {"mapplayercar", "WorldMap::AddPlayerCar", kAddPlayerCar, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A}, false, nfsmw::detour(&hook_add_player_car),
         nfsmw::orig(&g_orig_add_player_car)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, g_ini, "world map");
}

}  // namespace split_screen_support::world_map
