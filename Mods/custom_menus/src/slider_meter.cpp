#include "slider_meter.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "nfsmw_util.h"
#include "texture_pack.h"

namespace custom_menus::slider_meter {

namespace {

constexpr uintptr_t kFengInstance = 0x91CADC;  // cFEng::mInstance
constexpr float kBarLength = 160.0f;           // the slider's range in pixels (cSlider's fRange; SetSliderValues)
constexpr float kBarThickness = 3.0f;
constexpr unsigned kColor = 0xFFFFFFFF;        // 0xAARRGGBB: no tint; the flat green picture NFSMW_METER (nfsmw_custom_menus.tpk) gives the colour
constexpr unsigned kFallbackColor = 0xFFA0FFA0; // without that picture the fill bar's own picture is tinted (it is a gold bar in a tall image: dark when squashed)
constexpr int kSlots = 16;
nfsmw::Logger *g_log = nullptr;

using FindObjectFn = void *(*)(const char *, unsigned);                 // FEngFindObject
using GetPairFn = void (*)(void *, float *, float *);                   // FEngGetTopLeft / FEngGetSize
using SetPairFn = void (*)(void *, float, float);                       // FEngSetTopLeft
using SetFloatFn = void (*)(void *, float);                             // FEngSetSizeX / SizeY / BottomRightU
using SetColorFn = void (*)(void *, unsigned);                          // FEngSetColor
using ObjectFn = void (*)(void *);                                      // FEngSetVisible / Invisible
using CloneFn = void *(__fastcall *)(void *, void *, bool);             // FEObject::Clone (vtable 1)
using AddObjectFn = void(__fastcall *)(void *, void *, void *);         // FEPackage::AddObject
using GroupAddObjectFn = void(__fastcall *)(void *, void *, void *);    // FEGroup::AddObject
using FindPackageFn = void *(__fastcall *)(void *, void *, const char *);  // cFEng::FindPackage

const auto FindObject = reinterpret_cast<FindObjectFn>(0x524850);
const auto GetTopLeft = reinterpret_cast<GetPairFn>(0x514FD0);
const auto GetSize = reinterpret_cast<GetPairFn>(0x515520);
const auto SetTopLeft = reinterpret_cast<SetPairFn>(0x524CC0);
const auto SetSizeX = reinterpret_cast<SetFloatFn>(0x51A150);
const auto SetSizeY = reinterpret_cast<SetFloatFn>(0x564C10);
const auto SetBottomRightU = reinterpret_cast<SetFloatFn>(0x564C60);
const auto SetColor = reinterpret_cast<SetColorFn>(0x5157E0);
const auto SetVisible = reinterpret_cast<ObjectFn>(0x514CC0);
const auto SetInvisible = reinterpret_cast<ObjectFn>(0x514C70);
const auto SetTextureHash = reinterpret_cast<void (*)(void *, unsigned)>(0x515B90);  // FEngSetTextureHash
const auto GetTextureInfo = reinterpret_cast<void *(*)(unsigned, int, int)>(0x503400);

template <typename T>
T &field(void *base, size_t offset) {
    return *reinterpret_cast<T *>(static_cast<char *>(base) + offset);
}

unsigned hash_of(const char *format, int slot) {
    char name[48];
    snprintf(name, sizeof(name), format, slot);
    return nfsmw::bstring_hash(name);
}

// The group that holds an object (an object's position is relative to its group, and the sliders are in a group of the layout that is
// moved: a meter put straight into the package ended up off the screen), searched from a list of siblings.
void *find_parent(void *first, void *target, void *parent, int depth = 0) {
    for (void *o = first; o; o = field<void *>(o, 4)) {
        if (o == target) return parent ? parent : nullptr;
        if (field<int>(o, 0x18) == 5 && depth < 8) {
            if (void *found = find_parent(field<void *>(o, 0x64), target, o, depth + 1)) return found;
        }
    }
    return nullptr;
}

// FEObject::Clone makes the object and its scripts, but leaves its data zeroed (invisible): the copy gets the data of the original, its
// name, and no render cache. Only for an image, which has no pointers in its data. (Clone reads the current script without a check.)
void *make_meter(void *package, void *fill, unsigned name) {
    if (field<int>(fill, 0x18) != 1 || !field<void *>(fill, 0x54)) return nullptr;
    void *copy = reinterpret_cast<CloneFn>((*reinterpret_cast<void ***>(fill))[1])(fill, nullptr, false);
    if (!copy) return nullptr;
    const unsigned size = field<unsigned>(fill, 0x30);
    if (field<unsigned>(copy, 0x30) == size && field<void *>(fill, 0x2C) && field<void *>(copy, 0x2C))
        memcpy(field<void *>(copy, 0x2C), field<void *>(fill, 0x2C), size);
    field<unsigned>(copy, 0x10) = name;
    field<void *>(copy, 0x58) = nullptr;
    field<unsigned>(copy, 0x1C) |= (1u << 25) | (1u << 24);  // dirty
    void *parent = find_parent(field<void *>(package, 0x4C), fill, nullptr);
    if (parent) reinterpret_cast<GroupAddObjectFn>(0x5B5420)(parent, nullptr, copy);  // FEGroup::AddObject
    else reinterpret_cast<AddObjectFn>(0x50DB90)(package, nullptr, copy);
    if (g_log)
        g_log->line("slider meter: made %p next to the fill bar %p (%s), picture %08x", copy, fill, parent ? "in its group" : "at the top level of the layout",
                    field<unsigned>(fill, 0x24));
    return copy;
}

bool g_shown[kSlots] = {};
bool g_flat = false;  // the flat picture is in the game
bool g_pack_tried = false;

// The meter's picture: a flat colour of our own, from the texture pack next to the .asi (loaded the first time a meter is made).
void use_flat_picture(void *meter) {
    if (!g_pack_tried) {
        g_pack_tried = true;
        char path[MAX_PATH];
        nfsmw::data_file("nfsmw_custom_menus.tpk", path, sizeof(path));
        if (g_log) nfsmw::load_texture_pack(path, *g_log);
    }
    static const unsigned kPicture = nfsmw::texture_hash("NFSMW_METER");
    g_flat = GetTextureInfo(kPicture, 0, 0) != nullptr;
    if (!g_flat) return;
    SetTextureHash(meter, kPicture);
    void *data = field<void *>(meter, 0x2C);
    if (data && field<unsigned>(meter, 0x30) >= 0x54) {  // the image's data: the picture's corners (u, v) at +0x44 and +0x4C
        float *uv = reinterpret_cast<float *>(static_cast<char *>(data) + 0x44);
        uv[0] = 0.0f; uv[1] = 0.0f; uv[2] = 1.0f; uv[3] = 1.0f;
    }
}
bool g_logged[kSlots] = {};

}  // namespace

void init(nfsmw::Logger *log) { g_log = log; }

void set(const char *package, int slot, float level) {
    if (!package || slot < 1 || slot > kSlots) return;
    void *meter = FindObject(package, hash_of("NFSMW_METER_%d", slot));
    if (!meter) {
        if (level < 0.0f) return;
        void *fill = FindObject(package, hash_of("FILLBAR_SLIDER_%d", slot));
        void *feng = *reinterpret_cast<void **>(kFengInstance);
        void *pkg = feng ? reinterpret_cast<FindPackageFn>(0x516AF0)(feng, nullptr, package) : nullptr;
        if (!fill || !pkg || !(meter = make_meter(pkg, fill, hash_of("NFSMW_METER_%d", slot)))) return;
        g_shown[slot - 1] = true;  // the copy of the fill bar is visible
        use_flat_picture(meter);
        SetColor(meter, g_flat ? kColor : kFallbackColor);
    }
    void *base = FindObject(package, hash_of("BASE_SLIDER_%d", slot));
    void *fill = FindObject(package, hash_of("FILLBAR_SLIDER_%d", slot));
    // A slider scrolled out of view is hidden by the screen (UIWidgetMenu::Reposition) and keeps the position it last had: its meter goes too.
    const bool slider_shown = base && fill && !(field<unsigned>(base, 0x1C) & 1) && !(field<unsigned>(fill, 0x1C) & 1);
    if (level < 0.0f || !slider_shown) {
        if (g_shown[slot - 1]) SetInvisible(meter);
        g_shown[slot - 1] = false;
        return;
    }
    float base_x = 0, base_y = 0, base_w = 0, base_h = 0, fill_x = 0, fill_y = 0, fill_w = 0, fill_h = 0;
    GetTopLeft(base, &base_x, &base_y);
    GetSize(base, &base_w, &base_h);
    GetTopLeft(fill, &fill_x, &fill_y);
    GetSize(fill, &fill_w, &fill_h);
    if (!g_logged[slot - 1] && g_log) {
        g_logged[slot - 1] = true;
        g_log->line("slider meter %d: base at (%.1f, %.1f) size (%.1f, %.1f), fill at (%.1f, %.1f) size (%.1f, %.1f)", slot, base_x, base_y, base_w,
                    base_h, fill_x, fill_y, fill_w, fill_h);
    }
    const float bottom = base_y + base_h + 2.0f;  // the fill image is taller than the line it draws; the rows are 24 apart
    const float length = kBarLength * (level > 1.0f ? 1.0f : level);
    SetSizeX(meter, length);
    SetSizeY(meter, kBarThickness);
    SetTopLeft(meter, base_x + 2.0f, bottom);
    if (!g_flat) SetBottomRightU(meter, length);
    if (!g_shown[slot - 1]) SetVisible(meter);
    g_shown[slot - 1] = true;
}

}  // namespace custom_menus::slider_meter
