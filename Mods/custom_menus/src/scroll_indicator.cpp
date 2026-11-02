// The game's own scroll bar (FEScrollBar, UIWidgetMenu +0x54) finds the objects scrollbar_Backing, _Handle and _Arrow_1 / _2 in the layout; the Options layouts have them,
// Track_Options.fng does not. So the bar here is two images of our own: copies of the first row's right-hand arrow (an image that is drawn, in the group of the rows), given the
// layout's plain texture (BASEPOLY) and put in the same group, so that the arrows' positions are in the bar's own coordinates. For an image, FEngGetCenter gives the position in
// the object's data as it is (+0x1C / +0x20: its centre, in its group). The track runs from the first row's arrow to the arrow of the last row in view (their places are read
// once, with the view at the top), the handle follows the view top.
#include "scroll_indicator.h"

#include <algorithm>
#include <cmath>

#include "fe_hint.h"

namespace custom_menus::scroll_indicator {

namespace {

namespace fe = nfsmw::fe_hint::fe;
using nfsmw::fe_hint::copy_object_state;

constexpr uintptr_t kFindPackage = 0x516AF0, kPackageAddObject = 0x50DB90, kGroupAddObject = 0x5B5420, kGetCenter = 0x524EE0, kGetSize = 0x515520,
                    kGetWidget = 0x588280, kGetWidgetIndex = 0x588D60, kSetTextureHash = 0x515B90, kFEng = 0x91CADC;
constexpr unsigned kScreenPackage = 0x10, kRowsEnd = 0x104, kView = 0x38, kShown = 0x10C;  // UIWidgetMenu: the package name, the next row's index, the first row in view, the rows shown
constexpr unsigned kRightArrow = 0x50;                                                      // FEToggleWidget: the arrow at the right
constexpr unsigned kUserParam = 0x28;                                                       // FEObject: the resolved resource (an image's texture)
constexpr unsigned kBasePoly = 0xC6AFDD7E;                                                  // the texture BASEPOLY (plain), FEHashUpper of its name
constexpr unsigned kTrackName = 0x4E4D5301, kHandleName = 0x4E4D5302;                      // the names (and GUIDs) of our images
constexpr float kGap = 12.0f, kWidth = 4.0f, kMinHandle = 12.0f;                           // FE units: from the arrow to the bar, its width, the handle's shortest

void *g_track = nullptr, *g_handle = nullptr;
float g_x = 0, g_top = 0, g_length = 0;  // the track, in the coordinates of the rows' group

template <typename T>
T &field(void *object, unsigned offset) { return *reinterpret_cast<T *>(static_cast<char *>(object) + offset); }

void *widget(void *screen, unsigned index) { return reinterpret_cast<void *(__attribute__((thiscall)) *)(void *, unsigned)>(kGetWidget)(screen, index); }
unsigned index_of(void *screen, void *w) { return reinterpret_cast<unsigned(__attribute__((thiscall)) *)(void *, void *)>(kGetWidgetIndex)(screen, w); }
void center(void *object, float &x, float &y) { reinterpret_cast<void (*)(void *, float *, float *)>(kGetCenter)(object, &x, &y); }
void size(void *object, float &w, float &h) { reinterpret_cast<void (*)(void *, float *, float *)>(kGetSize)(object, &w, &h); }

// The group that holds `target` (anywhere below `group`).
void *parent_in(void *group, void *target, int depth) {
    if (depth > 8) return nullptr;
    void *child = field<void *>(group, fe::kChildHead);
    for (unsigned i = 0, count = field<unsigned>(group, fe::kChildCount); child && i < count && i < 512; ++i, child = field<void *>(child, fe::kNext)) {
        if (child == target) return group;
        if (field<int>(child, fe::kType) == fe::kGroup)
            if (void *found = parent_in(child, target, depth + 1)) return found;
    }
    return nullptr;
}

// A copy of `source` named `name`, with the plain texture, in the group of `source` (or among the package's own objects when it is one of those).
void *copy_beside(void *pkg, void *source, unsigned name) {
    using CloneFn = void *(__fastcall *)(void *, void *, bool);
    void *copy = reinterpret_cast<CloneFn>((*reinterpret_cast<void ***>(source))[1])(source, nullptr, false);
    if (!copy) return nullptr;
    copy_object_state(source, copy);
    field<unsigned>(copy, fe::kNameHash) = name;
    field<unsigned>(copy, fe::kGuid) = name;
    field<unsigned>(copy, fe::kHandle) = field<unsigned>(source, fe::kHandle);
    field<unsigned>(copy, kUserParam) = field<unsigned>(source, kUserParam);
    reinterpret_cast<void (*)(void *, unsigned)>(kSetTextureHash)(copy, kBasePoly);
    for (void *top = field<void *>(pkg, fe::kPackageObjects); top; top = field<void *>(top, fe::kNext)) {
        if (top == source) {
            reinterpret_cast<void(__attribute__((thiscall)) *)(void *, void *)>(kPackageAddObject)(pkg, copy);
            return copy;
        }
        if (field<int>(top, fe::kType) != fe::kGroup) continue;
        if (void *group = parent_in(top, source, 0)) {
            reinterpret_cast<void(__attribute__((thiscall)) *)(void *, void *)>(kGroupAddObject)(group, copy);
            return copy;
        }
    }
    return nullptr;  // (not found: the copy is not used; it leaks, once)
}

// Colour (the data's four ints are blue, green, red, alpha), centre and size of an image of ours.
void put(void *image, int red, int green, int blue, int alpha, float x, float y, float width, float height) {
    void *data = field<void *>(image, fe::kData);
    if (!data) return;
    field<int>(data, 0x00) = blue;
    field<int>(data, 0x04) = green;
    field<int>(data, 0x08) = red;
    field<int>(data, 0x0C) = alpha;
    field<float>(data, 0x1C) = x;
    field<float>(data, 0x20) = y;
    field<float>(data, 0x38) = width;
    field<float>(data, 0x3C) = height;
    field<unsigned>(image, fe::kFlags) |= fe::kDirtyColor | fe::kDirtyTransform | fe::kDirty;
}

}  // namespace

void update(void *screen) {
    if (!g_track || !g_handle) return;
    const int total = field<int>(screen, kRowsEnd) - 1, shown = field<int>(screen, kShown);
    void *view = field<void *>(screen, kView);
    if (!view || total <= shown) {  // (nothing to scroll: both invisible)
        put(g_track, 0, 0, 0, 0, g_x, g_top, kWidth, 1);
        put(g_handle, 0, 0, 0, 0, g_x, g_top, kWidth, 1);
        return;
    }
    put(g_track, 255, 255, 255, 60, g_x, g_top + g_length / 2, kWidth, g_length);
    const float handle = std::max(g_length * static_cast<float>(shown) / static_cast<float>(total), kMinHandle);
    const float progress = std::min(std::max(static_cast<float>(index_of(screen, view) - 1) / static_cast<float>(total - shown), 0.0f), 1.0f);
    put(g_handle, 255, 255, 255, 230, g_x, g_top + progress * (g_length - handle) + handle / 2, kWidth, handle);
}

void add(void *screen, nfsmw::Logger &log) {
    const char *package = field<const char *>(screen, kScreenPackage);
    if (!package || g_track) return;
    const int total = field<int>(screen, kRowsEnd) - 1, shown = field<int>(screen, kShown);
    if (shown < 2 || total <= shown) return;
    void *first = widget(screen, 1), *last = widget(screen, shown);
    void *first_arrow = first ? field<void *>(first, kRightArrow) : nullptr, *last_arrow = last ? field<void *>(last, kRightArrow) : nullptr;
    void *feng = *reinterpret_cast<void **>(kFEng);
    void *pkg = feng ? reinterpret_cast<void *(__attribute__((thiscall)) *)(void *, const char *)>(kFindPackage)(feng, package) : nullptr;
    if (!first_arrow || !last_arrow || !pkg) {
        log.line("scroll bar: no arrows to put one next to in %s", package);
        return;
    }
    float x, first_y, last_y, unused, arrow_width, arrow_height;
    center(first_arrow, x, first_y);
    center(last_arrow, unused, last_y);
    size(first_arrow, arrow_width, arrow_height);
    const float pitch = (last_y - first_y) / static_cast<float>(shown - 1);
    g_x = x + std::fabs(arrow_width) / 2 + kGap;
    g_top = first_y - pitch / 2;
    g_length = pitch * static_cast<float>(shown);
    g_track = copy_beside(pkg, first_arrow, kTrackName);
    g_handle = copy_beside(pkg, first_arrow, kHandleName);
    if (!g_track || !g_handle) {
        g_track = g_handle = nullptr;
        log.line("scroll bar: the copies of the arrow of %s could not be made", package);
        return;
    }
    log.line("scroll bar: %d rows, %d shown; at x %.0f, y %.0f to %.0f (the arrows of rows 1 and %d at y %.0f and %.0f, %.0f wide)", total, shown, g_x, g_top, g_top + g_length,
             shown, first_y, last_y, arrow_width);
    update(screen);
}

void forget() { g_track = g_handle = nullptr; }

}  // namespace custom_menus::scroll_indicator
