/*
 * The minimaps of a split screen race. The console split screen has a single minimap (HUD_Player1.fng, centred on player 1, player 2 as a second arrow);
 * HUD_Player2.fng has none. hud_players.cpp gives every player a HUD like player 1's, with a Minimap of their own, and this part makes them work together:
 *   - a minimap that is placed in its HUD (place) has its map picture, ring and backdrop moved, and kept there (the layout's scripts put them back);
 *   - each minimap shows every other player: the second marker of the minimap is the first other player (the game always shows the last player of its
 *     list), the rest are copies of that marker (FEObject::Clone) placed by the minimap's own code (UpdateElementArt), each in the colour of its player,
 *     and the minimap's own marker has the colour of its player;
 *   - the map pictures come from a streamer that keeps only the tiles of the minimap that asked last, and all ask every frame, so it is given all the sets at
 *     once, and it has room for the tiles of every minimap (the game's 9 slots are two or three minimaps' worth; the manager is made bigger).
 * Object layouts are the PC version's, not the decomp's: see agent_docs/split-screen.md.
 */
#include "minimap.h"

#include <malloc.h>

#include <algorithm>
#include <cstring>

#include "events.h"
#include "fe_objects.h"
#include "hud_views.h"
#include "hooks.h"
#include "max_players.h"
#include "split_screen_support.h"
#include "streaming_log.h"

