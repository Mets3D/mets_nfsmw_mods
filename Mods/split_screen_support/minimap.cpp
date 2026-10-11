/*
 * One minimap for each player. The console split screen has a single minimap (HUD_Player1.fng, centred on player 1, player 2 as
 * a second arrow); HUD_Player2.fng has none. So that each player gets one centred on themselves:
 *   - when player 2's HUD is made, the FEng objects of player 1's minimap are cloned (FEObject::Clone) into HUD_Player2.fng and
 *     a second Minimap is built on the clones; the map picture, ring and backdrop move to the middle of the top half (player 1)
 *     and of the bottom half (player 2), the markers stay (the game places them at absolute positions around the map's centre);
 *   - the second Minimap is shown and updated with player 2's HUD (FEngHud::Update);
 *   - each minimap shows the other player as its second arrow (the game always shows the last player of its list);
 *   - the arrows have a colour per player;
 *   - the map pictures come from a streamer that keeps only the tiles of the minimap that asked last, and both ask every frame,
 *     so it is given both sets at once;
 *   - the widescreen shift of the first minimap is repeated on the second.
 * Object layouts are the PC version's, not the decomp's: see docs/split-screen.md.
 */
#include "minimap.h"

#include <cstring>

#include "hooks.h"
#include "split_screen_support.h"

