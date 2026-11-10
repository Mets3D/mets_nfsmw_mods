// The game's label (the group 0xE3FE27FE of Car_Select.fng) is a string ("Player 1", right justified, at most 105 font units wide) on a dark plate that is
// wider (149 units). A line wider than a string's MaxWidth is drawn squashed to it (cFEngRender::RenderString 0x5A4390 scales x only), so
// "Player 1 - DualSense Wireless Controller" in that string was drawn at a quarter of its width. The device's name gets a row of its own instead, below the
// player's, its text made smaller in both directions until it fits the width of the plate (only a name that is still too wide at kSmallest is squashed).
// The plate is hidden: the screen's other texts (TOP SPEED ...) have no plate, they are drawn twice, a black copy a little lower and to the right under the
// white one, and so are these. A child of the group that comes earlier is drawn over a later one [tested], so the game's own string stays the white text and the
// shadows are copies added after it (the device's white text before its shadow).
// The copies are put in the game's group, not in a group of their own: the group is hidden by its scripts (INIT chains to HIDE, alpha 0) until the package's
// message 0x2CF6C390 plays its fade in, and a package message goes to the objects its target list names by GUID, which a copy of the group is not in
// (it stayed invisible). What is in the group fades with it.
#include "car_select_label.h"

#include <algorithm>
#include <cstdio>

#include "fe_clone.h"
#include "fe_objects.h"
#include "nfsmw_util.h"
#include "split_screen_support.h"

