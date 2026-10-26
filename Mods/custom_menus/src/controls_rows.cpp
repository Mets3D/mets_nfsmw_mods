#include "controls_rows.h"

#include <vector>

#include "hooks.h"
#include "translations.h"

namespace custom_menus::controls_rows {

namespace opt = nfsmw::custom_menus;
using nfsmw::field;

namespace {

// ------------------------------------------------------------------------------------------------
// Rows are binding entries by index: the game has 18 for the keyboard list (entries 0-17) and 19 for the controller list
// (0-18). The mods add `n` rows in between and at the end:
//  - the row count (0x12 + controller) is raised by n at the 8 places that compute it;
//  - the screen keeps a copy of each row's two bindings (0x18 bytes per row from +0xE8, to detect and undo changes),
//    room for 19 rows ending at its one-byte field at +0x2B0; the object grows from 0x2B4 to hold 19 + n rows and that
//    field moves to the end of them;
//  - the helpers that turn a row into a binding entry translate the added rows to their entries (layout[] below).
// ------------------------------------------------------------------------------------------------
struct CodePatch {
    uintptr_t addr;
    unsigned char expect[6];
    int len;
    int at;  // offset of the patched value
    int kind;  // what the value is
    int value_size;
};
enum PatchKind { kObjectSize, kFlagOffset, kRowCount };
const CodePatch kPatches[] = {
    {0x571D16, {0x68, 0xB4, 0x02, 0x00, 0x00}, 5, 1, kObjectSize, 4},  // new (0x2B4) in CreatePausePCController
    {0x52ACA1, {0x88, 0x97, 0xB0, 0x02, 0x00, 0x00}, 6, 2, kFlagOffset, 4},  // the +0x2B0 byte: refresh
    {0x51D56D, {0x2A, 0x85, 0xB0, 0x02, 0x00, 0x00}, 6, 2, kFlagOffset, 4},  // compare
    {0x51D645, {0x8A, 0x95, 0xB0, 0x02, 0x00, 0x00}, 6, 2, kFlagOffset, 4},  // restore
    {0x52ACE8, {0x83, 0xC1, 0x12}, 3, 2, kRowCount, 1},  // row counts: refresh
    {0x52AE1D, {0x83, 0xC0, 0x12}, 3, 2, kRowCount, 1},
    {0x51D42E, {0x83, 0xC1, 0x12}, 3, 2, kRowCount, 1},  // labels
    {0x51D498, {0x83, 0xC2, 0x12}, 3, 2, kRowCount, 1},
    {0x51D4E7, {0x83, 0xC1, 0x12}, 3, 2, kRowCount, 1},  // compare
    {0x51D555, {0x83, 0xC0, 0x12}, 3, 2, kRowCount, 1},
    {0x51D5C7, {0x83, 0xC1, 0x12}, 3, 2, kRowCount, 1},  // restore
    {0x51D636, {0x83, 0xC2, 0x12}, 3, 2, kRowCount, 1},
};

nfsmw::Logger *g_log = nullptr;
std::vector<opt::ControlsRow> g_rows;
std::vector<nfsmw::LangEntry> g_texts;
std::vector<nfsmw::LangEntry> g_sources;  // the same with the English texts, to translate again when the language changes
opt::BindingLabelFn g_label_fn = nullptr;
std::vector<void (*)()> g_refresh_fns, g_defaults_fns;

bool g_active = false;      // the code is patched and the helpers translate
std::vector<int> g_layout[2];  // [0] keyboard list, [1] controller list: row -> binding entry

void build_layout() {
    for (int list = 0; list < 2; ++list) {
        const int game_rows = list == 0 ? 18 : 19;
        std::vector<int> &out = g_layout[list];
        out.clear();
        for (int g = 0; g < game_rows; ++g) {
            out.push_back(g);
            for (const opt::ControlsRow &r : g_rows)
                if (r.after_row == g) out.push_back(r.entry);
        }
        for (const opt::ControlsRow &r : g_rows)
            if (r.after_row < 0 || r.after_row >= game_rows) out.push_back(r.entry);
    }
}

bool patch_code() {
    for (const CodePatch &c : kPatches) {
        if (memcmp(reinterpret_cast<const void *>(c.addr), c.expect, c.len) != 0) {
            g_log->line("controls rows: code at %#x is not as expected (another mod?); no extra rows",
                        static_cast<unsigned>(c.addr));
            return false;
        }
    }
    const unsigned n = static_cast<unsigned>(g_rows.size());
    const unsigned flag_at = 0xE8 + 0x18 * (19 + n);  // after the copies of all rows' bindings
    for (const CodePatch &c : kPatches) {
        const unsigned value = c.kind == kObjectSize ? flag_at + 4 : c.kind == kFlagOffset ? flag_at : 0x12 + n;
        unsigned char *at = reinterpret_cast<unsigned char *>(c.addr + c.at);
        DWORD old = 0;
        VirtualProtect(at, c.value_size, PAGE_EXECUTE_READWRITE, &old);
        if (c.value_size == 4) *reinterpret_cast<unsigned *>(at) = value;
        else *at = static_cast<unsigned char>(value);
        VirtualProtect(at, c.value_size, old, &old);
    }
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    return true;
}

void *g_screen = nullptr;  // the open UIOptionsPCController (+0xE4: 1 = keyboard list, 3 = controller)

int entry_for_row(int column, int row) {
    if (!g_active) return row;
    const std::vector<int> &layout = g_layout[column == 1 ? 0 : 1];
    return row >= 0 && row < static_cast<int>(layout.size()) ? layout[row] : row;
}

int controls_column() { return g_screen ? field<int>(g_screen, 0xE4) : 3; }

bool from_controls_screen(void *return_address) {
    const uintptr_t a = reinterpret_cast<uintptr_t>(return_address);
    return (a >= 0x51D410 && a < 0x51D740) || (a >= 0x52A7B0 && a < 0x52B300);
}

// The mods' strings: added to the language table, or replacing the game's own (the table is sorted by hash; the game's
// search is unsigned).
void ensure_texts() {
    nfsmw::LangEntry *cur = *reinterpret_cast<nfsmw::LangEntry **>(nfsmw::lang::kTable);
    const unsigned count = *reinterpret_cast<unsigned *>(nfsmw::lang::kCount);
    if (!cur || !count) return;
    std::vector<nfsmw::LangEntry> missing;
    for (const nfsmw::LangEntry &t : g_texts) {
        unsigned lo = 0, hi = count;
        while (lo < hi) {
            const unsigned mid = (lo + hi) / 2;
            if (cur[mid].hash < t.hash) lo = mid + 1;
            else hi = mid;
        }
        if (lo < count && cur[lo].hash == t.hash) cur[lo].text = t.text;
        else missing.push_back(t);
    }
    if (!missing.empty() && nfsmw::merge_language_entries(missing.data(), static_cast<unsigned>(missing.size())))
        g_log->line("controls rows: %d strings added to the language table", static_cast<int>(missing.size()));
}

using MethodFn = void(FASTCALL *)(void *, void *);
MethodFn g_orig_refresh;
void FASTCALL hook_refresh(void *screen, void *edx) {
    g_screen = screen;
    if (g_active) ensure_texts();
    for (void (*fn)() : g_refresh_fns) fn();
    g_orig_refresh(screen, edx);
}

// Row -> the entry's binding slots. Also used by the profile save and load for entries 0-18, which must not move.
using RowSlotsFn = void *(__stdcall *)(int);
RowSlotsFn g_orig_row_slots;
void *__stdcall hook_row_slots(int row) {
    if (from_controls_screen(__builtin_return_address(0))) row = entry_for_row(controls_column(), row);
    return g_orig_row_slots(row);
}

// Row -> language hash of the entry's name (row title, and the "press a key" prompt).
using RowNameFn = unsigned (*)(int);
RowNameFn g_orig_row_name;
unsigned hook_row_name(int row) { return g_orig_row_name(entry_for_row(controls_column(), row)); }

// Clear a binding / start remapping one: GameDevice methods taking (column, row, primary).
using RowActionFn = void(FASTCALL *)(void *, void *, int, int, int);
RowActionFn g_orig_clear_binding, g_orig_start_remap;
void FASTCALL hook_clear_binding(void *device, void *edx, int column, int row, int primary) {
    g_orig_clear_binding(device, edx, column, entry_for_row(column, row), primary);
}
void FASTCALL hook_start_remap(void *device, void *edx, int column, int row, int primary) {
    g_orig_start_remap(device, edx, column, entry_for_row(column, row), primary);
}

// The game's controller defaults (0x63CD80, per controller model): the Controls screen's Defaults, and a profile loaded
// with another kind of controller than the one connected. (It also runs once at start-up, before any mod has registered.)
MethodFn g_orig_defaults;
void FASTCALL hook_defaults(void *device, void *edx) {
    g_orig_defaults(device, edx);
    for (void (*fn)() : g_defaults_fns) fn();
}

// The Controls menu's label for one binding: (out, column: 1 keyboard / 3 controller, row, primary). Only called by the
// Controls menu, with rows.
using BindingLabelHookFn = void (*)(char *, int, int, bool);
BindingLabelHookFn g_orig_label;
void hook_label(char *out, int column, int row, bool primary) {
    const int entry = entry_for_row(column, row);
    g_orig_label(out, column, entry, primary);
    if (g_label_fn && out) {
        g_label_fn(out, column, entry, primary);
        const char *translated = translations::translate(out);  // the label buffer is 0x104 bytes
        if (translated != out) {
            strncpy(out, translated, 0x103);
            out[0x103] = 0;
        }
    }
}

}  // namespace

void add_rows(const opt::ControlsRow *rows, int count) {
    for (int i = 0; i < count; ++i) g_rows.push_back(rows[i]);
}

void add_text(unsigned hash, const char *text) {
    g_sources.push_back({hash, text});
    g_texts.push_back({hash, translations::translate(text)});
}

void retranslate() {
    for (size_t i = 0; i < g_sources.size(); ++i) g_texts[i].text = translations::translate(g_sources[i].text);
    ensure_texts();  // the game's new language table has none of them
}

void set_binding_label(opt::BindingLabelFn fn) { g_label_fn = fn; }

void add_refresh(void (*fn)()) { g_refresh_fns.push_back(fn); }

void add_controller_defaults(void (*fn)()) { g_defaults_fns.push_back(fn); }

void apply(nfsmw::Logger &log, const char *ini) {
    g_log = &log;
    ensure_texts();  // texts registered for anything but Controls rows (a mod's own screens) need to be in the table too
    if (!g_defaults_fns.empty()) {
        const nfsmw::Hook defaults[] = {
            {"controller_defaults", "controller defaults", 0x63CD80, {0xA1, 0x50, 0xF1, 0x91, 0x00, 0x81, 0xEC}, false,
             nfsmw::detour(&hook_defaults), nfsmw::orig(&g_orig_defaults)},
        };
        nfsmw::install_hooks(defaults, 1, log, ini, "controller defaults");
    }
    if (g_rows.empty() && !g_label_fn && g_refresh_fns.empty()) return;
    // The helpers: a half-translated menu would edit the wrong bindings, so all of them or none (without the rows
    // they pass everything on unchanged).
    const nfsmw::Hook hooks[] = {
        {"row_slots", "row -> binding slots", 0x628CB0, {0x8B, 0x44, 0x24, 0x04, 0x6B, 0xC0, 0x34}, true,
         nfsmw::detour(&hook_row_slots), nfsmw::orig(&g_orig_row_slots)},
        {"row_name", "row -> name", 0x627BB0, {0x8B, 0x44, 0x24, 0x04, 0x6B, 0xC0, 0x34}, true,
         nfsmw::detour(&hook_row_name), nfsmw::orig(&g_orig_row_name)},
        {"clear", "clear binding", 0x628560, {0x8B, 0x44, 0x24, 0x04, 0x33, 0xD2, 0x83}, true,
         nfsmw::detour(&hook_clear_binding), nfsmw::orig(&g_orig_clear_binding)},
        {"remap", "start remap", 0x628C00, {0x8B, 0x44, 0x24, 0x04, 0x83, 0xF8, 0x01}, true,
         nfsmw::detour(&hook_start_remap), nfsmw::orig(&g_orig_start_remap)},
        {"label", "binding label (Controls menu)", 0x628230, {0x53, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x14}, true,
         nfsmw::detour(&hook_label), nfsmw::orig(&g_orig_label)},
        {"controls_refresh", "Controls menu refresh", 0x52AC70, {0x6A, 0xFF, 0x68, 0x78, 0xEF, 0x86, 0x00}, true,
         nfsmw::detour(&hook_refresh), nfsmw::orig(&g_orig_refresh)},
    };
    const nfsmw::HookResult r = nfsmw::install_hooks(hooks, 6, log, ini, "Controls screen rows");
    if (!r.ok || r.created != 6) {
        log.line("controls rows: hooking the row helpers failed; no extra rows");
        return;
    }
    if (g_rows.empty()) return;
    build_layout();
    if (!patch_code()) return;
    g_active = true;
    log.line("controls rows: %d rows added to the Controls screen", static_cast<int>(g_rows.size()));
}

}  // namespace custom_menus::controls_rows
