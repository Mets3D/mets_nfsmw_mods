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

#include <d3d9.h>

#include "events.h"
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
float &position_x(void *o) { return field<float>(field<void *>(o, 0x2C), 0x1C); }

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
void *g_originals[16];        // what they are copies of
void *g_right_split = nullptr, *g_right_main = nullptr;  // player 1's two right-hand groups (see follow_right_group)
void *g_left = nullptr;                                  // player 1's left-hand group of the layout (1603009e)
float g_right_split_native = 0;
int g_clone_count = 0;
float g_pinned_y[16];         // where each shifted clone has to stay (0 = not pinned)
struct PinnedX {              // a clone (or a child of one) and the object it copies: it stays where that one is sideways
    void *clone;
    void *original;
} g_pinned_x[64];
int g_pinned_x_count = 0;
int g_hud_frames = 0;       // updates of the second HUD so far (restore_layout records the layout after kSettleFrames)
constexpr int kSettleFrames = 120;
unsigned long long g_features = ~0ull;
constexpr float kMinimapOffset = 120.0f;

constexpr unsigned kMapChildren[5] = {60, 64, 68, 72, 144};  // the Minimap's objects the widescreen fix moves, besides its own x (+184)

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

void settle_layout();
void restore_layout();
void forget_layout();

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

void follow_x(void *original, void *clone, int depth = 0);
void find_right_groups();
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
            shift(copy, kMinimapOffset);
            shift(parts[i], -kMinimapOffset);
            g_pinned_y[g_clone_count] = position_y(copy);
        }
        reinterpret_cast<AddObjectFn>(0x50DB90)(package_b, nullptr, copy);  // FEPackage::AddObject
        g_originals[g_clone_count] = parts[i];
        g_clones[g_clone_count++] = copy;
    }
    // the first minimap has already read the position it had
    field<float>(g_a, 0xBC) -= kMinimapOffset;  // map centre y
    field<float>(g_a, 0xA0) -= kMinimapOffset;  // default position y

    void *memory = reinterpret_cast<MallocFn>(kMalloc)(kMinimapSize);
    if (!memory) return;
    g_b = reinterpret_cast<MinimapCtorFn>(0x59D500)(memory, nullptr, "HUD_Player2.fng", 1);
    g_features = ~0ull;
    place_marker(g_b);
    place_marker(g_a);
    find_right_groups();
    g_pinned_x_count = 0;
    for (int i = 0; i < g_clone_count; ++i)
        if (g_pinned_y[i] != 0 && g_clones[i] != field<void *>(g_b, 0x38)) follow_x(g_originals[i], g_clones[i]);
    g_log.line("minimap: player 2's minimap built from %d cloned objects", g_clone_count);
}

// A clone plays its scripts from the start, and the first thing they do is set the position they were made with: the shift is
// lost. The map picture is not affected (the minimap sets its position every frame); the rest is put back each frame.
void pin_x();
void pin_clones() {
    void *layout = field<void *>(g_b, 0x38);
    for (int i = 0; i < g_clone_count; ++i) {
        void *clone = g_clones[i];
        if (g_pinned_y[i] != 0 && clone != layout && position_y(clone) != g_pinned_y[i]) shift(clone, g_pinned_y[i] - position_y(clone));
    }
    pin_x();
}

// The widescreen layout moves the original's objects sideways (the game's WIDESCREENMODE message, in HUD_Player1.fng, which
// the clones do not get), and a clone's scripts put it back where it was made: the black circle of the backdrop stayed where a 4:3
// screen has the minimap. So the shifted clones and their children follow the originals' x.
void follow_x(void *original, void *clone, int depth) {
    if (g_pinned_x_count >= 64) return;
    g_pinned_x[g_pinned_x_count++] = {clone, original};
    if (!is_group(original) || depth > 8) return;
    void *o = first_child(original), *c = first_child(clone);
    for (; o && c; o = next_of(o), c = next_of(c)) follow_x(o, c, depth + 1);
}

// HUD_Player1.fng has two groups for the right-hand HUD (speedometer, completion, position), 2fe4ff9c and 5d0101f1, both at x 209;
// HUD_Player2.fng only the second. The widescreen fix moves 5d0101f1 by the widescreen offset (to 273 at 16:10), while the
// layout's own widescreen message moves 2fe4ff9c by 120 (to 329: clipped by the screen's edge at 16:10, where the offset is 64).
// When 5d0101f1 has been moved, 2fe4ff9c is put where it is.
void find_right_groups() {
    g_right_split = g_right_main = g_left = nullptr;
    void *package = find_package("HUD_Player1.fng");
    for (void *o = package ? field<void *>(package, 0x4C) : nullptr; o; o = next_of(o)) {
        if (field<unsigned>(o, 0x10) == 0x2FE4FF9C) g_right_split = o;
        if (field<unsigned>(o, 0x10) == 0x5D0101F1) g_right_main = o;
        if (field<unsigned>(o, 0x10) == 0x1603009E) g_left = o;
    }
    if (g_right_split) g_right_split_native = position_x(g_right_split);
}

void follow_right_group() {
    if (!g_right_split || !g_right_main) return;
    const float x = position_x(g_right_main);
    if (x == g_right_split_native || position_x(g_right_split) == x) return;
    position_x(g_right_split) = x;
    field<unsigned>(g_right_split, 0x1C) |= kDirty;
}

void pin_x() {
    follow_right_group();
    for (int i = 0; i < g_pinned_x_count; ++i) {
        void *clone = g_pinned_x[i].clone;
        const float x = position_x(g_pinned_x[i].original);
        if (position_x(clone) != x) {
            position_x(clone) = x;
            field<unsigned>(clone, 0x1C) |= kDirty;
        }
    }
}

