/*
 * nfsmw_custom_menus: the Options menu framework for speed.exe 1.3 (SHA-256 0C5675A0...2067E342).
 *
 * Other mods add pages to Pause > Options through this one (see common/custom_menus_api.h): a page is a title, an icon and
 * a list of rows, and this mod builds the screen from the game's own Options layout (Pause_Options.fng, via the
 * Player category's screen with its rows replaced), so it looks and feels native:
 *  - an entry per page in the Options menu, with its icon (from a texture pack of the mod, or a fallback);
 *  - rows that change with left / right (Shift: finer steps), the game's Camera selector as a row kind;
 *  - like the game's own options, edits take effect at once and are kept on Accept; Back offers to discard them;
 *    Defaults (1) resets the page after the game's confirmation;
 *  - a "2 Help" hint (a clone of the layout's Defaults hint) and the 2 key (X on a controller) explain the
 *    highlighted row in the game's message box.
 * Every plugin that exports nfsmw_custom_menus_provide gets asked for its pages the first time the Options menu opens.
 *
 * Files in <game>/scripts/mets_mods/: nfsmw_custom_menus.ini (settings), nfsmw_custom_menus.log, nfsmw_custom_menus_crash.log (access
 * violations anywhere in the game, with the modules involved; handy for any of our mods).
 * ini settings, all optional:
 *   [features] help_hint=0, hide_dialog_icon=0, texture_pack=0  turn those parts off
 *   [hooks]    <name>=0  skips one hook (names in install_hooks)
 *
 * Offsets come from the PC disassembly; the decomp's struct offsets are from the console builds and differ.
 */

#include <nfsmw_sdk/nfsmw_sdk.h>
#include <MinHook.h>
#include <tlhelp32.h>

#include <csetjmp>
#include <cmath>
#include <deque>
#include <string>
#include <vector>

#include "hooks.h"
#include "nfsmw_util.h"
#include "custom_menus_api.h"
#include "controls_rows.h"
#include "texture_pack.h"
#include "title_icon.h"
#include "xinput.h"

NFSMW_PLUGIN_DECLARE("NFSMW Options Menu", "1.0.0", "Demeter Dzadik")

#define THISCALL __attribute__((thiscall))
// FASTCALL (__fastcall) comes from windows.h.

namespace {

using nfsmw::field;
using nfsmw::LangEntry;
namespace opt = nfsmw::custom_menus;

nfsmw::Logger g_log;

void log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_line(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    g_log.vline(fmt, ap);
    va_end(ap);
}

char g_ini[MAX_PATH];

// ------------------------------------------------------------------------------------------------
// Crash diagnostics. The handler runs in the middle of a crash, so it avoids the CRT's FILE locks and the
// loader lock (an earlier version could hang the game): raw WriteFile, module bases from VirtualQuery.
// The Help-hint code runs under a guard: an access violation inside it is logged and skipped.
// ------------------------------------------------------------------------------------------------
HANDLE g_crash_file = INVALID_HANDLE_VALUE;
void *g_self_base = nullptr;
jmp_buf g_guard_jb;
volatile bool g_guard_on = false;

void crash_write(const char *fmt, ...) {
    if (g_crash_file == INVALID_HANDLE_VALUE) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) n = sizeof(buf) - 2;
    buf[n++] = '\r';
    buf[n++] = '\n';
    DWORD written = 0;
    WriteFile(g_crash_file, buf, static_cast<DWORD>(n), &written, nullptr);
}

const char *label_for(const void *addr, unsigned *offset) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(addr, &mbi, sizeof(mbi)) || mbi.Type != MEM_IMAGE) return nullptr;
    *offset = static_cast<unsigned>(static_cast<const unsigned char *>(addr) - static_cast<unsigned char *>(mbi.AllocationBase));
    if (mbi.AllocationBase == reinterpret_cast<void *>(0x400000)) return "speed.exe";
    if (mbi.AllocationBase == g_self_base) return "nfsmw_custom_menus.asi";
    return "module";
}

