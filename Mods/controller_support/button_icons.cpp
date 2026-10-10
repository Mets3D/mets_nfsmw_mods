// The game draws its button prompts as images of keyboard keys (PC_KEY_ENTER, ...). Each frame, every such image on
// screen is switched to the controller picture (XBOX_KEY_ENTER, or PS_KEY_ENTER) while the last input was from a
// controller, and back to the key picture otherwise. The pictures are in our own texture pack, built from the PNGs in
// controller_support/buttons/; the game's per-frame UI update (FEngine::Update) is the hook that finds the images.
//
// The same idea, and the pictures, are from ThirteenAG's WidescreenFixesPack (MIT License, see buttons/LICENSE).
#include "button_icons.h"

#include "hooks.h"
#include "input.h"
#include "texture_pack.h"

namespace pad::button_icons {

namespace {

enum class Style { kOff, kXbox, kPlayStation };
Style g_style = Style::kXbox;

// The game's name for a picture, and its two controller versions.
struct Mapping {
    const char *pc, *xbox, *playstation;
};
const Mapping kMappings[] = {
    {"L1", "XBOX_L1", "PS_L1"},
    {"PC_KEY_Q", "XBOX_KEY_Q", "PS_KEY_Q"},
    {"PC_GREATERTHAN_LESSTHAN", "XBOX_GREATERTHAN_LESSTHAN", "PS_GREATERTHAN_LESSTHAN"},
    {"R1", "XBOX_R1", "PS_R1"},
    {"PC_KEY_ENTER", "XBOX_KEY_ENTER", "PS_KEY_ENTER"},
    {"PC_KEY_ESC_FRENCH", "XBOX_KEY_ESC_FRENCH", "PS_KEY_ESC_FRENCH"},
    {"PC_KEY_ESC", "XBOX_KEY_ESC", "PS_KEY_ESC"},
    {"PC_KEY_1", "XBOX_KEY_1", "PS_KEY_1"},
    {"PC_KEY_2", "XBOX_KEY_2", "PS_KEY_2"},
    {"PC_KEY_3", "XBOX_KEY_3", "PS_KEY_3"},
    {"PC_KEY_4", "XBOX_KEY_4", "PS_KEY_4"},
    {"PC_ARROW_KEYS_UP_DOWN", "XBOX_ARROW_KEYS_UP_DOWN", "PS_ARROW_KEYS_UP_DOWN"},
    {"PC_LOAD", "XBOX_LOAD", "PS_LOAD"},
    {"PC_ARROW_KEYS", "XBOX_ARROW_KEYS", "PS_ARROW_KEYS"},
};
constexpr int kMappingCount = sizeof(kMappings) / sizeof(kMappings[0]);

struct Hashes {
    unsigned pc, xbox, playstation;
};
Hashes g_hashes[kMappingCount];

// An FEng object: type at +0x18, flags at +0x1C, the texture (for an image) at +0x24.
constexpr unsigned kType = 0x18, kFlags = 0x1C, kTexture = 0x24;
constexpr int kImage = 1;
constexpr unsigned kTextureChanged = 0x2400000;  // flags the engine needs to pick up a new texture

// The engine's callback for ForAllObjects: an object whose first member points to {destructor, function}. The function
// is called once per object, with the object as its argument.
struct CallbackVtable {
    void *destructor;
    void *function;
};

char __stdcall visit_object(void *object) {
    if (field<int>(object, kType) != kImage) return 1;
    unsigned &texture = field<unsigned>(object, kTexture);
    const bool pad = input::last_input_was_pad();
    for (const Hashes &h : g_hashes) {
        if (texture != h.pc && texture != h.xbox && texture != h.playstation) continue;
        const unsigned wanted = !pad ? h.pc : g_style == Style::kPlayStation ? h.playstation : h.xbox;
        if (texture != wanted) {
            texture = wanted;
            field<unsigned>(object, kFlags) |= kTextureChanged;
        }
        break;
    }
    return 1;
}

using ForAllObjectsFn = char(FASTCALL *)(void *package, void *edx, void *callback);
const auto ForAllObjects = reinterpret_cast<ForAllObjectsFn>(0x5B1560);  // FEPackage::ForAllObjects

// FEngine::Update(int ticks, unsigned lock): the engine's per-frame work. The loaded packages are a list at +0xE0
// (next package at +4).
using UpdateFn = void(FASTCALL *)(void *, void *, int, unsigned);
UpdateFn g_orig_update;
bool g_pack_tried = false;
bool g_pack_loaded = false;

void FASTCALL hook_update(void *engine, void *edx, int ticks, unsigned lock) {
    if (!g_pack_tried) {
        g_pack_tried = true;
        char path[MAX_PATH];
        nfsmw::data_file("nfsmw_controller_buttons.tpk", path, sizeof(path));
        g_pack_loaded = nfsmw::load_texture_pack(path, g_log);
        if (!g_pack_loaded) log_line("button icons: the texture pack is missing; the pictures stay as the game has them");
    }
    if (g_pack_loaded) {
        static const CallbackVtable vtable = {nullptr, reinterpret_cast<void *>(&visit_object)};
        const CallbackVtable *callback = &vtable;  // the callback object: its first member is the table
        for (void *package = field<void *>(engine, 0xE0); package;) {
            ForAllObjects(package, nullptr, &callback);
            void *next = field<void *>(package, 4);
            if (next == package) break;
            package = next;
        }
    }
    g_orig_update(engine, edx, ticks, lock);
}

}  // namespace

void load_settings() {
    char text[32] = {};
    GetPrivateProfileStringA("buttons", "icons", "xbox", text, sizeof(text), g_ini);
    if (!_stricmp(text, "off") || !_stricmp(text, "0") || !_stricmp(text, "none")) g_style = Style::kOff;
    else if (!_stricmp(text, "playstation") || !_stricmp(text, "ps")) g_style = Style::kPlayStation;
    else g_style = Style::kXbox;
    log_line("button icons: %s", g_style == Style::kOff ? "off" : g_style == Style::kXbox ? "xbox" : "playstation");
}

void install_hooks() {
    if (g_style == Style::kOff) return;
    for (int i = 0; i < kMappingCount; ++i)
        g_hashes[i] = {nfsmw::texture_hash(kMappings[i].pc), nfsmw::texture_hash(kMappings[i].xbox),
                       nfsmw::texture_hash(kMappings[i].playstation)};
    const nfsmw::Hook hooks[] = {
        {"icons", "FEngine::Update", 0x5C53C0, {0x83, 0xEC, 0x0C, 0x56, 0x8B, 0xF1, 0x8A}, false,
         nfsmw::detour(&hook_update), nfsmw::orig(&g_orig_update)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, g_ini, "button icons");
}

}  // namespace pad::button_icons