void drop_second_minimap() {
    if (g_b) reinterpret_cast<MinimapDtorFn>(0x5A6520)(g_b, nullptr, 1);
    g_b = nullptr;
    remove_clones();
    g_tile_count[0] = g_tile_count[1] = 0;
    g_pinned_x_count = 0;
    g_right_split = g_right_main = g_left = nullptr;
    forget_layout();
    g_hud_frames = 0;
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
    if (package && strcmp(package, "HUD_Player1.fng") == 0) {
        g_a = result;
    }
    return result;
}

// DetermineHudFeatures (0x57CA60) hides all HUD on pause (FEManager::IsPaused(), Pause_Main.fng pushed; no per-player
// case) via HudElement::Toggle (0x5793F0) on whatever the hud owns. g_b isn't owned by FEngHud, so this mirrors that
// call for it - from events::on_frame rather than FEngHud::Update, which does not run at all while paused.
void update_visibility() {
    if (!g_hud2 || !g_b) return;
    const unsigned long long features = reinterpret_cast<FeaturesFn>(0x57CA60)(g_hud2, nullptr, g_player2);
    if (features != g_features) {
        g_features = features;
        reinterpret_cast<ToggleFn>(0x5793F0)(g_b, nullptr, features);
    }
}

void __fastcall hook_hud_update(void *self, void *edx, void *player, float dt) {
    g_orig_hud_update(self, edx, player, dt);
    if (self != g_hud2 || !g_b) return;
    if (g_hud_frames >= kSettleFrames) restore_layout();
    pin_clones();
    if (++g_hud_frames == kSettleFrames) settle_layout();
    const bool visible = reinterpret_cast<VisibleFn>(0x564A00)(g_b, nullptr);
    if (g_features != 0 && visible) reinterpret_cast<UpdateFn>(0x59DB50)(g_b, nullptr, player);
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

float *child_x(void *map, int i) {
    void *child = field<void *>(map, kMapChildren[i]);
    void *data = child ? field<void *>(child, 44) : nullptr;
    return data ? &field<float>(data, 28) : nullptr;
}

// The widescreen layout is applied to the first minimap (the game's own call, and the widescreen fix's); the second one gets the same.
void __fastcall hook_widescreen(void *self, void *edx, bool outwards) {
    g_orig_widescreen(self, edx, outwards);
    if (self == g_a && g_b) g_orig_widescreen(g_b, edx, outwards);
}

// The widescreen fix moves the HUD's left and right sides by an offset, and when the video settings are applied it works the offset
// out again, from a size the game is not drawing at (the window's, not the game's: 16:9 for a 16:10 picture), and moves what it
// knows (player 1's groups, its minimap) a second time. So once the layout has settled, the x of what it moves is recorded, and
// whenever it changes it is put back, unless the size the game really draws at has changed: then the layout settles again.
struct Tracked {
    float *value;
    void *object;  // to mark dirty; null for a member of the Minimap
    float settled;
};
Tracked g_tracked[64];
int g_tracked_count = 0;
unsigned g_settled_width = 0, g_settled_height = 0;
bool g_put_back_last = false;

void track(float *value, void *object) {
    if (value && g_tracked_count < 64) g_tracked[g_tracked_count++] = {value, object, *value};
}

void forget_layout() { g_tracked_count = 0; }

bool screen_size(unsigned &width, unsigned &height) {
    auto *device = *reinterpret_cast<IDirect3DDevice9 **>(0x982BDC);
    IDirect3DSurface9 *back = nullptr;
    if (!device || FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) || !back) return false;
    D3DSURFACE_DESC desc;
    back->GetDesc(&desc);
    back->Release();
    width = desc.Width;
    height = desc.Height;
    return true;
}

void settle_layout() {
    if (!g_right_main || !g_a || !g_b) return;
    g_tracked_count = 0;
    screen_size(g_settled_width, g_settled_height);
    for (void *map : {g_a, g_b}) {
        track(&field<float>(map, 184), nullptr);
        for (int i = 0; i < 5; ++i) {
            void *child = field<void *>(map, kMapChildren[i]);
            track(child ? child_x(map, i) : nullptr, child);
        }
    }
    track(&position_x(g_right_main), g_right_main);
    if (g_left) track(&position_x(g_left), g_left);
    // the objects the second minimap copies (backdrop, ring, map picture): the layout's scripts move them too
    for (int i = 0; i < g_pinned_x_count; ++i) track(&position_x(g_pinned_x[i].original), g_pinned_x[i].original);
}

bool screen_changed() {
    unsigned width = 0, height = 0;
    return screen_size(width, height) && (width != g_settled_width || height != g_settled_height);
}

void restore_layout() {
    int put_back = 0;
    for (int i = 0; i < g_tracked_count; ++i) {
        Tracked &t = g_tracked[i];
        if (*t.value == t.settled) continue;
        if (!put_back && screen_changed()) {  // a real change of the picture: this is the new layout
            g_log.line("hud: the screen size changed, the layout settles again");
            g_hud_frames = 0;
            forget_layout();
            return;
        }
        *t.value = t.settled;
        if (t.object) field<unsigned>(t.object, 0x1C) |= kDirty;
        ++put_back;
    }
    if (put_back && !g_put_back_last) g_log.line("hud: %d positions had been moved by another mod's layout; put back", put_back);
    g_put_back_last = put_back != 0;
}

}  // namespace

void install() {
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
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), g_log, nullptr, "minimap");
    events::on_frame(update_visibility);
}

}  // namespace split_screen_support::minimap