LONG WINAPI crash_logger(EXCEPTION_POINTERS *e) {
    static int count = 0;
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    const bool guarded = g_guard_on;
    if (count++ < 4) {
        const CONTEXT *c = e->ContextRecord;
        unsigned off = 0;
        const char *mod = label_for(reinterpret_cast<void *>(c->Eip), &off);
        crash_write("!! ACCESS VIOLATION at eip=%#lx (%s+%#x) %s address %#lx%s", c->Eip, mod ? mod : "?", off,
                    e->ExceptionRecord->ExceptionInformation[0] == 0 ? "reading" : "writing",
                    static_cast<unsigned long>(e->ExceptionRecord->ExceptionInformation[1]),
                    guarded ? "  [inside the guarded Help-hint code]" : "");
        crash_write("!! eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx", c->Eax, c->Ebx,
                    c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
        const unsigned long *sp = reinterpret_cast<const unsigned long *>(c->Esp);
        MEMORY_BASIC_INFORMATION smbi;
        if (VirtualQuery(sp, &smbi, sizeof(smbi))) {
            const unsigned long *end = reinterpret_cast<const unsigned long *>(
                static_cast<unsigned char *>(smbi.BaseAddress) + smbi.RegionSize);
            int shown = 0;
            for (int i = 0; i < 512 && sp + i < end && shown < 30; ++i) {
                const unsigned long v = sp[i];
                if (v < 0x10000) continue;
                MEMORY_BASIC_INFORMATION mbi;
                if (!VirtualQuery(reinterpret_cast<void *>(v), &mbi, sizeof(mbi)) || mbi.Type != MEM_IMAGE) continue;
                if (!(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) continue;
                if (const char *m = label_for(reinterpret_cast<void *>(v), &off)) {
                    crash_write("!!   [esp+%03x] %#lx = %s+%#x", i * 4, v, m, off);
                    ++shown;
                }
            }
        }
    }
    if (guarded) {
        g_guard_on = false;
        longjmp(g_guard_jb, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// ------------------------------------------------------------------------------------------------
// The game's functions and data we use (speed.exe 1.3). The hooked functions are listed in install_hooks.
// thiscall functions are hooked with fastcall detours: (this in ecx, unused edx, stack arguments).
// ------------------------------------------------------------------------------------------------
namespace game {
template <typename Fn>
Fn at(uintptr_t addr) { return reinterpret_cast<Fn>(addr); }

const auto Alloc = at<void *(*)(unsigned)>(0x652AD0);  // operator new
const auto IsPaused = at<bool(THISCALL *)(void *)>(0x64B680);  // GameFlowManager::IsPaused
const auto GetTextureInfo = at<void *(*)(unsigned hash, int return_default, int include_unloaded)>(0x503400);

// Strings and images
const auto FEngSetLanguageHash = at<void (*)(void *string, unsigned hash)>(0x515C00);
const auto FEngSetLanguageHashPkg = at<void (*)(const char *pkg, unsigned object, unsigned hash)>(0x525220);
const auto FEPrintf = at<int (*)(void *string, const char *fmt, ...)>(0x515D70);
const auto FEPrintfPkg = at<int (*)(const char *pkg, int object, const char *fmt, ...)>(0x5252D0);  // strings and groups
const auto FESetString = at<void (*)(void *string, const unsigned short *text)>(0x515CC0);
const auto FEngSetTextureHash = at<void (*)(void *image, unsigned hash)>(0x515B90);
const auto FEngSetInvisiblePkg = at<void (*)(const char *pkg, unsigned object)>(0x527DB0);
const auto ShowOneButton = at<unsigned (*)(const char *pkg, const char *dialog_pkg, int title, unsigned button_text,
                                           unsigned button_msg, unsigned back_msg, unsigned text)>(0x5989B0);

// FEng objects
const auto FEngFindGroup = at<void *(*)(const char *pkg, unsigned object)>(0x524880);
const auto FEngGetCenter = at<void (*)(void *object, float *x, float *y)>(0x524EE0);
const auto FindPackage = at<void *(THISCALL *)(void *feng, const char *pkg)>(0x516AF0);  // cFEng::FindPackage
const auto GroupClone = at<void *(THISCALL *)(void *group, int reference)>(0x5C49C0);
const auto GroupAddObject = at<void(THISCALL *)(void *group, void *object)>(0x5B5420);
const auto PackageAddObject = at<void(THISCALL *)(void *pkg, void *object)>(0x50DB90);
const auto BuildMouseObjectStateList = at<void(THISCALL *)(void *pkg)>(0x5B88F0);
const auto SetScript = at<void(THISCALL *)(void *object, unsigned id, int force)>(0x5BC160);  // FEObject::SetScript

// Option screens and their rows
const auto ClearWidgets = at<void(THISCALL *)(void *screen)>(0x588C90);
const auto RefreshWidgets = at<void(THISCALL *)(void *screen)>(0x588D20);  // Draw() on every row
const auto AddToggleOption = at<unsigned(THISCALL *)(void *screen, void *widget, int enabled)>(0x588570);
const auto ToggleWidgetCtor = at<void *(THISCALL *)(void *widget, int enabled)>(0x589300);  // FEToggleWidget, 0x5C bytes
const auto DriveCamCtor = at<void *(THISCALL *)(void *widget, int enabled)>(0x51AA90);  // PODriveCam, 0x5C bytes
const auto DriveCamAct = at<void(THISCALL *)(void *widget, const char *pkg, unsigned data)>(0x51C870);
const auto IconOptionCtor = at<void(THISCALL *)(void *option, unsigned tex, unsigned name, unsigned desc)>(0x586FA0);
constexpr uintptr_t kToggleDelDtor = 0x589370;      // FEToggleWidget scalar deleting destructor
constexpr uintptr_t kIconOptionDelDtor = 0x51A500;  // IconOption scalar deleting destructor
constexpr uintptr_t kDriveCamVtable = 0x89C048;     // 15 slots: [0] dtor [1] Act [3] Draw [14] BlinkArrows

// Data
constexpr uintptr_t kFEDatabase = 0x91CF90;  // FEDatabase*; +0x10 = options settings
constexpr uintptr_t kGameFlowManager = 0x925E70;
constexpr uintptr_t kFEng = 0x91CADC;  // cFEng*
constexpr uintptr_t kInGameDialogName = 0x89736C;  // "InGameDialog.fng"
}  // namespace game

// Layouts (PC). FEObject: the base of every FEng object.
namespace fe {
constexpr unsigned kNext = 0x04;          // FEMinNode: list link (lists of objects, responses, scripts)
constexpr unsigned kGuid = 0x0C;
constexpr unsigned kNameHash = 0x10;
constexpr unsigned kType = 0x18;          // kImage, kString, kGroup
constexpr unsigned kFlags = 0x1C;
constexpr unsigned kHandle = 0x24;        // images: texture hash
constexpr unsigned kData = 0x2C;          // FEObjData*: colour (alpha at +0xC), pivot, position (x at +0x1C), ...
constexpr unsigned kDataSize = 0x30;
constexpr unsigned kResponseCount = 0x38;  // message responses; node: MsgID +0xC, count +0x10,
constexpr unsigned kResponseHead = 0x3C;   //   array +0x14 of {ID, Param, Target}
constexpr unsigned kScriptCount = 0x48;    // scripts; FEScript: events count +0x24, array +0x28 of
constexpr unsigned kScriptHead = 0x4C;     //   {EventID, Target, Time}, ID +0x30
constexpr unsigned kCurrentScript = 0x54;
constexpr unsigned kCached = 0x58;         // render object built from the data
constexpr unsigned kChildCount = 0x60;     // FEGroup
constexpr unsigned kChildHead = 0x64;
constexpr unsigned kPackageObjects = 0x4C;  // FEPackage: head of its top-level objects

constexpr unsigned kDataAlpha = 0x0C;
constexpr unsigned kDataPosX = 0x1C;

constexpr int kImage = 1, kString = 2, kGroup = 5;
constexpr unsigned kMouseObject = 1u << 17;  // hit-box: the package tracks the mouse over it
constexpr unsigned kDirtyColor = 1u << 23, kDirtyTransform = 1u << 24, kDirty = 1u << 25;
}  // namespace fe

// UIWidgetMenu (option screens) and its rows (FEStatWidget / FEToggleWidget).
constexpr unsigned kScreenPackage = 0x10;  // const char* package name
constexpr unsigned kScreenCurrentRow = 0x34;
constexpr unsigned kRowChanged = 0x2A;  // set by the game's rows when their value changes
constexpr unsigned kRowTitle = 0x2C;    // FEString* label
constexpr unsigned kRowData = 0x30;     // FEString* value
constexpr unsigned kRowIndex = 0x5C;    // ours: the row's index in its page, after the 0x5C-byte widget
constexpr unsigned kRowPage = kRowIndex + 4;  // ours: the page (PageInfo) the row belongs to
constexpr unsigned kRowSize = kRowPage + 4;
constexpr unsigned kEntryPage = 0x5C;   // ours: the page of an Options menu entry (IconOption, 0x5C bytes)

// Hashes: FEng objects, messages, scripts, textures, language strings.
constexpr unsigned kHeaderText = 0x42ADB44C;  // HEADER_TEXT: the title of a screen or message box
constexpr unsigned kDialogIcon1 = 0x0D4F4069, kDialogIcon2 = 0xFAC88427;  // the message box's exclamation mark
// The Options layout has two hint bars: console hints (hidden on PC) and the PC hints that are shown, top-level
// groups of {label string, clickable hit-box group, key image}.
constexpr unsigned kPcDefaultsHint = 0x3FC72F9D;  // "1 Defaults"
constexpr unsigned kPcAcceptHint = 0x9502D148;    // "Enter Accept"
constexpr unsigned kHintName = 0xCA3E00A0;        // our clone's name; its objects get kHintName + 0x100 + i
constexpr unsigned kHintGuidBase = 0xCA3E2000;
constexpr unsigned kHelpClickMsg = 0xCA3E0B01;    // what a click on our hint posts to the screen

constexpr unsigned kButton5Msg = 0xC519BFC4;  // __PAD_BUTTON5__: the 1 key (Defaults)
constexpr unsigned kButtonPressed = 0x0C407210;
constexpr unsigned kPadLeft = 0x9120409E, kPadRight = 0xB5971BF1;
constexpr unsigned kScreenTick = 0xC98356BA;    // FEMSG_SCREEN_TICK: every frame
constexpr unsigned kExitComplete = 0xE1FDE1D1;  // FEHASH_EXITCOMPLETE: the screen finished leaving
constexpr unsigned kDialogMessageNo = 0x34DC1BCF;
constexpr unsigned kDialogOkText = 0x417B2601;
constexpr int kDialogInfo = 2;                    // eDialogTitle dialog_info
constexpr unsigned kEventSetButton = 0x1B3909AA;  // script event that moves the menu cursor
constexpr unsigned kScriptHighlight = 0x249DB7B7, kScriptUnhighlight = 0x7AB5521A;
constexpr int kCategoryPlayer = 4;  // OptionsSettings::CurrentCategory

constexpr unsigned kPcKey1Texture = 0x68A29A8A, kPcKey2Texture = 0x68A29A8B, kPcKey4Texture = 0x68A29A8D;
constexpr unsigned kCircleTexture = 0x54A68051;  // drawn as the 1 key on PC

// ------------------------------------------------------------------------------------------------
// Pages: what the mods register. Everything we show is a language string of the game (the game's labels, titles and
// dialogs all take a hash), so each page's texts get hashes from their names and are merged into the language table.
// ------------------------------------------------------------------------------------------------
constexpr int kMaxPages = 8, kMaxRows = 40;

struct PageInfo {
    const opt::Page *page;
    unsigned entry_hash, title_hash, help_title_hash;
    unsigned row_label_hash[kMaxRows];  // 0 for rows that use the game's own label
    unsigned row_help_hash[kMaxRows];
    unsigned icon;      // the texture shown: page->icon when the game has it, else the fallback
    bool available;     // as of the last time the Options menu opened
};

PageInfo g_pages[kMaxPages];
int g_page_count = 0;
std::deque<std::string> g_texts;  // the strings behind the language entries (stable addresses)
std::vector<LangEntry> g_entries;
bool g_discovered = false;

unsigned add_text(const char *id, const char *what, int row, const char *text) {
    char name[96];
    if (row >= 0) snprintf(name, sizeof(name), "NFSMW_CUSTOM_MENUS.%s.%d.%s", id, row, what);
    else snprintf(name, sizeof(name), "NFSMW_CUSTOM_MENUS.%s.%s", id, what);
    const unsigned hash = nfsmw::bstring_hash(name);
    g_texts.emplace_back(text);
    g_entries.push_back({hash, g_texts.back().c_str()});
    return hash;
}

void register_page(const opt::Page *page) {
    if (!page || !page->id || !page->rows || page->row_count > kMaxRows ||
        g_page_count >= kMaxPages) {
        log_line("register_page: page \"%s\" rejected (no id or rows, or too many rows or pages)", page && page->id ? page->id : "?");
        return;
    }
    PageInfo &p = g_pages[g_page_count++];
    p = {};
    p.page = page;
    p.entry_hash = add_text(page->id, "entry", -1, page->entry_text);
    p.title_hash = add_text(page->id, "title", -1, page->title);
    p.help_title_hash = add_text(page->id, "help_title", -1, page->help_title);
    for (int r = 0; r < page->row_count; ++r) {
        const opt::Row &row = page->rows[r];
        if (row.kind == opt::kValueRow && !row.label_hash) p.row_label_hash[r] = add_text(page->id, "label", r, row.label);
        else p.row_label_hash[r] = row.label_hash;
        p.row_help_hash[r] = add_text(page->id, "help", r, row.help);
    }
    log_line("page \"%s\": %d rows", page->id, page->row_count);
}

// Asks every loaded plugin for its pages, once. They are all loaded by the time the Options menu opens.
void discover_pages() {
    if (g_discovered) return;
    g_discovered = true;
    const opt::Host host = {&register_page, &custom_menus::controls_rows::add_rows, &custom_menus::controls_rows::add_text,
                            &custom_menus::controls_rows::set_binding_label, &custom_menus::controls_rows::add_refresh};
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        log_line("could not list the loaded modules");
        return;
    }
    MODULEENTRY32 m = {};
    m.dwSize = sizeof(m);
    for (BOOL ok = Module32First(snapshot, &m); ok; ok = Module32Next(snapshot, &m)) {
        const auto provide =
            reinterpret_cast<opt::ProvideFn>(reinterpret_cast<void *>(GetProcAddress(m.hModule, opt::kProvideExport)));
        if (provide) {
            log_line("pages from %s", m.szModule);
            provide(&host);
        }
    }
    CloseHandle(snapshot);
    log_line("%d page(s) registered", g_page_count);
    custom_menus::controls_rows::apply(g_log, g_ini);
}

void ensure_language_entries() {
    if (nfsmw::merge_language_entries(g_entries.data(), static_cast<unsigned>(g_entries.size())))
        log_line("language table: merged %lu entries", static_cast<unsigned long>(g_entries.size()));
}

// FESetString takes 16-bit characters, which lets us pass the degree sign (U+00B0) without guessing the byte
// encoding that FEPrintf expects. Character 1 in `ascii` stands for the degree sign.
void set_wide_text(void *fe_string, const char *ascii) {
    static unsigned short buf[96];
    size_t n = 0;
    for (; ascii[n] && n < 95; ++n) {
        const unsigned char c = static_cast<unsigned char>(ascii[n]);
        buf[n] = c == 1 ? 0x00B0 : c;
    }
    buf[n] = 0;
    game::FESetString(fe_string, buf);
}

// ------------------------------------------------------------------------------------------------
// The page screen: a category screen built from the game's own Options layout (Pause_Options.fng). It reuses the
// "Player" category (SetupPlayer) so the screen, its Defaults/Accept/Back buttons and the "settings changed?" logic
// stay native, then swaps the Player rows for the page's:
//   kCameraSelectorRow  the game's own Camera selector (PODriveCam, changes the player's camera view)
//   kValueRow           our row: label and value from the page
// ------------------------------------------------------------------------------------------------
using MethodFn = void(FASTCALL *)(void *, void *);  // a hooked thiscall method without arguments
using BoolMethodFn = bool(FASTCALL *)(void *, void *);
using BlinkFn = void(THISCALL *)(void *, unsigned);  // vtable slot 14: blink the arrow for an Act data value
using DrawFn = void(THISCALL *)(void *);            // vtable slot 3

PageInfo *g_page_next = nullptr;  // one of our Options entries was pressed
PageInfo *g_page = nullptr;       // the open Options screen is this page
void *g_options_screen = nullptr;
void *g_pending_optmain = nullptr;      // the Options menu being set up, which gets our entries
void *g_hint_pending_screen = nullptr;  // the Help hint is added on this screen's first tick

void *g_row_vtbl[15];       // our value rows (FEToggleWidget behaviour with our Act/Draw)
void *g_selector_vtbl[15];  // PODriveCam with Act wrapped so the other rows refresh when the view changes
void *g_entry_vtbl[2];      // Options menu entries

int &options_category() { return field<int>(field<void *>(*reinterpret_cast<void **>(game::kFEDatabase), 0x10), 0x24); }

// A button of the first XInput controller, read directly (no controller_support needed).
bool pad_button_down(WORD button) {
    nfsmw::xinput::Gamepad pad;
    return nfsmw::xinput::read(&pad) && (pad.buttons & button) != 0;
}

void THISCALL value_row_act(void *self, const char *pkg, unsigned data) {
    (void)pkg;
    if (data != kPadLeft && data != kPadRight) return;
    const int row = field<int>(self, kRowIndex);
    const PageInfo *page = field<PageInfo *>(self, kRowPage);
    if (!page || row < 0 || row >= page->page->row_count) return;
    if (page->page->change) page->page->change(row, data == kPadRight ? 1 : -1, (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 || pad_button_down(nfsmw::xinput::kLeftShoulder));
    field<unsigned char>(self, kRowChanged) = 1;  // as PODriveCam::Act does
    void **vtbl = field<void **>(self, 0);
    reinterpret_cast<BlinkFn>(vtbl[14])(self, data);  // arrow blink
    reinterpret_cast<DrawFn>(vtbl[3])(self);
}

void THISCALL value_row_draw(void *self) {
    const int row = field<int>(self, kRowIndex);
    const PageInfo *page = field<PageInfo *>(self, kRowPage);
    if (!page || row < 0 || row >= page->page->row_count) return;
    // The label goes in as a language hash, like PODriveCam::Draw does for "Camera". FEStatWidget::Position
    // centres a label on the column edge using the width of its literal text, which a language-hash label leaves
    // out, so the game's labels end at the edge (right-aligned). Text set with FESetString was measured and centred.
    game::FEngSetLanguageHash(field<void *>(self, kRowTitle), page->row_label_hash[row]);
    char text[64] = {};
    const unsigned hash = page->page->value ? page->page->value(row, text, sizeof(text)) : 0;
    if (hash) game::FEngSetLanguageHash(field<void *>(self, kRowData), hash);
    else set_wide_text(field<void *>(self, kRowData), text);
}

void THISCALL selector_row_act(void *self, const char *pkg, unsigned data) {
    game::DriveCamAct(self, pkg, data);
    if (g_options_screen) game::RefreshWidgets(g_options_screen);  // the other rows follow the new view
}

void init_option_vtables() {
    void **src = reinterpret_cast<void **>(game::kDriveCamVtable);
    for (int i = 0; i < 15; ++i) g_row_vtbl[i] = g_selector_vtbl[i] = src[i];
    g_row_vtbl[0] = reinterpret_cast<void *>(game::kToggleDelDtor);
    g_row_vtbl[1] = reinterpret_cast<void *>(&value_row_act);
    g_row_vtbl[3] = reinterpret_cast<void *>(&value_row_draw);
    g_selector_vtbl[1] = reinterpret_cast<void *>(&selector_row_act);
}

void build_page_screen(void *screen) {
    const opt::Page &page = *g_page->page;
    g_options_screen = screen;
    if (page.opened) page.opened();
    game::ClearWidgets(screen);  // drop the Player rows SetupPlayer just added
    for (int r = 0; r < page.row_count; ++r) {
        void *w = game::Alloc(kRowSize);
        if (!w) continue;
        if (page.rows[r].kind == opt::kCameraSelectorRow) {  // the game's own Camera selector
            game::DriveCamCtor(w, 1);
            field<void **>(w, 0) = g_selector_vtbl;
        } else {
            game::ToggleWidgetCtor(w, 1);
            field<void **>(w, 0) = g_row_vtbl;
        }
        field<int>(w, kRowIndex) = r;
        field<PageInfo *>(w, kRowPage) = g_page;
        game::AddToggleOption(screen, w, 1);
    }
    // The title object is a group of strings (text and shadow). Set the hash the way the game's own screens do, then
    // also print into the group so it does not depend on the table lookup.
    const char *pkg = field<const char *>(screen, kScreenPackage);
    game::FEngSetLanguageHashPkg(pkg, kHeaderText, g_page->title_hash);
    game::FEPrintfPkg(pkg, static_cast<int>(kHeaderText), "%s", page.title);
    custom_menus::title_icon::set(pkg, g_page->icon);
    game::RefreshWidgets(screen);
    g_hint_pending_screen = screen;  // the Help hint is cloned on the first tick, once the layout is live
    log_line("Options > %s built", page.entry_text);
}

// Rows that mods add to the game's own Player options (Placement kPlayerOptions): after the game's rows, in the pages
// that were available when the screen was built. Back, Defaults and Accept ask these pages as well as the game.
PageInfo *g_player_pages[kMaxPages];
int g_player_page_count = 0;
bool g_player_rows_open = false;  // the open Player screen has such rows

void add_player_rows(void *screen) {
    g_player_page_count = 0;
    g_player_rows_open = false;
    for (int i = 0; i < g_page_count; ++i) {
        PageInfo &info = g_pages[i];
        const opt::Page &page = *info.page;
        if (page.placement != opt::kPlayerOptions || (page.available && !page.available())) continue;
        if (page.opened) page.opened();
        for (int r = 0; r < page.row_count; ++r) {
            void *w = game::Alloc(kRowSize);
            if (!w) continue;
            game::ToggleWidgetCtor(w, 1);
            field<void **>(w, 0) = g_row_vtbl;
            field<int>(w, kRowIndex) = r;
            field<PageInfo *>(w, kRowPage) = &info;
            game::AddToggleOption(screen, w, 1);
        }
        g_player_pages[g_player_page_count++] = &info;
        log_line("Options > Player: %d row(s) from \"%s\"", page.row_count, page.id);
    }
    g_player_rows_open = g_player_page_count > 0;
    if (g_player_rows_open) game::RefreshWidgets(screen);
}

bool player_rows_changed() {
    if (!g_player_rows_open || options_category() != kCategoryPlayer) return false;
    for (int i = 0; i < g_player_page_count; ++i)
        if (g_player_pages[i]->page->changed && g_player_pages[i]->page->changed()) return true;
    return false;
}

MethodFn g_orig_setupplayer;
void FASTCALL hook_setupplayer(void *self, void *edx) {
    g_orig_setupplayer(self, edx);
    g_page = g_page_next;
    g_page_next = nullptr;
    ensure_language_entries();
    if (!g_page) {
        add_player_rows(self);  // the game's own Player options
        return;
    }
    g_player_rows_open = false;
    build_page_screen(self);
}

bool page_changed() { return g_page && g_page->page->changed && g_page->page->changed(); }

// Back asks "discard changes?" only when this says something changed; the page's values count as well.
BoolMethodFn g_orig_didnotchange;
bool FASTCALL hook_didnotchange(void *self, void *edx) {
    const bool unchanged = g_orig_didnotchange(self, edx);
    if (g_page && self == g_options_screen && page_changed()) return false;
    if (!g_page && player_rows_changed()) return false;
    return unchanged;
}

// The discard was confirmed: the game puts its own values back; put the page's back too.
MethodFn g_orig_restoreoriginals;
void FASTCALL hook_restoreoriginals(void *self, void *edx) {
    const bool player_rows = !g_page && player_rows_changed();
    g_orig_restoreoriginals(self, edx);
    if (g_page && self == g_options_screen && page_changed() && g_page->page->discard) {
        g_page->page->discard();
        log_line("%s: changes discarded", g_page->page->id);
    }
    if (player_rows)
        for (int i = 0; i < g_player_page_count; ++i)
            if (g_player_pages[i]->page->discard) g_player_pages[i]->page->discard();
    if (player_rows) log_line("Options > Player: changes to the added rows discarded");
}

// Defaults was confirmed: reset the page instead of the Player settings.
MethodFn g_orig_restoredefaults;
void FASTCALL hook_restoredefaults(void *self, void *edx) {
    if (g_page && options_category() == kCategoryPlayer) {
        if (g_page->page->defaults) g_page->page->defaults();
        log_line("%s: defaults", g_page->page->id);
        game::RefreshWidgets(self);
        return;
    }
    g_orig_restoredefaults(self, edx);
    if (!g_page && g_player_rows_open && options_category() == kCategoryPlayer) {
        for (int i = 0; i < g_player_page_count; ++i)
            if (g_player_pages[i]->page->defaults) g_player_pages[i]->page->defaults();
        log_line("Options > Player: defaults for the added rows");
        game::RefreshWidgets(self);
    }
}

// ------------------------------------------------------------------------------------------------
// The visible "2 Help" hint: a clone of the layout's own "1 Defaults" hint (FEGroup::Clone), placed one hint to the
// right. The clone is made on the screen's first tick, after the layout's own setup has run.
// ------------------------------------------------------------------------------------------------
struct HintPair {
    void *orig;
    void *copy;
    bool mirrored;  // runs the original's appear/fade scripts (everything except the hit-box)
};

struct HelpHint {
    void *screen;  // the Camera screen it belongs to
    void *clone;   // root group of the clone
    void *pkg;
    int n;
    HintPair pair[32];
};
HelpHint g_hint;

bool in_hint_clone(void *obj) {
    for (int i = 0; i < g_hint.n; ++i)
        if (g_hint.pair[i].copy == obj) return true;
    return false;
}

void *find_parent(void *container, bool is_pkg, void *target, bool *parent_is_pkg) {
    void *n = field<void *>(container, is_pkg ? fe::kPackageObjects : fe::kChildHead);
    for (int i = 0; n && i < 1024; ++i) {
        if (n == target) {
            *parent_is_pkg = is_pkg;
            return container;
        }
        if (field<int>(n, fe::kType) == fe::kGroup)
            if (void *r = find_parent(n, false, target, parent_is_pkg)) return r;
        n = field<void *>(n, fe::kNext);
    }
    return nullptr;
}

bool is_key_glyph(unsigned tex) {
    return tex == kPcKey1Texture || tex == kPcKey4Texture || tex == kCircleTexture || tex == kPcKey2Texture;
}

// FEObject's copy constructor leaves a clone broken in two ways: the cached render object holds garbage (the renderer
// then skips it, and unloading the package crashes in FERenderObject::Clear), and the object data (colour, position,
// size, UVs) is blank (position 0,0,0 is never drawn). Each cloned object also gets its own name hash and GUID: the
// mouse code finds a hit-box's screen offset by name (OffsetCalculatron), and responses and script events address
// objects by GUID, so with the original's names the clone's hit area landed on the Defaults hint.
void prepare_clone(void *orig, void *copy, int depth, bool mirrored) {
    if (field<unsigned>(copy, fe::kFlags) & fe::kMouseObject) mirrored = false;  // the hit-box keeps its own hover look
    if (g_hint.n < 32) {
        const int i = g_hint.n++;
        g_hint.pair[i] = {orig, copy, mirrored};
        field<unsigned>(copy, fe::kNameHash) = i == 0 ? kHintName : kHintName + 0x100 + i;
        field<unsigned>(copy, fe::kGuid) = kHintGuidBase + i;
    }
    field<void *>(copy, fe::kCached) = nullptr;
    void *od = field<void *>(orig, fe::kData);
    void *cd = field<void *>(copy, fe::kData);
    const unsigned osz = field<unsigned>(orig, fe::kDataSize), csz = field<unsigned>(copy, fe::kDataSize);
    const unsigned n = osz < csz ? osz : csz;
    if (od && cd && od != cd && n >= 0x44 && n <= 0x400) memcpy(cd, od, n);
    else log_line("  hint object %p: data not copied (orig %p/%u, copy %p/%u)", copy, od, osz, cd, csz);
    field<unsigned>(copy, fe::kFlags) |= fe::kDirtyColor | fe::kDirtyTransform | fe::kDirty;

    const int type = field<int>(copy, fe::kType);
    if (type == fe::kString) {  // the label
        game::FEPrintf(copy, "%s", "Help");
    } else if (type == fe::kImage) {  // the key glyph (the hit-box's backing image keeps its texture)
        if (is_key_glyph(field<unsigned>(copy, fe::kHandle))) game::FEngSetTextureHash(copy, kPcKey2Texture);
    } else if (type == fe::kGroup && depth < 4) {
        void *o = field<void *>(orig, fe::kChildHead);
        void *c = field<void *>(copy, fe::kChildHead);
        for (unsigned i = 0, count = field<unsigned>(copy, fe::kChildCount); o && c && i < count && i < 16; ++i) {
            prepare_clone(o, c, depth + 1, mirrored);
            o = field<void *>(o, fe::kNext);
            c = field<void *>(c, fe::kNext);
        }
    }
}

// A click runs the hit-box's message responses. On the clone, the response that posts BUTTON5 (Defaults) posts our
// own message straight to the screen instead, and responses aimed at one of the original's objects (by GUID) are
// aimed at the clone's counterpart. Returns {clicks retargeted, targets moved}.
void retarget_responses(int *clicks, int *targets) {
    *clicks = *targets = 0;
    for (int i = 0; i < g_hint.n; ++i) {
        void *ro = field<void *>(g_hint.pair[i].orig, fe::kResponseHead);
        void *rc = field<void *>(g_hint.pair[i].copy, fe::kResponseHead);
        for (unsigned k = 0, n = field<unsigned>(g_hint.pair[i].copy, fe::kResponseCount); rc && k < n && k < 32; ++k) {
            unsigned *list = field<unsigned *>(rc, 0x14);
            const unsigned *olist = ro ? field<unsigned *>(ro, 0x14) : nullptr;
            for (unsigned j = 0, cnt = field<unsigned>(rc, 0x10); list && list != olist && j < cnt && j < 16; ++j) {
                unsigned *r = list + j * 3;  // {ID, Param, Target}
                if (r[1] == kButton5Msg && (r[0] == 1 || r[0] == 2)) {
                    r[0] = 2;  // MR_PostMessageToGame: straight to the screen
                    r[1] = kHelpClickMsg;
                    ++*clicks;
                }
                for (int m = 0; m < g_hint.n && r[2]; ++m) {
                    if (r[2] == field<unsigned>(g_hint.pair[m].orig, fe::kGuid)) {
                        r[2] = field<unsigned>(g_hint.pair[m].copy, fe::kGuid);
                        ++*targets;
                        break;
                    }
                }
            }
            rc = field<void *>(rc, fe::kNext);
            ro = ro ? field<void *>(ro, fe::kNext) : nullptr;
        }
    }
}

bool responds_to(void *obj, unsigned msg) {
    void *r = field<void *>(obj, fe::kResponseHead);
    for (unsigned i = 0, n = field<unsigned>(obj, fe::kResponseCount); r && i < n && i < 32; ++i, r = field<void *>(r, fe::kNext))
        if (field<unsigned>(r, 0xC) == msg) return true;
    return false;
}

// The label's hover look comes from script events, not responses: the hit-box's mouse-over script posts 0x696851AD
// (label: HIGHLIGHT) and its mouse-exit script posts 0x1C8E4190 (label: UNHIGHLIGHT), both addressed to the whole
// package. The package hands them to the objects in its message target lists, which name the Defaults label only.
// So on the clone, such an event is addressed straight to the one clone object that responds to it, and events aimed
// at one of the original's objects are aimed at the clone's counterpart. (Each cloned script has its own events.)
int retarget_script_events() {
    int moved = 0;
    for (int i = 0; i < g_hint.n; ++i) {
        void *so = field<void *>(g_hint.pair[i].orig, fe::kScriptHead);
        void *sc = field<void *>(g_hint.pair[i].copy, fe::kScriptHead);
        for (unsigned k = 0, n = field<unsigned>(g_hint.pair[i].copy, fe::kScriptCount); sc && k < n && k < 32; ++k) {
            unsigned *ev = field<unsigned *>(sc, 0x28);
            const unsigned *oev = so ? field<unsigned *>(so, 0x28) : nullptr;
            for (unsigned j = 0, cnt = field<unsigned>(sc, 0x24); ev && ev != oev && j < cnt && j < 32; ++j) {
                const unsigned msg = ev[j * 3];
                unsigned &target = ev[j * 3 + 1];
                const unsigned before = target;
                if (target == 0 || target == 0xFFFFFFFC) {  // to everyone / to the package
                    int responder = -1, responders = 0;
                    for (int m = 0; m < g_hint.n; ++m)
                        if (responds_to(g_hint.pair[m].orig, msg)) {
                            responder = m;
                            ++responders;
                        }
                    if (responders == 1 && msg != kEventSetButton)
                        target = field<unsigned>(g_hint.pair[responder].copy, fe::kGuid);
                } else {
                    for (int m = 0; m < g_hint.n; ++m)
                        if (target == field<unsigned>(g_hint.pair[m].orig, fe::kGuid)) {
                            target = field<unsigned>(g_hint.pair[m].copy, fe::kGuid);
                            break;
                        }
                }
                moved += target != before;
            }
            sc = field<void *>(sc, fe::kNext);
            so = so ? field<void *>(so, fe::kNext) : nullptr;
        }
    }
    return moved;
}

void add_help_hint_unguarded(void *screen) {
    const char *pkg_name = field<const char *>(screen, kScreenPackage);
    void *defaults = game::FEngFindGroup(pkg_name, kPcDefaultsHint);
    void *accept = game::FEngFindGroup(pkg_name, kPcAcceptHint);
    if (!defaults) {
        log_line("Help hint: the PC Defaults hint group was not found in %s", pkg_name);
        return;
    }
    void *pkg = game::FindPackage(*reinterpret_cast<void **>(game::kFEng), pkg_name);
    bool parent_is_pkg = false;
    void *parent = pkg ? find_parent(pkg, true, defaults, &parent_is_pkg) : nullptr;
    if (!parent) {
        log_line("Help hint: could not find the Defaults hint's parent");
        return;
    }
    void *clone = game::GroupClone(defaults, 0);
    if (!clone) {
        log_line("Help hint: Clone returned null");
        return;
    }
    g_hint.screen = screen;
    g_hint.n = 0;
    prepare_clone(defaults, clone, 0, true);
    int clicks = 0, targets = 0;
    retarget_responses(&clicks, &targets);
    const int events = retarget_script_events();

    // One hint further along, with the same gap as between the Accept and Defaults hints. Only x is moved:
    // FEngSetCenter would also reset z (depth), which hides the object.
    float dx = 0, dy = 0, ax = 0, ay = 0;
    game::FEngGetCenter(defaults, &dx, &dy);
    float spacing = 120.0f;
    if (accept) {
        game::FEngGetCenter(accept, &ax, &ay);
        const float gap = dx - ax;
        if (gap > 1.0f || gap < -1.0f) spacing = gap;
    }
    if (void *data = field<void *>(clone, fe::kData)) field<float>(data, fe::kDataPosX) += spacing;

    if (parent_is_pkg) game::PackageAddObject(parent, clone);
    else game::GroupAddObject(parent, clone);
    game::BuildMouseObjectStateList(pkg);  // the package's list of hit-boxes, so the clone's is clickable
    g_hint.clone = clone;
    g_hint.pkg = pkg;
    log_line("Help hint added: %d objects, %d click responses, %d response targets and %d script events retargeted",
             g_hint.n, clicks, targets, events);
}

void add_help_hint(void *screen) {
    if (!GetPrivateProfileIntA("features", "help_hint", 1, g_ini)) return;
    if (setjmp(g_guard_jb) == 0) {
        g_guard_on = true;
        add_help_hint_unguarded(screen);
        g_guard_on = false;
    } else {
        g_guard_on = false;
        g_hint.n = 0;
        g_hint.clone = nullptr;
        log_line("Help hint: failed (see nfsmw_custom_menus_crash.log); the rest of the screen is unaffected");
    }
}

unsigned script_id(void *obj) {
    void *sc = field<void *>(obj, fe::kCurrentScript);
    return sc ? field<unsigned>(sc, 0x30) : 0;
}

// Each frame: run the same script as the original (appear, fade out, ...) and copy its alpha, so the clone looks and
// animates like the Defaults hint. Hover is the exception: the hit-box and the label's highlight are the clone's own.
void sync_hint(void *screen) {
    if (g_hint.screen != screen || !g_hint.clone) return;
    for (int i = 0; i < g_hint.n; ++i) {
        const HintPair &p = g_hint.pair[i];
        if (!p.mirrored) continue;
        const unsigned id = script_id(p.orig);
        if (field<int>(p.copy, fe::kType) == fe::kString && (id == kScriptHighlight || id == kScriptUnhighlight)) continue;
        if (id && id != script_id(p.copy)) game::SetScript(p.copy, id, 1);
        void *od = field<void *>(p.orig, fe::kData);
        void *cd = field<void *>(p.copy, fe::kData);
        if (!od || !cd) continue;
        const int alpha = field<int>(od, fe::kDataAlpha);
        if (field<int>(cd, fe::kDataAlpha) != alpha) {
            field<int>(cd, fe::kDataAlpha) = alpha;
            field<unsigned>(p.copy, fe::kFlags) |= fe::kDirtyColor | fe::kDirty;
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Help: the layout has no help panel, so the explanation opens in the game's own message box. Its title and icon
// are adjusted when the box builds itself (feDialogScreen::BuildFromConfig).
// ------------------------------------------------------------------------------------------------
bool g_help_dialog_open = false;
bool g_help_dialog_pending = false;  // the next message box to build is ours

void show_help_for_current_row(void *screen) {
    if (!g_page) return;
    ensure_language_entries();
    void *row = field<void *>(screen, kScreenCurrentRow);
    if (!row) return;
    void **vtbl = field<void **>(row, 0);
    if (vtbl != g_row_vtbl && vtbl != g_selector_vtbl) return;
    const int index = field<int>(row, kRowIndex);
    if (field<PageInfo *>(row, kRowPage) != g_page || index < 0 || index >= g_page->page->row_count) return;
    g_help_dialog_open = true;
    g_help_dialog_pending = true;
    game::ShowOneButton(field<const char *>(screen, kScreenPackage), reinterpret_cast<const char *>(game::kInGameDialogName),
                        kDialogInfo, kDialogOkText, kDialogMessageNo, kDialogMessageNo, g_page->row_help_hash[index]);
}

MethodFn g_orig_dialog_build;
void FASTCALL hook_dialog_build(void *self, void *edx) {
    g_orig_dialog_build(self, edx);
    if (!g_help_dialog_pending || !g_page) return;
    g_help_dialog_pending = false;
    const char *pkg = field<const char *>(self, kScreenPackage);
    game::FEngSetLanguageHashPkg(pkg, kHeaderText, g_page->help_title_hash);
    game::FEPrintfPkg(pkg, static_cast<int>(kHeaderText), "%s", g_page->page->help_title);
    if (GetPrivateProfileIntA("features", "hide_dialog_icon", 1, g_ini)) {
        game::FEngSetInvisiblePkg(pkg, kDialogIcon1);
        game::FEngSetInvisiblePkg(pkg, kDialogIcon2);
    }
}

// The controller's X button opens Help too: with the widescreen fix's gamepad icons, the "2" key glyph on our hint is
// drawn as X.
bool pad_x_down() { return pad_button_down(nfsmw::xinput::kButtonX); }

// The Options layout only forwards the input messages it has responses for (Defaults, Accept, Back, arrows), so a
// "2" key press never reaches this screen as a message. The screen does receive FEMSG_SCREEN_TICK every frame, so
// the Help key (and the controller's X button) is read on those ticks.
using NotifyFn = void(FASTCALL *)(void *, void *, unsigned, void *, unsigned, unsigned);
NotifyFn g_orig_notify = nullptr;
void FASTCALL hook_notify(void *self, void *edx, unsigned msg, void *obj, unsigned p1, unsigned p2) {
    if (g_page) {
        if (msg == kHelpClickMsg || (msg == kButton5Msg && obj && in_hint_clone(obj))) {
            if (!g_help_dialog_open) show_help_for_current_row(self);
            return;
        }
        if (msg == kScreenTick) {
            if (g_hint_pending_screen == self) {
                g_hint_pending_screen = nullptr;
                add_help_hint(self);
            }
            sync_hint(self);
            static bool was_down = false;
            static DWORD last_ms = 0;
            const bool down = (GetAsyncKeyState('2') & 0x8000) || (GetAsyncKeyState(VK_NUMPAD2) & 0x8000) || pad_x_down();
            const DWORD now = GetTickCount();
            if (down && !was_down && !g_help_dialog_open && static_cast<LONG>(now - last_ms) > 400) {
                last_ms = now;
                show_help_for_current_row(self);
            }
            was_down = down;
        } else if (msg == kDialogMessageNo) {
            g_help_dialog_open = false;  // our message box (or the Defaults one) closed
            g_help_dialog_pending = false;
        } else if (msg == kExitComplete && self == g_options_screen) {
            // Left the screen. A discard has already put the values back, so what differs now was accepted.
            if (page_changed() && g_page->page->accept) {
                g_page->page->accept();
                log_line("%s: changes applied", g_page->page->id);
            }
        }
    }
    if (!g_page && msg == kExitComplete && g_player_rows_open && options_category() == kCategoryPlayer) {
        // Left the Player screen: a discard has already put the values back, so what differs now was accepted.
        for (int i = 0; i < g_player_page_count; ++i) {
            const opt::Page &page = *g_player_pages[i]->page;
            if (page.changed && page.changed() && page.accept) {
                page.accept();
                log_line("Options > Player: changes to the rows of \"%s\" applied", page.id);
            }
        }
        g_player_rows_open = false;
    }
    g_orig_notify(self, edx, msg, obj, p1, p2);
}

// ------------------------------------------------------------------------------------------------
// The Options menu entries ("Camera" next to Audio / Video / Gameplay / Player / Controller), pause menu only.
// ------------------------------------------------------------------------------------------------
void THISCALL entry_react(void *self, const char *pkg, unsigned data, void *obj, unsigned p1, unsigned p2) {
    (void)pkg; (void)obj; (void)p1; (void)p2;
    if (data != kButtonPressed) return;
    // Exactly what OMPlayer::React does: select the Player category. UIOptionsMain then opens the Options layout.
    options_category() = kCategoryPlayer;
    g_page_next = field<PageInfo *>(self, kEntryPage);
}

void add_options_entry(void *menu, PageInfo *page) {
    void *opt_object = game::Alloc(kEntryPage + 4);
    if (!opt_object) return;
    game::IconOptionCtor(opt_object, page->icon, page->entry_hash, 0);
    field<void **>(opt_object, 0) = g_entry_vtbl;
    field<PageInfo *>(opt_object, kEntryPage) = page;
    field<unsigned char>(opt_object, 0x45) = 1;  // react immediately, like the game's own entries
    void *row = static_cast<unsigned char *>(menu) + 0x2C;
    void **vtbl = field<void **>(row, 0);
    // Same as IconScrollerMenu::AddOption, keeping the result: the row binds options to UI objects
    // "option_1".."option_N" in the layout and silently drops extras.
    void *img = reinterpret_cast<void *(THISCALL *)(void *, void *)>(vtbl[2])(row, opt_object);
    if (img) game::FEngSetTextureHash(img, page->icon);
    else log_line("Options menu: no free slot for the %s entry", page->page->entry_text);
}

// UIOptionsMain::Setup adds its icons and then lays them out with the row's SetInitialPos (the call is inlined,
// so it cannot be hooked at SetInitialOption). Our entries must be added right before that call.
using SetInitialPosFn = void(FASTCALL *)(void *, void *, int);
SetInitialPosFn g_orig_initialpos;
void FASTCALL hook_initialpos(void *row, void *edx, int index) {
    void *menu = static_cast<unsigned char *>(row) - 0x2C;  // the icon row object lives at menu+0x2C
    if (g_pending_optmain && menu == g_pending_optmain) {
        g_pending_optmain = nullptr;
        for (int i = 0; i < g_page_count; ++i)
            if (g_pages[i].available) add_options_entry(menu, &g_pages[i]);
    }
    g_orig_initialpos(row, edx, index);
}

// A mod's icons live in its own texture pack (a .tpk in the data folder), loaded once, the first time a page needs it.
void load_texture_pack(const char *path) {
    static std::vector<std::string> tried;
    if (!path || !*path || !GetPrivateProfileIntA("features", "texture_pack", 1, g_ini)) return;
    for (const std::string &t : tried)
        if (t == path) return;
    tried.push_back(path);
    nfsmw::load_texture_pack(path, g_log);  // optional: a mod ships one only with icons
}

MethodFn g_orig_optmain_setup;
void FASTCALL hook_optmain_setup(void *self, void *edx) {
    g_page_next = nullptr;
    g_page = nullptr;
    g_pending_optmain = nullptr;
    g_hint_pending_screen = nullptr;
    g_hint.screen = nullptr;
    g_hint.clone = nullptr;
    g_hint.n = 0;
    discover_pages();
    ensure_language_entries();
    if (game::IsPaused(reinterpret_cast<void *>(game::kGameFlowManager))) {
        bool any = false;
        for (int i = 0; i < g_page_count; ++i) {
            PageInfo &p = g_pages[i];
            if (p.page->placement != opt::kNewEntry) continue;  // rows of the Player options, not an entry
            p.available = !p.page->available || p.page->available();
            if (!p.available) continue;
            any = true;
            load_texture_pack(p.page->texture_pack);
            p.icon = p.page->icon && game::GetTextureInfo(p.page->icon, 0, 0) ? p.page->icon : p.page->fallback_icon;
        }
        if (any) g_pending_optmain = self;
    }
    g_orig_optmain_setup(self, edx);
    g_pending_optmain = nullptr;
}

// ------------------------------------------------------------------------------------------------
// Hooks. They are checked first: if another mod changed any of their first bytes, nothing is hooked, because the
// parts depend on each other.
// ------------------------------------------------------------------------------------------------
void install_hooks() {
    init_option_vtables();
    g_entry_vtbl[0] = reinterpret_cast<void *>(game::kIconOptionDelDtor);
    g_entry_vtbl[1] = reinterpret_cast<void *>(&entry_react);
    const nfsmw::Hook hooks[] = {
        {"optmain", "UIOptionsMain::Setup", 0x528F30, {0x6A, 0xFF, 0x68, 0x20, 0xEB, 0x86, 0x00}, true,
         nfsmw::detour(&hook_optmain_setup), nfsmw::orig(&g_orig_optmain_setup)},
        {"initialpos", "IconScroller::SetInitialPos", 0x592C90, {0x51, 0x53, 0x55, 0x56, 0x8B, 0xF1, 0x8B}, true,
         nfsmw::detour(&hook_initialpos), nfsmw::orig(&g_orig_initialpos)},
        {"setupplayer", "UIOptionsScreen::SetupPlayer", 0x529F10, {0x6A, 0xFF, 0x68, 0x80, 0xEE, 0x86, 0x00}, true,
         nfsmw::detour(&hook_setupplayer), nfsmw::orig(&g_orig_setupplayer)},
        {"notify", "UIOptionsScreen::NotificationMessage", 0x545750, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x8B}, true,
         nfsmw::detour(&hook_notify), nfsmw::orig(&g_orig_notify)},
        {"restoredefaults", "UIOptionsScreen::RestoreDefaults", 0x51D170, {0x53, 0x55, 0x8B, 0xE9, 0x8B, 0x0D, 0x90}, true,
         nfsmw::detour(&hook_restoredefaults), nfsmw::orig(&g_orig_restoredefaults)},
        {"didnotchange", "UIOptionsScreen::OptionsDidNotChange", 0x510360, {0x56, 0x8B, 0x35, 0x90, 0xCF, 0x91, 0x00}, true,
         nfsmw::detour(&hook_didnotchange), nfsmw::orig(&g_orig_didnotchange)},
        {"restoreoriginals", "UIOptionsScreen::RestoreOriginals", 0x510450, {0x56, 0x8B, 0x35, 0x90, 0xCF, 0x91, 0x00}, true,
         nfsmw::detour(&hook_restoreoriginals), nfsmw::orig(&g_orig_restoreoriginals)},
        {"dialogbuild", "feDialogScreen::BuildFromConfig", 0x58F720, {0x83, 0xEC, 0x08, 0x56, 0x8B, 0xF1, 0x8A}, true,
         nfsmw::detour(&hook_dialog_build), nfsmw::orig(&g_orig_dialog_build)},
    };
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), g_log, g_ini);
    custom_menus::title_icon::install_hooks(g_log, g_ini);
}

}  // namespace

NFSMW_PLUGIN_MAIN() {
    g_log.open_in_data_folder("nfsmw_custom_menus.log");
    log_line("nfsmw_custom_menus 1.0.0 starting");
    char crash_path[MAX_PATH];
    nfsmw::data_file("nfsmw_custom_menus_crash.log", crash_path, sizeof(crash_path));
    g_crash_file = CreateFileA(crash_path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<void *>(&crash_logger), &mbi, sizeof(mbi))) g_self_base = mbi.AllocationBase;
    crash_write("---- nfsmw_custom_menus 1.0.0 session (tick %lu) ----", GetTickCount());
    AddVectoredExceptionHandler(1, crash_logger);
    nfsmw::data_file("nfsmw_custom_menus.ini", g_ini, sizeof(g_ini));
    nfsmw::xinput::load(&g_log);
    install_hooks();
    return NFSMW_OK;
}