namespace split_screen_support::minimap {

namespace {

using namespace fe;

constexpr unsigned kDirty = fe::kFlagDirty;

// Object layouts (speed.exe 1.3):
//   FEObject: next +4, name hash +0x10, type +0x18 (1 image, 5 group, 12 multi image), flags +0x1C, data size +0x30, data +0x2C
//   (FEObjData: colour +0, position +0x1C), current script +0x54, render cache +0x58; a group's first child +0x64;
//   FEPackage: first object +0x4C.
//   HudElement: object list +8 (circular; next +0, object +8). Minimap: map layout +0x38, own marker +0x90, other player's marker
//   +0x94, default position +0x9C (y +0xA0), map centre +0xB8 (y +0xBC).
void shift(void *o, float dy) {
    position_y(o) += dy;
    field<unsigned>(o, 0x1C) |= kDirty;
}

// ---- the game's functions
using CloneFn = void *(__fastcall *)(void *, void *, bool);
using VehicleVectorsFn = void(__cdecl *)(float *, float *, void *);
using ElementArtFn = void(__fastcall *)(void *, void *, float *, float *, void *, bool);
using GetColorFn = unsigned(__cdecl *)(void *);
using SetColorFn = void(__cdecl *)(void *, unsigned);

// Entry n of an object's virtual table (0 = deleting destructor, 1 = Clone for FEObject, 1 = GetSimable for IPlayer).
void *vslot(void *object, int n) { return (*reinterpret_cast<void ***>(object))[n]; }

// ---- state
// The tiles each minimap wants (the streamer keeps what the last call asked for).
void *g_setup_for;
struct TileSet {
    void *minimap;
    short tiles[8];
    int count;
};
TileSet g_tile_sets[nfsmw::kMaxPlayers];
int g_tile_set_count = 0;

// ChoppedMiniMapManager (the global 0x91CEFC, made by FEManager::Init through 0x579780 before the mods load, 0x1B4 bytes): the number of slots at +4 and the
// slots (tile, memory, size: 0xC bytes) from +0x148, 9 of them. Each minimap asks for up to 4 tiles (a 2 x 2 piece of the map around its player), so
// there is room for 4 for each player the mods are made for: the first call that comes with the game's object moves it into a bigger one (every use of
// the manager reads the global).
constexpr uintptr_t kManagerGlobal = 0x91CEFC;
constexpr unsigned kManagerSlotCount = 4, kManagerSlots = 0x148, kSlotSize = 0xC, kGameSlots = 9;
constexpr int kTilesPerMinimap = 4, kSlots = nfsmw::kMaxPlayers * kTilesPerMinimap;
void *g_wide_manager = nullptr;

// The manager to ask: the game's, made bigger the first time (its tiles and their table stay as they are).
void *wide_manager(void *manager) {
    if (manager == g_wide_manager) return manager;
    void *&global = *reinterpret_cast<void **>(kManagerGlobal);
    if (manager != global || field<int>(manager, kManagerSlotCount) != static_cast<int>(kGameSlots)) return manager;
    const size_t game_size = kManagerSlots + kGameSlots * kSlotSize, size = kManagerSlots + kSlots * kSlotSize;
    void *wide = _aligned_malloc(size, 16);
    if (!wide) return manager;
    memcpy(wide, manager, game_size);
    memset(static_cast<char *>(wide) + game_size, 0, size - game_size);
    field<int>(wide, kManagerSlotCount) = kSlots;
    global = wide;
    g_wide_manager = wide;
    g_log.line("minimap: the tile manager has %d slots now (the game's %d)", kSlots, kGameSlots);
    return wide;
}

constexpr int kMaxOthers = 7;
constexpr int kMaxAiRacers = 15;  // the most racers a race has (16) less the player
struct Markers {
    void *minimap;
    unsigned own_rgb;
    int other_count;
    int clones_made_for;       // how many others the clones were made for (a failed try is not repeated)
    const char *package;       // the HUD's package (for the markers' clones)
    void *clones[kMaxOthers];  // the markers of the others but the first: copies of the second marker, in its group (the layout has none of the AI racers' markers)
    void *others[kMaxOthers];  // the other players (IPlayer): the first is shown with the minimap's second marker, the rest with its racer markers
    unsigned other_rgb[kMaxOthers];
    void *ai_clones[kMaxAiRacers];  // the markers of the opponents (the HUD of a split screen race has none): copies of the second marker, made as they are needed
    bool ai_clone_failed;
};

Markers g_markers[8];
int g_marker_count = 0;

Markers *markers_of(void *minimap) {
    for (int i = 0; i < g_marker_count; ++i)
        if (g_markers[i].minimap == minimap) return &g_markers[i];
    return nullptr;
}

void (__fastcall *g_orig_minimap_update)(void *, void *, void *);
void (__fastcall *g_orig_setup)(void *, void *, void *);
void (__fastcall *g_orig_tiles)(void *, void *, short *, int);
void (__fastcall *g_orig_other_marker)(void *, void *);
void (__fastcall *g_orig_ai_racers)(void *, void *);

// ---- cloning
bool in_set(void *object, void *set_head) {
    int guard = 0;
    for (void *n = field<void *>(set_head, 0); n && n != set_head && guard < 1024; n = field<void *>(n, 0), ++guard)
        if (field<void *>(n, 8) == object) return true;
    return false;
}

bool contains(void *object, void *set_head, int depth = 0) {
    if (in_set(object, set_head)) return true;
    if (!is_group(object) || depth > 8) return false;
    for (void *c = first_child(object); c; c = next_of(c))
        if (contains(c, set_head, depth + 1)) return true;
    return false;
}

bool has_descendant(void *object, void *target, int depth = 0) {
    if (object == target) return true;
    if (!is_group(object) || depth > 8) return false;
    for (void *c = first_child(object); c; c = next_of(c))
        if (has_descendant(c, target, depth + 1)) return true;
    return false;
}

// The game's Clone makes the object and its scripts but not its data (finish_clone does that), and only images, groups and multi
// images have no pointers in their data; scripts are needed because Clone reads the current one without a check.
bool can_clone(void *object, int depth = 0) {
    const int type = field<int>(object, 0x18);
    if (type != 1 && type != 5 && type != 12) {  // image, group, multi image: no pointers in their data
        g_log.line("minimap: object %08x has type %d, cloning it is not safe", field<unsigned>(object, 0x10), type);
        return false;
    }
    if (!field<void *>(object, 0x54)) {
        g_log.line("minimap: object %08x has no script, cloning it is not safe", field<unsigned>(object, 0x10));
        return false;
    }
    if (!is_group(object) || depth > 8) return true;
    for (void *c = first_child(object); c; c = next_of(c))
        if (!can_clone(c, depth + 1)) return false;
    return true;
}

// FEObject::Clone leaves the object's data zeroed (alpha 0: invisible) and a multi image's texture fields unset. The copy also has
// new GUIDs, may lack the name hash (packages keep only the hash) and must not inherit a render cache.
void finish_clone(void *original, void *copy, int depth = 0) {
    field<unsigned>(copy, 0x10) = field<unsigned>(original, 0x10);
    const unsigned size = field<unsigned>(original, 0x30);
    if (field<unsigned>(copy, 0x30) == size && field<void *>(original, 0x2C) && field<void *>(copy, 0x2C))
        memcpy(field<void *>(copy, 0x2C), field<void *>(original, 0x2C), size);
    if (field<int>(original, 0x18) == 12) memcpy(static_cast<char *>(copy) + 0x60, static_cast<char *>(original) + 0x60, 0x18);  // textures, texture flags
    field<void *>(copy, 0x58) = nullptr;
    field<unsigned>(copy, 0x1C) |= kDirty;
    if (!is_group(original) || depth > 8) return;
    void *o = first_child(original), *c = first_child(copy);
    for (; o && c; o = next_of(o), c = next_of(c)) finish_clone(o, c, depth + 1);
}

// A player's own marker is at the map's centre (the game only sets that in AdjustForWidescreen, which is not always called).
void place_marker(void *minimap) {
    void *marker = field<void *>(minimap, 0x90);
    if (!marker) return;
    position_y(marker) = field<float>(minimap, 0xBC);
    field<unsigned>(marker, 0x1C) |= kDirty;
}

// Minimaps placed by place(): the objects moved and where each has to stay (the position the shift gave it).
struct Placed {
    void *object;
    float y;
};
Placed g_placed[64];
int g_placed_count = 0;

// A clone of a marker is shown by the minimap's update only: with the HUD hidden (the own marker is hidden) it is hidden too.
void follow_marker_visibility() {
    for (int i = 0; i < g_marker_count; ++i) {
        void *own = field<void *>(g_markers[i].minimap, 0x90);
        if (!own || !(field<unsigned>(own, 0x1C) & 1)) continue;
        for (int k = 1; k < g_markers[i].other_count; ++k)
            if (g_markers[i].clones[k]) field<unsigned>(g_markers[i].clones[k], 0x1C) |= 1;
        for (void *art : g_markers[i].ai_clones)
            if (art) field<unsigned>(art, 0x1C) |= 1;
    }
}

void pin_placed() {
    follow_marker_visibility();
    for (int i = 0; i < g_placed_count; ++i)
        if (position_y(g_placed[i].object) != g_placed[i].y) shift(g_placed[i].object, g_placed[i].y - position_y(g_placed[i].object));
}

// The group that has `target` as a child (anywhere below `group`).
void *parent_of(void *group, void *target, int depth = 0) {
    if (depth > 8) return nullptr;
    for (void *c = first_child(group); c; c = next_of(c)) {
        if (c == target) return group;
        if (is_group(c))
            if (void *found = parent_of(c, target, depth + 1)) return found;
    }
    return nullptr;
}

// A copy of the minimap's marker of another player, in the same group (so the group's visibility is its).
void *clone_marker(Markers &m) {
    void *marker = field<void *>(m.minimap, 0x94);
    void *package = m.package ? find_package(m.package) : nullptr;
    if (!marker || !package || !can_clone(marker)) return nullptr;
    void *group = nullptr;
    for (void *o = field<void *>(package, 0x4C); o && !group; o = next_of(o))
        if (is_group(o)) group = parent_of(o, marker);
    if (!group) return nullptr;
    void *copy = reinterpret_cast<CloneFn>(vslot(marker, 1))(marker, nullptr, false);
    if (!copy) return nullptr;
    finish_clone(marker, copy);
    reinterpret_cast<void(__fastcall *)(void *, void *, void *)>(0x5B5420)(group, nullptr, copy);  // FEGroup::AddObject
    hud_views::classify_like(copy, marker);
    return copy;
}

// A minimap has one marker for another player; the others are copies of it.
void make_marker_clones(Markers &m) {
    if (m.clones_made_for == m.other_count) return;
    m.clones_made_for = m.other_count;
    for (int k = 1; k < m.other_count; ++k)
        if (!m.clones[k]) m.clones[k] = clone_marker(m);
}

// ---- hooks
void hud_destroy(void *) {
    g_placed_count = 0;
    g_tile_set_count = 0;  // the minimaps that are left ask again (they do every frame)
    g_marker_count = 0;
}

void colour(void *marker, unsigned rgb) {
    if (!marker) return;
    const unsigned alpha = reinterpret_cast<GetColorFn>(0x51A180)(marker) & 0xFF000000u;
    reinterpret_cast<SetColorFn>(0x5157E0)(marker, alpha | rgb);
}

// The opponents' markers (Minimap::UpdateAiRacerElements 0x57A110 does this for the HUDs that have the objects MMICON_AIRACER_<n>, which the split screen HUD has not; the game's
// own copy of the update shows none of them): a small copy of the other players' marker for each active racer of the game's list of AI racers (the array of vehicle lists at
// 0x92CD18, 0x60 bytes each, list 3: pointer to the vehicles at +4, their count at +0xC), in a colour of their own, moved by the minimap's own placing code.
constexpr uintptr_t kVehicleLists = 0x92CD18, kSetInvisible = 0x514C70;
constexpr unsigned kAiRacerList = 3, kAiRgb = 0x00E8E8E8;
constexpr float kAiScale = 0.75f;

// What an object of the minimap is like, for the log (the data: colour as 4 ints, position, size).
void describe(const char *what, void *object) {
    if (!object) {
        g_log.line("minimap: %s: none", what);
        return;
    }
    void *data = field<void *>(object, 0x2C);
    g_log.line("minimap: %s %p: name %08x, type %d, flags %08x, HUD part %d, colour %d %d %d %d, position (%.1f, %.1f), size (%.1f, %.1f)", what, object, hash_of(object), type_of(object),
               field<unsigned>(object, 0x1C), hud_views::part_of_object(object), data ? field<int>(data, 0) : -1, data ? field<int>(data, 4) : -1, data ? field<int>(data, 8) : -1,
               data ? field<int>(data, 0xC) : -1, data ? field<float>(data, 0x1C) : 0.f, data ? field<float>(data, 0x20) : 0.f, data ? field<float>(data, 0x38) : 0.f, data ? field<float>(data, 0x3C) : 0.f);
}

void update_ai_markers(void *minimap, Markers &m) {
    static unsigned tick = 0;
    const bool log = ++tick % 600 == 1;  // about every 10 seconds, for each minimap (the first log of a race is the first frame of it)
    if (log) g_log.line("minimap: opponents' markers of the minimap %p: map centre (%.1f, %.1f), visible %d", minimap, field<float>(minimap, 0xB8), field<float>(minimap, 0xBC), field<unsigned>(minimap, 0x90) ? 1 : 0);
    const unsigned char *list = reinterpret_cast<const unsigned char *>(kVehicleLists + kAiRacerList * 0x60);
    void *const *vehicles = *reinterpret_cast<void *const *const *>(list + 4);
    const int count = *reinterpret_cast<const int *>(list + 0xC);
    int shown = 0;
    for (int i = 0; vehicles && i < count && shown < kMaxAiRacers; ++i) {
        void *vehicle = vehicles[i];
        if (!vehicle || !reinterpret_cast<bool(__fastcall *)(void *, void *)>(vslot(vehicle, 0x88 / 4))(vehicle, nullptr)) continue;
        void *art = m.ai_clones[shown];
        if (!art && !m.ai_clone_failed) {
            art = m.ai_clones[shown] = clone_marker(m);
            if (!art) m.ai_clone_failed = true;  // (no second marker to copy yet, or one that cannot be copied: not tried again until the HUD is made again)
            else {
                float *size = &field<float>(field<void *>(art, 0x2C), 0x38);  // the width and height in FEng units (14.5 x 16.5 for the players' arrows)
                size[0] *= kAiScale;
                size[1] *= kAiScale;
                field<unsigned>(art, 0x1C) |= kDirty;
            }
        }
        if (!art) break;
        void *simable = reinterpret_cast<void *(__fastcall *)(void *, void *)>(vslot(vehicle, 1))(vehicle, nullptr);
        if (!simable) continue;
        float position[2] = {}, direction[2] = {};
        reinterpret_cast<VehicleVectorsFn>(0x579840)(position, direction, simable);
        reinterpret_cast<ElementArtFn>(0x5675D0)(minimap, nullptr, position, direction, art, false);
        colour(art, kAiRgb);
        if (log && shown < 3) {
            g_log.line("minimap: opponent %d (vehicle %p) at (%.1f, %.1f), heading (%.2f, %.2f)", shown, vehicle, position[0], position[1], direction[0], direction[1]);
            describe("its marker", art);
        }
        ++shown;
    }
    if (log) {
        g_log.line("minimap: %d racers in the list of AI racers, %d markers shown, clone failed %d", count, shown, m.ai_clone_failed);
        describe("the second player's marker", field<void *>(minimap, 0x94));
        describe("the first player's clone", m.clones[1]);
    }
    for (int k = shown; k < kMaxAiRacers; ++k)
        if (m.ai_clones[k]) reinterpret_cast<void(__cdecl *)(void *)>(kSetInvisible)(m.ai_clones[k]);
}

void __fastcall hook_minimap_update(void *self, void *edx, void *player) {
    g_orig_minimap_update(self, edx, player);
    Markers *m = markers_of(self);
    if (!m) return;
    make_marker_clones(*m);
    colour(field<void *>(self, 0x90), m->own_rgb);
    if (m->other_count > 0) colour(field<void *>(self, 0x94), m->other_rgb[0]);
    for (int k = 1; k < m->other_count && k <= 8; ++k) {
        void *art = m->clones[k];
        void *simable = reinterpret_cast<void *(__fastcall *)(void *, void *)>(vslot(m->others[k], 1))(m->others[k], nullptr);
        if (!art || !simable) continue;
        float position[2] = {}, direction[2] = {};
        reinterpret_cast<VehicleVectorsFn>(0x579840)(position, direction, simable);
        reinterpret_cast<ElementArtFn>(0x5675D0)(self, nullptr, position, direction, art, false);
        colour(art, m->other_rgb[k]);
    }
    update_ai_markers(self, *m);
}

// The game's update of the opponents' markers walks 8 objects the split screen HUD does not have (and more than 8 racers run past them): update_ai_markers does it.
void __fastcall hook_ai_racers(void *self, void *edx) {
    if (!markers_of(self)) g_orig_ai_racers(self, edx);
}

// Minimap::UpdatePlayer2Element: the second arrow is whatever the last entry of the player list is; it has to be another player.
void __fastcall hook_other_marker(void *self, void *edx) {
    const Markers *m = markers_of(self);
    void *other = m && m->other_count > 0 ? m->others[0] : nullptr;
    if (!other) {
        g_orig_other_marker(self, edx);
        return;
    }
    void *simable = reinterpret_cast<void *(__fastcall *)(void *, void *)>(vslot(other, 1))(other, nullptr);
    float position[2] = {}, direction[2] = {};
    reinterpret_cast<VehicleVectorsFn>(0x579840)(position, direction, simable);
    reinterpret_cast<ElementArtFn>(0x5675D0)(self, nullptr, position, direction, field<void *>(self, 0x94), false);
}

void __fastcall hook_setup(void *self, void *edx, void *player) {
    g_setup_for = self;
    g_orig_setup(self, edx, player);
    g_setup_for = nullptr;
}

// A tile is loaded by allocating its uncompressed size from the main heap (UncompressMaps 0x5672F0: bMalloc(size at +8 of the tile's chunk, flags 0x2000) and a decompress
// into it, then LoadEmbeddedChunks, which makes the texture pack) and the game does not look at whether the allocation worked: with the heap full a failed one crashed
// the game (a null TexturePack in AttachTextureTable 0x506090) when four minimaps wanted 13 tiles. A tile that is not loaded yet and would not fit (with 8 MB to spare for the
// texture packs' own objects) is left out of the list: its map stays black until there is room.
constexpr unsigned kTileChunks = 0x48, kChunkUncompressedSize = 8, kMarginBytes = 8 << 20;

int drop_tiles_that_do_not_fit(void *manager, short *tiles, int count) {
    // (bCountFreeMemory says 0 for the main heap, so the largest free block is what there is to go by: a tile after another one takes from it.)
    int free_bytes = streaming_log::pool_largest(0) - kMarginBytes;
    int kept = 0;
    for (int i = 0; i < count; ++i) {
        bool loaded = false;
        const int slots = field<int>(manager, kManagerSlotCount);
        for (int s = 0; s < slots && !loaded; ++s) {
            unsigned char *slot = static_cast<unsigned char *>(manager) + kManagerSlots + s * kSlotSize;
            loaded = *reinterpret_cast<int *>(slot) == tiles[i] && *reinterpret_cast<void **>(slot + 4) != nullptr;
        }
        if (!loaded) {
            void *chunk = tiles[i] >= 0 ? field<void *>(manager, kTileChunks + tiles[i] * 4) : nullptr;
            const int size = chunk ? field<int>(chunk, kChunkUncompressedSize) : 0;
            if (size > 0) {
                if (size > free_bytes) {
                    static int logged = 0;
                    if (logged++ < 10) g_log.line("minimap: tile %d (%d KB) does not fit the main heap (largest free block %d KB): its map stays black", tiles[i], size >> 10, (free_bytes + kMarginBytes) >> 10);
                    continue;
                }
                free_bytes -= size;
            }
        }
        tiles[kept++] = tiles[i];
    }
    return kept;
}

// The tile streamer unloads every tile that is not in the list. With several minimaps its list is all their lists together (each tile once, as many as it
// has slots).
void __fastcall hook_tiles(void *self, void *edx, short *tiles, int count) {
    self = wide_manager(self);
    if (!g_setup_for || count > kTilesPerMinimap || count < 0) {
        if (count == 0) g_tile_set_count = 0;
        g_orig_tiles(self, edx, tiles, count);
        return;
    }
    TileSet *set = nullptr;
    for (int i = 0; i < g_tile_set_count; ++i)
        if (g_tile_sets[i].minimap == g_setup_for) set = &g_tile_sets[i];
    if (!set && g_tile_set_count < nfsmw::kMaxPlayers) set = &g_tile_sets[g_tile_set_count++];
    if (!set) {
        g_orig_tiles(self, edx, tiles, count);
        return;
    }
    set->minimap = g_setup_for;
    memcpy(set->tiles, tiles, count * sizeof(short));
    set->count = count;
    const int slots = std::min(field<int>(self, kManagerSlotCount), kSlots);
    short all[kSlots];
    int n = 0, wanted = 0;
    for (int s = 0; s < g_tile_set_count; ++s)
        for (int i = 0; i < g_tile_sets[s].count; ++i) {
            bool seen = false;
            for (int k = 0; k < n; ++k) seen = seen || all[k] == g_tile_sets[s].tiles[i];
            if (seen) continue;
            ++wanted;
            if (n < slots) all[n++] = g_tile_sets[s].tiles[i];
        }
    static int logged_wanted = 0;
    if (wanted > logged_wanted) {  // the most tiles wanted at once so far
        logged_wanted = wanted;
        g_log.line("minimap: %d tiles wanted by %d minimaps at once, %d slots%s; the game's memory pools: %s", wanted, g_tile_set_count, slots, wanted > slots ? ": TOO FEW, some maps stay black" : "",
                   streaming_log::pools_text());
    }
    n = drop_tiles_that_do_not_fit(self, all, n);
    g_orig_tiles(self, edx, all, n);
}

}  // namespace

void place(void *minimap, const char *package, float dy) {
    void *package_object = find_package(package);
    if (!minimap || !package_object) return;
    void *set = static_cast<char *>(minimap) + 8;
    void *marker = field<void *>(minimap, 0x90);
    void *layout = field<void *>(minimap, 0x38);  // the map picture's group: the minimap sets its position every frame
    int moved = 0;
    for (void *o = field<void *>(package_object, 0x4C); o; o = next_of(o)) {
        if (!contains(o, set) || has_descendant(o, marker)) continue;
        shift(o, dy);
        ++moved;
        if (o != layout && g_placed_count < 64) g_placed[g_placed_count++] = {o, position_y(o)};
    }
    field<float>(minimap, 0xBC) += dy;  // map centre y
    field<float>(minimap, 0xA0) += dy;  // default position y
    place_marker(minimap);
    g_log.line("minimap: %d objects of %s moved by %.0f", moved, package, dy);
}

void for_each_object(void *minimap, void (*fn)(void *object)) {
    void *set = static_cast<char *>(minimap) + 8;
    int guard = 0;
    for (void *n = field<void *>(set, 0); n && n != set && guard < 1024; n = field<void *>(n, 0), ++guard)
        if (void *object = field<void *>(n, 8)) fn(object);
}

void dump_object(void *o, int depth) {
    const char *indent = depth == 0 ? "" : depth == 1 ? "  " : depth == 2 ? "    " : "      ";
    void *data = field<void *>(o, 0x2C);
    if (!data) return;
    g_log.line("dump: %s%08x type %d pos (%.0f, %.0f) size (%.2f, %.2f) flags %08x alpha %d", indent, field<unsigned>(o, 0x10), field<int>(o, 0x18), field<float>(data, 0x1C),
               field<float>(data, 0x20), field<float>(data, 0x38), field<float>(data, 0x3C), field<unsigned>(o, 0x1C), field<int>(data, 0xC));
    if (is_group(o) && depth < 4)
        for (void *c = first_child(o); c; c = next_of(c)) dump_object(c, depth + 1);
}

void dump_package(const char *package) {
    void *p = find_package(package);
    if (!p) return;
    g_log.line("dump: package %s", package);
    for (void *o = field<void *>(p, 0x4C); o; o = next_of(o)) dump_object(o, 0);
}

void set_markers(void *minimap, const char *package, unsigned own_rgb, const void *const *others, const unsigned *other_rgb, int count) {
    Markers *m = markers_of(minimap);
    if (!m && g_marker_count < 8) {
        m = &g_markers[g_marker_count++];
        *m = {};
    }
    if (!m) return;
    m->minimap = minimap;
    m->package = package;
    m->own_rgb = own_rgb;
    m->other_count = count < kMaxOthers ? count : kMaxOthers;
    for (int i = 0; i < m->other_count; ++i) {
        m->others[i] = const_cast<void *>(others[i]);
        m->other_rgb[i] = other_rgb[i];
    }
}

void install() {
    const nfsmw::Hook hooks[] = {
        {"minimap", "Minimap::Update", 0x59DB50, {0x83, 0xEC, 0x10, 0x53, 0x56, 0x8B, 0xF1}, true, nfsmw::detour(&hook_minimap_update),
         nfsmw::orig(&g_orig_minimap_update)},
        {"minimap", "Minimap::UpdatePlayer2Element", 0x594EA0, {0x83, 0xEC, 0x10, 0x56, 0x8B, 0xF1, 0xE8}, true,
         nfsmw::detour(&hook_other_marker), nfsmw::orig(&g_orig_other_marker)},
        {"minimap", "Minimap::UpdateAiRacerElements", 0x57A110, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83}, true, nfsmw::detour(&hook_ai_racers),
         nfsmw::orig(&g_orig_ai_racers)},
        {"minimap", "Minimap::SetupMinimap", 0x579900, {0x81, 0xEC, 0xAC, 0x00, 0x00, 0x00, 0xA1}, true, nfsmw::detour(&hook_setup),
         nfsmw::orig(&g_orig_setup)},
        {"minimap", "ChoppedMiniMapManager::UncompressMaps", 0x5672F0, {0x51, 0x53, 0x55, 0x56, 0x8B, 0xE9, 0x8B}, true,
         nfsmw::detour(&hook_tiles), nfsmw::orig(&g_orig_tiles)},
    };
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), g_log, nullptr, "minimap");
    events::on_frame(pin_placed);
    events::on_hud_destroy(&hud_destroy);
}

}  // namespace split_screen_support::minimap
