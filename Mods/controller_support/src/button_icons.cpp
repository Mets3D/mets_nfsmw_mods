// The game draws its button prompts as images of keyboard keys (PC_KEY_ENTER, ...). Each frame, every such image on
// screen is switched to the controller picture while the last input was from a controller, and back to the key picture
// otherwise. Which prompts, and which picture each shows, is in prompts.ini (controller_support/buttons/, copied to the
// data folder as nfsmw_controller_buttons.ini); the pictures are built from the PNGs there into our own texture pack,
// nfsmw_controller_buttons.tpk, with one texture per prompt, named <STYLE>_<prompt> (XBOX_PC_KEY_ENTER). The game's
// per-frame UI update (FEngine::Update) is the hook that finds the images.
//
// The same idea, and the pictures, are from ThirteenAG's WidescreenFixesPack (MIT License, see buttons/LICENSE).
#include "button_icons.h"

#include <algorithm>
#include <string>
#include <vector>

#include "device_settings.h"
#include "hooks.h"
#include "input.h"
#include "texture_pack.h"

namespace pad::button_icons {

namespace {

Style g_default_style = Style::kXbox;

// What this frame shows: the last input was a controller, and the style of that controller.
bool g_frame_pad = false;
Style g_frame_style = Style::kXbox;

// A prompt picture of the game and its two controller versions (the texture hashes).
struct Hashes {
    unsigned pc, xbox, playstation;
};
std::vector<Hashes> g_hashes;
unsigned g_quit_key, g_back_key, g_back_key_french;  // the picture hashes of the "Quit" (Q) and "Back" (Esc) prompts

// While the objects are walked each frame: is a Quit prompt (key Q) / a Back prompt (key Esc) on screen? On the main menu
// the first is and the second is not, and the controller's B quits there.
bool g_quit_prompt_seen = false, g_back_prompt_seen = false;
bool g_quit_available = false;  // what the last frame showed

bool g_pack_tried = false;
bool g_pack_loaded = false;  // the texture pack with the controller pictures is in the game

// An FEng object: type at +0x18, flags at +0x1C, the texture (for an image) at +0x24.
constexpr unsigned kType = 0x18, kFlags = 0x1C, kTexture = 0x24;
constexpr int kImage = 1;
constexpr unsigned kTextureChanged = 0x2400000;  // flags the engine needs to pick up a new texture
constexpr unsigned kInvisible = 1;               // the game's FEngSetInvisible sets this (and the changed flags) on an object

// The engine's callback for ForAllObjects: an object whose first member points to {destructor, function}. The function
// is called once per object, with the object as its argument.
struct CallbackVtable {
    void *destructor;
    void *function;
};

char __stdcall visit_object(void *object) {
    if (field<int>(object, kType) != kImage) return 1;
    unsigned &texture = field<unsigned>(object, kTexture);
    const bool visible = (field<unsigned>(object, kFlags) & kInvisible) == 0;
    for (const Hashes &h : g_hashes) {
        if (texture != h.pc && texture != h.xbox && texture != h.playstation) continue;
        if (visible) {
            if (h.pc == g_quit_key) g_quit_prompt_seen = true;
            if (h.pc == g_back_key || h.pc == g_back_key_french) g_back_prompt_seen = true;
        }
        if (!g_pack_loaded) break;
        const unsigned wanted = !g_frame_pad || g_frame_style == Style::kOff ? h.pc : g_frame_style == Style::kPlayStation ? h.playstation : h.xbox;
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

void FASTCALL hook_update(void *engine, void *edx, int ticks, unsigned lock) {
    g_frame_pad = input::last_input_was_pad();
    if (g_frame_pad) {
        const int slot = input::last_pad_slot();  // -1: a controller that is not XInput
        g_frame_style = slot >= 0 ? device_settings::prompts(slot) : g_default_style;
    }
    if (!g_pack_tried && g_frame_pad && g_frame_style != Style::kOff) {  // the first time the pictures are wanted
        g_pack_tried = true;
        char path[MAX_PATH];
        nfsmw::data_file("nfsmw_controller_buttons.tpk", path, sizeof(path));
        g_pack_loaded = nfsmw::load_texture_pack(path, g_log);
        if (!g_pack_loaded) log_line("button icons: the texture pack is missing; the pictures stay as the game has them");
    }
    {
        static const CallbackVtable vtable = {nullptr, reinterpret_cast<void *>(&visit_object)};
        const CallbackVtable *callback = &vtable;  // the callback object: its first member is the table
        g_quit_prompt_seen = g_back_prompt_seen = false;
        for (void *package = field<void *>(engine, 0xE0); package;) {
            ForAllObjects(package, nullptr, &callback);
            void *next = field<void *>(package, 4);
            if (next == package) break;
            package = next;
        }
    }
    g_quit_available = g_quit_prompt_seen && !g_back_prompt_seen;
    g_orig_update(engine, edx, ticks, lock);
}

const char *style_name(const char *section) { return !_stricmp(section, "xbox") ? "XBOX" : "PLAYSTATION"; }

// The prompts of one style in prompts.ini: the names left of the "=" (the picture on the right is only for the pack).
std::vector<std::string> read_prompts(const char *ini, const char *section) {
    static char buffer[16384];
    std::vector<std::string> prompts;
    const DWORD n = GetPrivateProfileSectionA(section, buffer, sizeof(buffer), ini);
    for (const char *line = buffer; line < buffer + n && *line; line += strlen(line) + 1) {
        std::string text(line, strcspn(line, ";"));
        const size_t eq = text.find('=');
        if (eq == std::string::npos) continue;
        text.resize(eq);
        while (!text.empty() && isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
        size_t first = 0;
        while (first < text.size() && isspace(static_cast<unsigned char>(text[first]))) ++first;
        if (first < text.size()) prompts.push_back(text.substr(first));
    }
    return prompts;
}

// The hashes of every prompt in the file. A prompt a style does not list stays as the game has it in that style.
void load_prompts() {
    char path[MAX_PATH];
    nfsmw::data_file("nfsmw_controller_buttons.ini", path, sizeof(path));
    const std::vector<std::string> xbox = read_prompts(path, "xbox"), playstation = read_prompts(path, "playstation");
    std::vector<std::string> all = xbox;
    for (const std::string &p : playstation)
        if (std::find(all.begin(), all.end(), p) == all.end()) all.push_back(p);
    for (const std::string &prompt : all) {
        const unsigned pc = nfsmw::texture_hash(prompt.c_str());
        const bool in_xbox = std::find(xbox.begin(), xbox.end(), prompt) != xbox.end();
        const bool in_playstation = std::find(playstation.begin(), playstation.end(), prompt) != playstation.end();
        g_hashes.push_back({pc, in_xbox ? nfsmw::texture_hash((std::string(style_name("xbox")) + "_" + prompt).c_str()) : pc,
                            in_playstation ? nfsmw::texture_hash((std::string(style_name("playstation")) + "_" + prompt).c_str()) : pc});
    }
    log_line("button icons: %u prompts from %s%s", static_cast<unsigned>(all.size()), path,
             all.empty() ? " (missing or empty: the pictures stay as the game has them)" : "");
}

}  // namespace

bool quit_prompt_shown() { return g_quit_available; }

const char *style_text(Style style) { return style == Style::kOff ? "off" : style == Style::kXbox ? "xbox" : "playstation"; }

Style parse_style(const char *text, Style fallback) {
    if (!_stricmp(text, "off") || !_stricmp(text, "0") || !_stricmp(text, "none")) return Style::kOff;
    if (!_stricmp(text, "playstation") || !_stricmp(text, "ps")) return Style::kPlayStation;
    if (!_stricmp(text, "xbox")) return Style::kXbox;
    return fallback;
}

Style default_style() { return g_default_style; }

void load_settings() {
    char text[32] = {};
    GetPrivateProfileStringA("buttons", "button_prompts", "xbox", text, sizeof(text), g_ini);
    g_default_style = parse_style(text, Style::kXbox);
    log_line("button icons: %s unless a controller has its own style", style_text(g_default_style));
}

void install_hooks() {
    load_prompts();
    g_quit_key = nfsmw::texture_hash("PC_KEY_Q");
    g_back_key = nfsmw::texture_hash("PC_KEY_ESC");
    g_back_key_french = nfsmw::texture_hash("PC_KEY_ESC_FRENCH");
    const nfsmw::Hook hooks[] = {
        {"icons", "FEngine::Update", 0x5C53C0, {0x83, 0xEC, 0x0C, 0x56, 0x8B, 0xF1, 0x8A}, false,
         nfsmw::detour(&hook_update), nfsmw::orig(&g_orig_update)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, g_ini, "button icons");
}

}  // namespace pad::button_icons