namespace split_screen_support::minimap {

namespace {

constexpr int kHudSplit1 = 3, kHudSplit2 = 4;  // ePlayerHudType
constexpr uintptr_t kFengInstance = 0x91CADC;  // cFEng::mInstance
constexpr unsigned kMinimapSize = 0x338;
constexpr uintptr_t kMalloc = 0x7C66D0;  // the game's allocator (cdecl): FEngHud makes its Minimap with it
constexpr unsigned kDirty = (1u << 25) | (1u << 24);  // FF_Dirty | FF_DirtyTransform

// FEng colours are 0xAARRGGBB; the alpha stays as the game set it.
constexpr unsigned kColorPlayer1 = 0x00FF9040;  // orange
constexpr unsigned kColorPlayer2 = 0x002090FF;  // blue

template <typename T>
T &field(void *base, size_t offset) {
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

// Object layouts (speed.exe 1.3):
//   FEObject: next +4, name hash +0x10, type +0x18 (1 image, 5 group, 12 multi image), flags +0x1C, data size +0x30, data +0x2C
//   (FEObjData: colour +0, position +0x1C), current script +0x54, render cache +0x58; a group's first child +0x64;
//   FEPackage: first object +0x4C.
//   HudElement: object list +8 (circular; next +0, object +8). Minimap: map layout +0x38, own marker +0x90, other player's marker
//   +0x94, default position +0x9C (y +0xA0), map centre +0xB8 (y +0xBC).
void *next_of(void *o) { return field<void *>(o, 4); }
void *first_child(void *group) { return field<void *>(group, 0x64); }
bool is_group(void *o) { return field<int>(o, 0x18) == 5; }

float &position_y(void *o) { return field<float>(field<void *>(o, 0x2C), 0x20); }

void shift(void *o, float dy) {
    position_y(o) += dy;
    field<unsigned>(o, 0x1C) |= kDirty;
}

// ---- the game's functions
using FindPackageFn = void *(__fastcall *)(void *, void *, const char *);
using AddObjectFn = void(__fastcall *)(void *, void *, void *);
using CloneFn = void *(__fastcall *)(void *, void *, bool);
using DeletingDtorFn = void(__fastcall *)(void *, void *, unsigned);
using MinimapCtorFn = void *(__fastcall *)(void *, void *, const char *, int);
using MinimapDtorFn = void(__fastcall *)(void *, void *, unsigned);
using UpdateFn = void(__fastcall *)(void *, void *, void *);
using FeaturesFn = unsigned long long(__fastcall *)(void *, void *, void *);
using ToggleFn = void(__fastcall *)(void *, void *, unsigned long long);
using VisibleFn = bool(__fastcall *)(void *, void *);
using VehicleVectorsFn = void(__cdecl *)(float *, float *, void *);
using ElementArtFn = void(__fastcall *)(void *, void *, float *, float *, void *, bool);
using GetColorFn = unsigned(__cdecl *)(void *);
using SetColorFn = void(__cdecl *)(void *, unsigned);
using MallocFn = void *(__cdecl *)(size_t);

void *find_package(const char *name) {
    void *feng = *reinterpret_cast<void **>(kFengInstance);
    return feng ? reinterpret_cast<FindPackageFn>(0x516AF0)(feng, nullptr, name) : nullptr;
}

// Entry n of an object's virtual table (0 = deleting destructor, 1 = Clone for FEObject, 1 = GetSimable for IPlayer).
void *vslot(void *object, int n) { return (*reinterpret_cast<void ***>(object))[n]; }

// ---- state
void *g_hud1, *g_hud2;        // the FEngHud of each player
void *g_player1, *g_player2;  // their IPlayer
void *g_a, *g_b;              // player 1's Minimap (HUD_Player1.fng), player 2's
void *g_clones[16];           // the objects added to it
int g_clone_count = 0;
float g_pinned_y[16];         // where each shifted clone has to stay (0 = not pinned)
unsigned long long g_features = ~0ull;
float g_offset = 120.0f;

// The tiles each minimap wants (the streamer keeps what the last call asked for).
void *g_setup_for;
short g_tiles[2][8];
int g_tile_count[2];

void *(__fastcall *g_orig_hud)(void *, void *, int, const char *, void *, int);
void (__fastcall *g_orig_hud_dtor)(void *, void *);
void (__fastcall *g_orig_hud_update)(void *, void *, void *, float);
void *(__fastcall *g_orig_minimap)(void *, void *, const char *, int);
void (__fastcall *g_orig_minimap_update)(void *, void *, void *);
void (__fastcall *g_orig_setup)(void *, void *, void *);
void (__fastcall *g_orig_tiles)(void *, void *, short *, int);
void (__fastcall *g_orig_widescreen)(void *, void *, bool);
void (__fastcall *g_orig_other_marker)(void *, void *);

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
        g_log.line("minimap: object %08x has type %d, cloning it is not safe: player 2 gets no minimap", field<unsigned>(object, 0x10), type);
        return false;
    }
    if (!field<void *>(object, 0x54)) {
        g_log.line("minimap: object %08x has no script, cloning it is not safe: player 2 gets no minimap", field<unsigned>(object, 0x10));
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

void remove_clones() {
    void *package = find_package("HUD_Player2.fng");
    for (int i = 0; package && i < g_clone_count; ++i) {
        void *obj = g_clones[i];
        reinterpret_cast<AddObjectFn>(0x50DBB0)(package, nullptr, obj);  // FEPackage::RemoveObject
        reinterpret_cast<DeletingDtorFn>(vslot(obj, 0))(obj, nullptr, 1);
    }
    g_clone_count = 0;
}

// A player's own marker is at the map's centre (the game only sets that in AdjustForWidescreen, which is not always called).
void place_marker(void *minimap) {
    void *marker = field<void *>(minimap, 0x90);
    if (!marker) return;
    position_y(marker) = field<float>(minimap, 0xBC);
    field<unsigned>(marker, 0x1C) |= kDirty;
}

void build_second_minimap() {
    if (!g_a || !g_hud2) return;
    void *package_a = find_package("HUD_Player1.fng");
    void *package_b = find_package("HUD_Player2.fng");
    if (!package_a || !package_b) {
        g_log.line("minimap: HUD_Player1.fng / HUD_Player2.fng not found: player 2 gets no minimap");
        return;
    }
    void *set = static_cast<char *>(g_a) + 8;
    // The objects that hold the minimap: the top level objects of player 1's HUD that contain what the Minimap found.
    void *parts[16];
    int count = 0;
    for (void *o = field<void *>(package_a, 0x4C); o && count < 16; o = next_of(o))
        if (contains(o, set)) parts[count++] = o;
    if (count == 0) {
        g_log.line("minimap: no object of HUD_Player1.fng belongs to the minimap: player 2 gets none");
        return;
    }
    for (int i = 0; i < count; ++i)
        if (!can_clone(parts[i])) return;
    g_clone_count = 0;
    for (int i = 0; i < count; ++i) {
        void *copy = reinterpret_cast<CloneFn>(vslot(parts[i], 1))(parts[i], nullptr, false);
        if (!copy) continue;
        finish_clone(parts[i], copy);
        // The game puts the markers and icons at absolute positions around the map's centre, so the group that holds them stays
        // where it is; everything else (the map picture, the ring and backdrop around it) is placed by the layout and is moved.
        g_pinned_y[g_clone_count] = 0;
        if (!has_descendant(parts[i], field<void *>(g_a, 0x90))) {
            shift(copy, g_offset);
            shift(parts[i], -g_offset);
            g_pinned_y[g_clone_count] = position_y(copy);
        }
        reinterpret_cast<AddObjectFn>(0x50DB90)(package_b, nullptr, copy);  // FEPackage::AddObject
        g_clones[g_clone_count++] = copy;
    }
    // the first minimap has already read the position it had
    field<float>(g_a, 0xBC) -= g_offset;  // map centre y
    field<float>(g_a, 0xA0) -= g_offset;  // default position y

    void *memory = reinterpret_cast<MallocFn>(kMalloc)(kMinimapSize);
    if (!memory) return;
    g_b = reinterpret_cast<MinimapCtorFn>(0x59D500)(memory, nullptr, "HUD_Player2.fng", 1);
    g_features = ~0ull;
    place_marker(g_b);
    place_marker(g_a);
    g_log.line("minimap: player 2's minimap built from %d cloned objects", g_clone_count);
}

// A clone plays its scripts from the start, and the first thing they do is set the position they were made with: the shift is
// lost. The map picture is not affected (the minimap sets its position every frame); the rest is put back each frame.
void pin_clones() {
    void *layout = field<void *>(g_b, 0x38);
    for (int i = 0; i < g_clone_count; ++i) {
        void *clone = g_clones[i];
        if (g_pinned_y[i] != 0 && clone != layout && position_y(clone) != g_pinned_y[i]) shift(clone, g_pinned_y[i] - position_y(clone));
    }
}

void drop_second_minimap() {
    if (g_b) reinterpret_cast<MinimapDtorFn>(0x5A6520)(g_b, nullptr, 1);
    g_b = nullptr;
    remove_clones();
    g_tile_count[0] = g_tile_count[1] = 0;
}

// ---- hooks
void *__fastcall hook_hud(void *self, void *edx, int type, const char *package, void *player, int number) {
    if (type == kHudSplit1) g_a = nullptr;
    void *result = g_orig_hud(self, edx, type, package, player, number);
    if (type == kHudSplit1) {
        g_hud1 = self;
        g_player1 = player;
    } else if (type == kHudSplit2) {
        g_hud2 = self;
        g_player2 = player;
        build_second_minimap();
    }
    return result;
}

void __fastcall hook_hud_dtor(void *self, void *edx) {
    if (self == g_hud2) {
        drop_second_minimap();
        g_hud2 = nullptr;
    } else if (self == g_hud1) {
        g_hud1 = nullptr;
        g_a = nullptr;
    }
    g_orig_hud_dtor(self, edx);
}

void *__fastcall hook_minimap(void *self, void *edx, const char *package, int number) {
    void *result = g_orig_minimap(self, edx, package, number);
    if (package && strcmp(package, "HUD_Player1.fng") == 0) g_a = result;
    return result;
}

void __fastcall hook_hud_update(void *self, void *edx, void *player, float dt) {
    g_orig_hud_update(self, edx, player, dt);
    if (self != g_hud2 || !g_b) return;
    pin_clones();
    const unsigned long long features = reinterpret_cast<FeaturesFn>(0x57CA60)(self, nullptr, player);
    if (features != g_features) {
        g_features = features;
        reinterpret_cast<ToggleFn>(0x5793F0)(g_b, nullptr, features);
    }
    const bool visible = reinterpret_cast<VisibleFn>(0x564A00)(g_b, nullptr);
    if (features != 0 && visible) reinterpret_cast<UpdateFn>(0x59DB50)(g_b, nullptr, player);
}

void colour(void *marker, unsigned rgb) {
    if (!marker) return;
    const unsigned alpha = reinterpret_cast<GetColorFn>(0x51A180)(marker) & 0xFF000000u;
    reinterpret_cast<SetColorFn>(0x5157E0)(marker, alpha | rgb);
}

void __fastcall hook_minimap_update(void *self, void *edx, void *player) {
    g_orig_minimap_update(self, edx, player);
    if (!g_b || (self != g_a && self != g_b)) return;
    const bool first = self == g_a;
    colour(field<void *>(self, 0x90), first ? kColorPlayer1 : kColorPlayer2);
    colour(field<void *>(self, 0x94), first ? kColorPlayer2 : kColorPlayer1);
}

// Minimap::UpdatePlayer2Element: the second arrow is whatever the last entry of the player list is; it has to be the other player.
void __fastcall hook_other_marker(void *self, void *edx) {
    void *other = self == g_b ? g_player1 : self == g_a ? g_player2 : nullptr;
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

// The tile streamer unloads every tile that is not in the list. With both minimaps its list is the two lists together.
void __fastcall hook_tiles(void *self, void *edx, short *tiles, int count) {
    const int slot = (g_b && g_setup_for == g_a) ? 0 : (g_b && g_setup_for == g_b) ? 1 : -1;
    if (slot < 0 || count > 4) {
        if (count == 0) g_tile_count[0] = g_tile_count[1] = 0;
        g_orig_tiles(self, edx, tiles, count);
        return;
    }
    memcpy(g_tiles[slot], tiles, count * sizeof(short));
    g_tile_count[slot] = count;
    short both[8];
    int n = 0;
    for (int s = 0; s < 2; ++s)
        for (int i = 0; i < g_tile_count[s]; ++i) both[n++] = g_tiles[s][i];
    g_orig_tiles(self, edx, both, n);
}

void __fastcall hook_widescreen(void *self, void *edx, bool outwards) {
    g_orig_widescreen(self, edx, outwards);
    if (self == g_a && g_b) g_orig_widescreen(g_b, edx, outwards);
}

}  // namespace

void install() {
    g_offset = static_cast<float>(GetPrivateProfileIntA("hud", "minimap_offset", 120, g_ini));
    const nfsmw::Hook hooks[] = {
        {"minimap", "FEngHud::FEngHud", 0x5A6600, {0x6A, 0xFF, 0x68, 0xB6, 0x39, 0x87, 0x00}, true, nfsmw::detour(&hook_hud),
         nfsmw::orig(&g_orig_hud)},
        {"minimap", "FEngHud::~FEngHud", 0x5A0910, {0x6A, 0xFF, 0x68, 0x9A, 0x34, 0x87, 0x00}, true, nfsmw::detour(&hook_hud_dtor),
         nfsmw::orig(&g_orig_hud_dtor)},
        {"minimap", "FEngHud::Update", 0x58CA30, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A}, true, nfsmw::detour(&hook_hud_update),
         nfsmw::orig(&g_orig_hud_update)},
        {"minimap", "Minimap::Minimap", 0x59D500, {0x6A, 0xFF, 0x68, 0xF3, 0x2F, 0x87, 0x00}, true, nfsmw::detour(&hook_minimap),
         nfsmw::orig(&g_orig_minimap)},
        {"minimap", "Minimap::Update", 0x59DB50, {0x83, 0xEC, 0x10, 0x53, 0x56, 0x8B, 0xF1}, true, nfsmw::detour(&hook_minimap_update),
         nfsmw::orig(&g_orig_minimap_update)},
        {"minimap", "Minimap::UpdatePlayer2Element", 0x594EA0, {0x83, 0xEC, 0x10, 0x56, 0x8B, 0xF1, 0xE8}, true,
         nfsmw::detour(&hook_other_marker), nfsmw::orig(&g_orig_other_marker)},
        {"minimap", "Minimap::SetupMinimap", 0x579900, {0x81, 0xEC, 0xAC, 0x00, 0x00, 0x00, 0xA1}, true, nfsmw::detour(&hook_setup),
         nfsmw::orig(&g_orig_setup)},
        {"minimap", "ChoppedMiniMapManager::UncompressMaps", 0x5672F0, {0x51, 0x53, 0x55, 0x56, 0x8B, 0xE9, 0x8B}, true,
         nfsmw::detour(&hook_tiles), nfsmw::orig(&g_orig_tiles)},
        {"minimap", "Minimap::AdjustForWidescreen", 0x5678A0, {0x8A, 0x44, 0x24, 0x04, 0x84, 0xC0, 0x74}, true,
         nfsmw::detour(&hook_widescreen), nfsmw::orig(&g_orig_widescreen)},
    };
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), g_log, g_ini, "minimap");
}

}  // namespace split_screen_support::minimap