namespace split_screen_support::car_select_label {

namespace {

using namespace fe;

constexpr uintptr_t kFindObject = 0x524850, kAssignText = 0x57E8C0, kFindFont = 0x585B50, kLineWidth = 0x586130, kGroupAddObject = 0x5B5420;
constexpr unsigned kLabel = 0xE3FE27FE;
constexpr unsigned kText = 0x64, kFormat = 0x6C, kMaxWidth = 0x74, kFont = 0x24;  // FEString: its characters, FESTRING_ flags, MaxWidth; FEObject: Handle
constexpr unsigned kX = 0x1C / 4, kY = 0x20 / 4, kSizeX = 0x38 / 4, kSizeY = 0x3C / 4;  // FEObjData, in floats
constexpr unsigned kJustifyRight = 2;
constexpr float kDeviceSize = 0.8f, kSmallest = 0.55f;  // the device's text against the player's: when it fits, and at least
constexpr float kShadowX = 1.0f, kShadowY = 2.0f;       // as the game's TOP SPEED shadow is placed

float *data_of(void *object) { return field<float *>(object, 0x2C); }

// The names of the copies (the game's own look for the label's strings must not take them for the game's).
unsigned player_hash() { return nfsmw::bstring_hash("METS_CAR_SELECT_PLAYER"); }
unsigned device_hash() { return nfsmw::bstring_hash("METS_CAR_SELECT_DEVICE"); }
unsigned device_shadow_hash() { return nfsmw::bstring_hash("METS_CAR_SELECT_DEVICE_SHADOW"); }
bool ours(void *object) {
    const unsigned hash = hash_of(object);
    return hash == player_hash() || hash == device_hash() || hash == device_shadow_hash();
}

void *child_named(void *group, unsigned hash) {
    for (void *c = first_child(group); c; c = next_of(c))
        if (hash_of(c) == hash) return c;
    return nullptr;
}

// The game's first child of the group of a type.
void *game_child(void *group, int type) {
    for (void *c = first_child(group); c; c = next_of(c))
        if (type_of(c) == type && !ours(c)) return c;
    return nullptr;
}

// FEWideString::operator=(const char *) makes room (FEPrintf cuts the text to the string's buffer, 14 characters here), then the flags FEPrintf sets: the text
// is new (0x400000) and is not the language string's (2).
void set_text(void *string, const char *text) {
    reinterpret_cast<void *(__fastcall *)(void *, void *, const char *)>(kAssignText)(static_cast<char *>(string) + kText, nullptr, text);
    field<unsigned>(string, 0x1C) |= kFlagDirty | 0x400002u;
}

// FEngFont::GetLineWidth(text, flags, max width, word wrap), in the font's units (the string's size is not in it).
float line_width(void *string) {
    void *font = reinterpret_cast<void *(__cdecl *)(unsigned)>(kFindFont)(field<unsigned>(string, kFont));
    const short *text = field<const short *>(string, kText);
    if (!font || !text) return 0.0f;
    return reinterpret_cast<float(__fastcall *)(void *, void *, const short *, unsigned, unsigned, bool)>(kLineWidth)(font, nullptr, text, 0, 0, false);
}

// A copy of a child of the group, in the group, `down` lower. FEObject's copy constructor reads the object's current script without a check.
void *copy_below(void *group, void *original, unsigned hash, float down) {
    if (!field<void *>(original, 0x54)) return nullptr;
    void *copy = reinterpret_cast<void *(__fastcall *)(void *, void *, bool)>((*reinterpret_cast<void ***>(original))[1])(original, nullptr, false);  // FEObject::Clone
    if (!copy) return nullptr;
    finish_clone(original, copy);
    field<unsigned>(copy, 0x10) = hash;
    data_of(copy)[kY] += down;
    reinterpret_cast<void(__fastcall *)(void *, void *, void *)>(kGroupAddObject)(group, nullptr, copy);
    return copy;
}

// A string drawn black, a little lower and to the right: the shadow of its white copy. (No script of the strings sets a colour.)
void make_shadow(void *string) {
    int *colour = field<int *>(string, 0x2C);  // b, g, r, a
    colour[0] = colour[1] = colour[2] = 0;
    data_of(string)[kX] += kShadowX;
    data_of(string)[kY] += kShadowY;
    field<unsigned>(string, 0x1C) |= kFlagDirty;
}

// The device's text ends where the player's does and may reach as far to the left as the plate does, less the margin it has on the right. Both the text and
// its shadow get the size. `player`: the string it is a copy of, whose size it starts from.
void fit(void *string, void *shadow, void *plate, void *player) {
    float *s = data_of(string), *p = data_of(plate);
    const float left = p[kX] - p[kSizeX] / 2, right = p[kX] + p[kSizeX] / 2;
    const float room = s[kX] - left - (right - s[kX]);
    const float width = line_width(string) * data_of(player)[kSizeX];
    if (!(field<unsigned>(string, kFormat) & kJustifyRight) || room <= 0.0f || width <= 0.0f) return;
    const float scale = std::clamp(room / width, kSmallest, kDeviceSize);
    for (void *o : {string, shadow}) {
        data_of(o)[kSizeX] = data_of(player)[kSizeX] * scale;
        data_of(o)[kSizeY] = data_of(player)[kSizeY] * scale;
        field<unsigned>(o, kMaxWidth) = static_cast<unsigned>(room / data_of(o)[kSizeX]);
        field<unsigned>(o, 0x1C) |= kFlagDirty;
    }
    g_log.line("Car Select: the device's name is %.0f wide for %.0f, drawn at %.0f%% of the player's text", width, room, scale * 100.0f);
}

}  // namespace

void show(const char *package_name, const char *player, const char *device) {
    void *group = reinterpret_cast<void *(*)(const char *, unsigned)>(kFindObject)(package_name, kLabel);
    if (!group || !is_group(group)) return;
    void *game_text = game_child(group, kTypeString), *plate = game_child(group, kTypeImage);
    if (!game_text || !plate) return;
    void *player_shadow = child_named(group, player_hash());
    void *device_text = child_named(group, device_hash()), *device_shadow = child_named(group, device_shadow_hash());
    if (!player_shadow) {  // (the package is made again with each screen: this is the first time for these objects)
        const float down = data_of(plate)[kSizeY];
        if (device[0]) {
            device_text = copy_below(group, game_text, device_hash(), down);
            device_shadow = device_text ? copy_below(group, game_text, device_shadow_hash(), down) : nullptr;
            if (device_shadow) make_shadow(device_shadow);
        }
        player_shadow = copy_below(group, game_text, player_hash(), 0.0f);
        if (player_shadow) make_shadow(player_shadow);
        field<unsigned>(plate, 0x1C) |= kFlagInvisible | kFlagDirty;
    }
    if (!player_shadow) {
        g_log.line("Car Select: no copy of the label's text (it has no script): the game's label, without the device");
        set_text(game_text, player);
        return;
    }
    set_text(game_text, player);
    set_text(player_shadow, player);
    if (device_text && device_shadow) {
        set_text(device_text, device);
        set_text(device_shadow, device);
        fit(device_text, device_shadow, plate, game_text);
    }
}

}  // namespace split_screen_support::car_select_label
