/*
 * nfsmw_custom_menus: the Options menu framework for speed.exe 1.3 (SHA-256 0C5675A0...2067E342).
 *
 * Other mods add pages to the Options menu (pause menu; the main menu too if the page asks) through this one (see
 * common/custom_menus_api.h): a page is a title, an icon and a list of rows, and this mod builds the screen from the
 * game's own Options layout (Pause_Options.fng / Options.fng, via the
 * Player category's screen with its rows replaced), so it looks and feels native:
 *  - an entry per page in the Options menu, with its icon (from a texture pack of the mod, or a fallback);
 *  - rows that change with left / right (Shift: finer steps), the game's Camera selector as a row kind;
 *  - like the game's own options, edits take effect at once and are kept on Accept; Back offers to discard them;
 *    Defaults (1) resets the page after the game's confirmation;
 *  - a "2 Help" hint (a clone of the layout's Defaults hint) and the 2 key (X on a controller) explain the
 *    highlighted row in the game's message box.
 * Every plugin that exports nfsmw_custom_menus_provide gets asked for its pages the first time the Options menu opens.
 *
 * Files in <game>/scripts/mets_mods/: nfsmw_custom_menus.ini (settings), nfsmw_custom_menus.log.
 * ini settings, all optional:
 *   [features] fewer_popups=0, skip_career_menu=0, skippable_cutscenes=0  turn those parts off
 *   [hooks]    <name>=0  skips one hook (names in install_hooks)
 *
 * Offsets come from the PC disassembly; the decomp's struct offsets are from the console builds and differ.
 */

#include <algorithm>
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
#include "resume_career.h"
#include "skip_dialogs.h"
#include "skip_movies.h"
#include "translations.h"
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
// The Help-hint code runs under a guard: an access violation inside it is skipped (the rest of the screen works without the hint)
// and logged. Faults anywhere else are not ours and pass through (nfsmw_dev_tools' diagnostics logs those).
// ------------------------------------------------------------------------------------------------
jmp_buf g_guard_jb;
volatile bool g_guard_on = false;
unsigned long g_fault_eip = 0, g_fault_address = 0;  // of the last fault the guard caught

LONG WINAPI guard_handler(EXCEPTION_POINTERS *e) {
    if (!g_guard_on || e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    g_fault_eip = e->ContextRecord->Eip;
    g_fault_address = static_cast<unsigned long>(e->ExceptionRecord->ExceptionInformation[1]);
    g_guard_on = false;
    longjmp(g_guard_jb, 1);
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
const auto FEngSetScriptOn = at<void (*)(void *object, unsigned script_hash, bool restart)>(0x514D10);  // FEngSetScript(FEObject *, hash, bool)
const auto FindImage = at<void *(*)(const char *pkg, unsigned name_hash)>(0x5251A0);  // FEngFindImage
const auto FEHashUpper = at<unsigned (*)(const char *)>(0x5AF1C0);
const auto GroupClone = at<void *(THISCALL *)(void *group, int reference)>(0x5C49C0);
const auto GroupAddObject = at<void(THISCALL *)(void *group, void *object)>(0x5B5420);
const auto PackageAddObject = at<void(THISCALL *)(void *pkg, void *object)>(0x50DB90);
const auto BuildMouseObjectStateList = at<void(THISCALL *)(void *pkg)>(0x5B88F0);
const auto SetScript = at<void(THISCALL *)(void *object, unsigned id, int force)>(0x5BC160);  // FEObject::SetScript

// Option screens and their rows
const auto ClearWidgets = at<void(THISCALL *)(void *screen)>(0x588C90);
const auto RefreshWidgets = at<void(THISCALL *)(void *screen)>(0x588D20);  // Draw() on every row
const auto SetInitialOption = at<void(THISCALL *)(void *screen, int number)>(0x5A5EA0);  // UIWidgetMenu::SetInitialOption: the cursor on widget `number` (1-based)
const auto AddToggleOption = at<unsigned(THISCALL *)(void *screen, void *widget, int enabled)>(0x588570);
const auto AddSliderOption = at<unsigned(THISCALL *)(void *screen, void *widget, int enabled)>(0x588860);
const auto SliderCtor = at<void *(THISCALL *)(void *widget, int enabled)>(0x589540);  // FESliderWidget
const auto SetSliderValues = at<void(THISCALL *)(void *widget, float min, float max, float inc, float cur)>(0x50E660);
const auto SetSliderValue = at<void(THISCALL *)(void *widget, float value)>(0x50E6A0);
const auto DrawSlider = at<void(THISCALL *)(void *widget)>(0x50E6B0);
const auto ToggleWidgetCtor = at<void *(THISCALL *)(void *widget, int enabled)>(0x589300);  // FEToggleWidget, 0x5C bytes
const auto DriveCamCtor = at<void *(THISCALL *)(void *widget, int enabled)>(0x51AA90);  // PODriveCam, 0x5C bytes
const auto DriveCamAct = at<void(THISCALL *)(void *widget, const char *pkg, unsigned data)>(0x51C870);
const auto IconOptionCtor = at<void(THISCALL *)(void *option, unsigned tex, unsigned name, unsigned desc)>(0x586FA0);
constexpr uintptr_t kToggleDelDtor = 0x589370;      // FEToggleWidget scalar deleting destructor
constexpr uintptr_t kIconOptionDelDtor = 0x51A500;  // IconOption scalar deleting destructor
constexpr uintptr_t kDriveCamVtable = 0x89C048;     // 15 slots: [0] dtor [1] Act [3] Draw [14] BlinkArrows
constexpr uintptr_t kSliderVtable = 0x8A1490;       // FESliderWidget: those, and [15] SetInitialValues (called when it is added)

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
constexpr unsigned kRowIndex = 0xA0;    // ours: the row's index in its page, after the widget (FESliderWidget is the largest, 0x9D bytes)
constexpr unsigned kRowPage = kRowIndex + 4;  // ours: the page (PageInfo) the row belongs to
constexpr unsigned kRowSize = kRowPage + 4;
constexpr unsigned kScreenFromPause = 0x124;  // UIOptionsScreen::mCalledFromPauseMenu (byte)
constexpr int kGameIconSlots = 4 + 8;  // the scroller's four end markers, and the most entries the game adds itself (7, 8 online)
constexpr unsigned kIconScrollerCurrent = 0x38;  // IconScrollerMenu: the current entry (IconOption*)
constexpr unsigned kOptionObject = 0x10;        // IconOption: its FEng image
constexpr unsigned kEntryPage = 0x5C;   // ours: the page of an Options menu entry (IconOption, 0x5C bytes)

// Hashes: FEng objects, messages, scripts, textures, language strings.
constexpr unsigned kHeaderText = 0x42ADB44C;  // HEADER_TEXT: the title of a screen or message box
constexpr unsigned kDialogIcon1 = 0x0D4F4069, kDialogIcon2 = 0xFAC88427;  // the message box's exclamation mark
// The Options layout has two hint bars: console hints (hidden on PC) and the PC hints that are shown, top-level
// groups of {label string, clickable hit-box group, key image}.
constexpr unsigned kPcDefaultsHint = 0x3FC72F9D;  // "1 Defaults"
constexpr unsigned kPcAcceptHint = 0x9502D148;    // "Enter Accept"
// The main menu's layouts (Options.fng, OptionsPCDisplay.fng) have one set of hints, named differently; the same kinds of objects
// (label, backing image, hit-box image, key glyph) in a group each.
constexpr unsigned kMainDefaultsHint = 0xD6463100, kMainAcceptHint = 0x73DCB662;
constexpr unsigned kHintName = 0xCA3E00A0;        // our clone's name; its objects get kHintName + 0x100 + i
constexpr unsigned kHintGuidBase = 0xCA3E2000;
constexpr unsigned kHelpClickMsg = 0xCA3E0B01;    // what a click on our hint posts to the screen

constexpr unsigned kButton5Msg = 0xC519BFC4;  // __PAD_BUTTON5__: the 1 key (Defaults)
constexpr unsigned kButtonPressed = 0x0C407210;
constexpr unsigned kPadLeft = 0x9120409E, kPadRight = 0xB5971BF1;
constexpr unsigned kPadUp = 0x72619778, kPadDown = 0x911C0A4B;
constexpr unsigned kVideoBasicMsg = 0x9CDD83CA;  // the Video screen: advanced <-> basic options (its "Basic" hint, the X key)
constexpr unsigned kScreenTick = 0xC98356BA;    // FEMSG_SCREEN_TICK: every frame
constexpr unsigned kExitComplete = 0xE1FDE1D1;  // FEHASH_EXITCOMPLETE: the screen finished leaving
constexpr unsigned kDialogMessageNo = 0x34DC1BCF, kDialogMessageYes = 0xD05FC3A3;
constexpr unsigned kDialogOkText = 0x417B2601;
constexpr int kDialogInfo = 2;                    // eDialogTitle dialog_info
constexpr unsigned kEventSetButton = 0x1B3909AA;  // script event that moves the menu cursor
constexpr unsigned kScriptHighlight = 0x249DB7B7, kScriptUnhighlight = 0x7AB5521A;
constexpr int kCategoryVideo = 1, kCategoryPlayer = 4;  // OptionsSettings::CurrentCategory
// The Player screen's layout (Pause_Options.fng) has 10 row slots. The Video screen's (Pause_PCDisplayOptions.fng, the PC "Advanced Video"
// one) has 15, and a scroll bar with chevrons, so pages with more rows than the first holds are built on the second.
constexpr int kPlayerSlots = 10;

constexpr unsigned kPcKey1Texture = 0x68A29A8A, kPcKey2Texture = 0x68A29A8B, kPcKey3Texture = 0x68A29A8C, kPcKey4Texture = 0x68A29A8D;
constexpr unsigned kCircleTexture = 0x54A68051;  // drawn as the 1 key on PC

// ------------------------------------------------------------------------------------------------
// Pages: what the mods register. Everything we show is a language string of the game (the game's labels, titles and
// dialogs all take a hash), so each page's texts get hashes from their names and are merged into the language table.
// ------------------------------------------------------------------------------------------------
struct PageInfo {
    const opt::Page *page;
    unsigned entry_hash, title_hash, help_title_hash;
    std::vector<unsigned> row_label_hash;  // 0 for rows that use the game's own label
    std::vector<unsigned> row_help_hash;
    unsigned icon;      // the texture shown: page->icon when the game has it, else the fallback
    bool available;     // as of the last time the Options menu opened
    int category;       // the Options category whose screen the page is built on
};
bool g_video_hook = false;  // SetupVideo is hooked: pages with many rows can use the Video screen

std::deque<PageInfo> g_pages;  // stable addresses: the entries and screens point at them
int g_page_count = 0;
std::deque<std::string> g_texts;  // the strings behind the language entries (stable addresses)
std::vector<LangEntry> g_entries;
bool g_discovered = false;
opt::Host g_host;  // what the mods are given (and keep: open_page is used later)
void open_page(const char *id, bool in_game, void (*on_exit)());
void (*g_exit_callback)() = nullptr;  // the open page was opened by open_page: where it goes when left

unsigned add_text(const char *id, const char *what, int row, const char *text) {
    char name[96];
    if (row >= 0) snprintf(name, sizeof(name), "NFSMW_CUSTOM_MENUS.%s.%d.%s", id, row, what);
    else snprintf(name, sizeof(name), "NFSMW_CUSTOM_MENUS.%s.%s", id, what);
    const unsigned hash = nfsmw::bstring_hash(name);
    g_texts.emplace_back(custom_menus::translations::translate(text));
    g_entries.push_back({hash, g_texts.back().c_str()});
    return hash;
}

// The texture an entry shows: the page's own icon, else its fallback, else the Options icon (two cogs) of whichever menu is up.
unsigned shown_icon(const opt::Page &page) {
    constexpr unsigned kPauseOptionsIcon = 0x520DE4E3, kMainOptionsIcon = 0x3058FE37;
    for (unsigned icon : {page.icon, page.fallback_icon, kPauseOptionsIcon, kMainOptionsIcon})
        if (icon && game::GetTextureInfo(icon, 0, 0)) return icon;
    return page.fallback_icon;
}

void register_page(const opt::Page *page) {
    if (!page || !page->id || !page->rows || page->row_count <= 0) {
        log_line("register_page: page \"%s\" rejected (no id or rows)", page && page->id ? page->id : "?");
        return;
    }
    PageInfo &p = g_pages.emplace_back();
    g_page_count = static_cast<int>(g_pages.size());
    p.page = page;
    p.category = page->row_count > kPlayerSlots && g_video_hook ? kCategoryVideo : kCategoryPlayer;
    p.entry_hash = add_text(page->id, "entry", -1, page->entry_text);
    p.title_hash = add_text(page->id, "title", -1, page->title);
    p.help_title_hash = add_text(page->id, "help_title", -1, page->help_title);
    p.row_label_hash.resize(page->row_count);
    p.row_help_hash.resize(page->row_count);
    for (int r = 0; r < page->row_count; ++r) {
        const opt::Row &row = page->rows[r];
        if (row.kind != opt::kCameraSelectorRow && !row.label_hash) p.row_label_hash[r] = add_text(page->id, "label", r, row.label);
        else p.row_label_hash[r] = row.label_hash;
        p.row_help_hash[r] = add_text(page->id, "help", r, row.help);
    }
    log_line("page \"%s\": %d rows", page->id, page->row_count);
}

// Asks every loaded plugin for its pages, once. They are all loaded by the time the Options menu opens.
void discover_pages() {
    if (g_discovered) return;
    g_discovered = true;
    g_host = {&register_page, &custom_menus::controls_rows::add_rows, &custom_menus::controls_rows::add_text,
              &custom_menus::translations::add_folder, &custom_menus::controls_rows::set_binding_label,
              &custom_menus::controls_rows::add_refresh, &custom_menus::controls_rows::add_controller_defaults, &open_page};
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
            provide(&g_host);
        }
    }
    CloseHandle(snapshot);
    log_line("%d page(s) registered", g_page_count);
    log_line("translations: %d texts for the game's language", custom_menus::translations::count());
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
void *g_slider_vtbl[16];    // our slider rows (FESliderWidget behaviour with our Act/Draw/SetInitialValues)
void *g_selector_vtbl[15];  // PODriveCam with Act wrapped so the other rows refresh when the view changes
void *g_entry_vtbl[2];      // Options menu entries

int &options_category() { return field<int>(field<void *>(*reinterpret_cast<void **>(game::kFEDatabase), 0x10), 0x24); }

// A button of the first XInput controller, read directly (no controller_support needed).
bool pad_button_down(WORD button) {
    nfsmw::xinput::Gamepad pad;
    return nfsmw::xinput::read_any(&pad) && (pad.buttons & button) != 0;  // any controller (read is the first one only)
}

// A page with more rows than the layout has slots shows a window of them: the widgets are the slots, and slot k shows the page's row
// g_row_offset + k (see handle_window_scroll). kRowIndex of a widget is its slot then, otherwise its row.
bool g_windowed = false;
int g_page_category = kCategoryPlayer;  // the category of the open page's screen
int g_row_offset = 0;
int g_window_rows = 0;   // the page's rows, when windowed
int g_window_slots = 0;  // the widgets (slots) it shows them in

// The screen's own scroll bar (UIWidgetMenu's FEScrollBar member at +0x54; the Video layout has the objects, at +0x40 / +0x44 of it the track and
// the handle). The game updates it itself when it scrolls, for the widgets there are; a windowed page has more rows than widgets, so we tell it the
// rows after each up / down: Update(rows in view, rows, first row in view, selected row), 1-based.
const auto ScrollBarUpdate = reinterpret_cast<void(THISCALL *)(void *bar, int view, int list, int head, int selected)>(0x589870);
const auto GetWidgetIndex = reinterpret_cast<unsigned(THISCALL *)(void *screen, void *widget)>(0x588D60);  // 1-based
const auto GetWidget = reinterpret_cast<void *(THISCALL *)(void *screen, unsigned index)>(0x588280);         // 1-based
const auto Reposition = reinterpret_cast<void(THISCALL *)(void *screen)>(0x59BFF0);  // shows the widgets from the view top (pViewTop, +0x38), hides the rest

bool row_is_enabled(const PageInfo *page, int row);

// The rows the window scrolls through: greyed out rows at the end of the page (past the last one the cursor can reach) do not count, so a
// page whose last rows are greyed out neither scrolls nor shows a scroll bar that says it could.
int last_enabled_row() {
    int last = -1;
    for (int r = 0; r < g_window_rows; ++r)
        if (row_is_enabled(g_page, r)) last = r;
    return last;
}
int window_list_rows() { return std::max(g_window_slots, last_enabled_row() + 1); }

void update_scroll_bar(void *screen) {
    if (!g_windowed) return;
    unsigned char *bar = static_cast<unsigned char *>(screen) + 0x54;
    if (!field<void *>(bar, 0x40) || !field<void *>(bar, 0x44)) return;  // the layout has none
    void *top = field<void *>(screen, 0x38), *current = field<void *>(screen, 0x34);
    const int top_index = top ? static_cast<int>(GetWidgetIndex(screen, top)) : 1;
    const int current_index = current ? static_cast<int>(GetWidgetIndex(screen, current)) : top_index;
    // The screen shows 10 widgets at a time (9 in the main menu: UIOptionsScreen's constructor, +0x10C), however many slots the layout has.
    const int shown = field<int>(screen, 0x10C);
    ScrollBarUpdate(bar, shown, std::max(shown, last_enabled_row() + 1), g_row_offset + top_index, g_row_offset + current_index);
}

int row_of(void *widget) {
    const int index = field<int>(widget, kRowIndex);
    return g_windowed && field<PageInfo *>(widget, kRowPage) == g_page ? index + g_row_offset : index;
}

// Keeps the row's widget enabled or disabled as the page says (FEWidget::bEnabled at +0x28; the game's Enable / Disable are vtable slots
// 7 / 8: a disabled row plays the GREY script and UIWidgetMenu::Scroll goes past it).
constexpr unsigned kRowEnabled = 0x28;
bool row_is_enabled(const PageInfo *page, int row) { return !page->page->row_enabled || page->page->row_enabled(row); }

// Called after the row's texts were set. Enabling plays UNHIGHLIGHT on the row's objects, which restores the label but not the value:
// the layouts give the value only the scripts INIT (its normal look) and GREY, so INIT is played on it again (the arrows have INIT
// and HIDE and cannot go grey).
//
// A slider row: FESliderWidget::Enable only sets the flag (the label would stay grey), so the toggle's Enable runs instead, which plays the
// label's script; and the bar (BASE_SLIDER_n / FILLBAR_SLIDER_n, n the slot) gets the GREY script if it has one, UNHIGHLIGHT again when enabled.
void apply_row_enabled(void *self, const PageInfo *page, int row) {
    if (!page->page->row_enabled) return;
    const bool want = page->page->row_enabled(row);
    if (want == (field<unsigned char>(self, kRowEnabled) != 0)) return;
    void **vtbl = field<void **>(self, 0);
    const bool slider = vtbl == g_slider_vtbl;
    reinterpret_cast<DrawFn>((slider && want ? g_row_vtbl : vtbl)[want ? 7 : 8])(self);
    if (want) game::FEngSetScriptOn(field<void *>(self, kRowData), nfsmw::bstring_hash("INIT"), true);
    if (slider && field<PageInfo *>(self, kRowPage) == g_page) {
        const char *pkg = field<const char *>(g_options_screen, kScreenPackage);
        const auto FindObject = reinterpret_cast<void *(*)(const char *, unsigned)>(0x524850);  // FEngFindObject
        for (const char *name : {"BASE_SLIDER_%d", "FILLBAR_SLIDER_%d"}) {
            char text[32];
            snprintf(text, sizeof(text), name, field<int>(self, kRowIndex) + 1);
            if (void *bar = FindObject(pkg, nfsmw::bstring_hash(text)))
                game::FEngSetScriptOn(bar, nfsmw::bstring_hash(want ? "UNHIGHLIGHT" : "GREY"), true);
        }
    }
}

void THISCALL value_row_act(void *self, const char *pkg, unsigned data) {
    (void)pkg;
    if (data != kPadLeft && data != kPadRight) return;
    const int row = row_of(self);
    const PageInfo *page = field<PageInfo *>(self, kRowPage);
    if (!page || row < 0 || row >= page->page->row_count || !row_is_enabled(page, row)) return;
    if (page->page->change) page->page->change(row, data == kPadRight ? 1 : -1, (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 || pad_button_down(nfsmw::xinput::kLeftShoulder));
    field<unsigned char>(self, kRowChanged) = 1;  // as PODriveCam::Act does
    // Other rows may be greyed out or not now.
    if (page->page->row_enabled && g_options_screen && field<PageInfo *>(self, kRowPage) == g_page) game::RefreshWidgets(g_options_screen);
    void **vtbl = field<void **>(self, 0);
    reinterpret_cast<BlinkFn>(vtbl[14])(self, data);  // arrow blink
    reinterpret_cast<DrawFn>(vtbl[3])(self);
}

void THISCALL value_row_draw(void *self) {
    const int row = row_of(self);
    const PageInfo *page = field<PageInfo *>(self, kRowPage);
    if (!page || row < 0 || row >= page->page->row_count) return;
    // The label goes in as a language hash, like PODriveCam::Draw does for "Camera". FEStatWidget::Position
    // centres a label on the column edge using the width of its literal text, which a language-hash label leaves
    // out, so the game's labels end at the edge (right-aligned). Text set with FESetString was measured and centred.
    game::FEngSetLanguageHash(field<void *>(self, kRowTitle), page->row_label_hash[row]);
    char text[64] = {};
    const unsigned hash = page->page->value ? page->page->value(row, text, sizeof(text)) : 0;
    if (hash) game::FEngSetLanguageHash(field<void *>(self, kRowData), hash);
    else set_wide_text(field<void *>(self, kRowData), custom_menus::translations::translate(text));
    apply_row_enabled(self, page, row);
}

// A slider row: the bar is the layout's BASE_SLIDER_n / FILLBAR_SLIDER_n for the row's slot, which the game's widget finds by itself.
void THISCALL slider_row_draw(void *self) {
    const int row = row_of(self);
    const PageInfo *page = field<PageInfo *>(self, kRowPage);
    if (!page || row < 0 || row >= page->page->row_count) return;
    game::FEngSetLanguageHash(field<void *>(self, kRowTitle), page->row_label_hash[row]);
    game::SetSliderValue(self, page->page->fill(row));
    game::DrawSlider(self);
    apply_row_enabled(self, page, row);
}

void THISCALL slider_row_initial_values(void *self) {
    const int row = row_of(self);
    const PageInfo *page = field<PageInfo *>(self, kRowPage);
    game::SetSliderValues(self, 0.0f, 1.0f, 0.1f, page && row >= 0 && row < page->page->row_count ? page->page->fill(row) : 0.0f);
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
    void **slider = reinterpret_cast<void **>(game::kSliderVtable);
    for (int i = 0; i < 16; ++i) g_slider_vtbl[i] = slider[i];
    g_slider_vtbl[1] = reinterpret_cast<void *>(&value_row_act);
    g_slider_vtbl[3] = reinterpret_cast<void *>(&slider_row_draw);
    g_slider_vtbl[15] = reinterpret_cast<void *>(&slider_row_initial_values);
}

// The layout's row slots: its OPTION_NAME_1 .. OPTION_NAME_n strings, which the widgets take as their title objects in order. A widget past the
// last has none, and focusing it crashes the game. How many more rows the screen can take: the slots, less the widgets it has (UIWidgetMenu: the
// next index to add is at +0x104).
int layout_slots(const char *pkg) {
    const auto FindObject = reinterpret_cast<void *(*)(const char *, unsigned)>(0x524850);  // FEngFindObject
    int n = 0;
    char name[32];
    for (; n < 64; ++n) {
        snprintf(name, sizeof(name), "OPTION_NAME_%d", n + 1);
        if (!FindObject(pkg, nfsmw::bstring_hash(name))) break;
    }
    return n;
}

int free_row_slots(void *screen) {
    const int slots = layout_slots(field<const char *>(screen, kScreenPackage)), used = field<int>(screen, 0x104) - 1;
    return slots > used ? slots - used : 0;
}

// The layout's slider bars: BASE_SLIDER_n and FILLBAR_SLIDER_n for the row slot n (the Player and Video layouts, in both menus, have them).
bool layout_has_sliders(const char *pkg) {
    const auto FindObject = reinterpret_cast<void *(*)(const char *, unsigned)>(0x524850);  // FEngFindObject
    return FindObject(pkg, nfsmw::bstring_hash("BASE_SLIDER_1")) && FindObject(pkg, nfsmw::bstring_hash("FILLBAR_SLIDER_1"));
}

// Adds the page's row `r` as the screen's next row; `index` is what kRowIndex gets: the row, or the slot in a windowed page. A slider row is
// a value row when it can't be a bar (no `fill`, no bars in the layout).
void add_page_row(void *screen, PageInfo *info, int index, int r, bool bars) {
    const opt::Row &row = info->page->rows[r];
    void *w = game::Alloc(kRowSize);
    if (!w) return;
    const bool slider = bars && row.kind == opt::kSliderRow && info->page->fill;
    if (row.kind == opt::kCameraSelectorRow) {  // the game's own Camera selector
        game::DriveCamCtor(w, 1);
        field<void **>(w, 0) = g_selector_vtbl;
    } else if (slider) {
        game::SliderCtor(w, 1);
        field<void **>(w, 0) = g_slider_vtbl;
    } else {
        game::ToggleWidgetCtor(w, 1);
        field<void **>(w, 0) = g_row_vtbl;
    }
    field<int>(w, kRowIndex) = index;
    field<PageInfo *>(w, kRowPage) = info;
    (slider ? game::AddSliderOption : game::AddToggleOption)(screen, w, 1);
}

void build_page_screen(void *screen) {
    const opt::Page &page = *g_page->page;
    g_options_screen = screen;
    g_page_category = options_category();
    if (page.opened) page.opened();
    game::ClearWidgets(screen);  // drop the rows SetupPlayer / SetupVideo just added
    const int room = free_row_slots(screen);
    bool has_selector = false;
    for (int r = 0; r < page.row_count; ++r) has_selector |= page.rows[r].kind == opt::kCameraSelectorRow;
    // More rows than slots: the slots show a window of them. Not with a Camera selector row (the game's own widget, which
    // shows one fixed row) or the layout's Done button (the game then leaves the list at its ends).
    g_windowed = page.row_count > room && room >= 2 && !has_selector && !field<void *>(screen, 0x4C);
    g_row_offset = 0;
    g_window_rows = page.row_count;
    g_window_slots = room;
    if (g_windowed)
        log_line("page \"%s\": %d rows in a window of %d", page.id, page.row_count, room);
    else if (page.row_count > room)
        log_line("page \"%s\" has %d rows but the screen has room for %d: the rest are left out", page.id, page.row_count, room);
    const int widgets = g_windowed ? room : (page.row_count < room ? page.row_count : room);
    const bool bars = layout_has_sliders(field<const char *>(screen, kScreenPackage));
    for (int r = 0; r < widgets; ++r) add_page_row(screen, g_page, r, r, bars);
    // The title object is a group of strings (text and shadow). Set the hash the way the game's own screens do, then
    // also print into the group so it does not depend on the table lookup.
    const char *pkg = field<const char *>(screen, kScreenPackage);
    game::FEngSetLanguageHashPkg(pkg, kHeaderText, g_page->title_hash);
    game::FEPrintfPkg(pkg, static_cast<int>(kHeaderText), "%s", custom_menus::translations::translate(page.title));
    custom_menus::title_icon::set(pkg, g_page->icon);
    game::RefreshWidgets(screen);
    g_hint_pending_screen = screen;  // the Help hint is cloned on the first tick, once the layout is live
    log_line("Options > %s built", page.entry_text);
    update_scroll_bar(screen);
}

// Rows that mods add to the game's own Player options (Placement kPlayerOptions): after the game's rows, in the pages
// that were available when the screen was built. Back, Defaults and Accept ask these pages as well as the game.
std::vector<PageInfo *> g_player_pages;
int g_player_page_count = 0;
bool g_player_rows_open = false;  // the open Player screen has such rows

void add_player_rows(void *screen) {
    g_player_pages.clear();
    g_player_page_count = 0;
    g_player_rows_open = false;
    for (int i = 0; i < g_page_count; ++i) {
        PageInfo &info = g_pages[i];
        const opt::Page &page = *info.page;
        if (page.placement != opt::kPlayerOptions || (page.available && !page.available())) continue;
        if (page.opened) page.opened();
        const int room = free_row_slots(screen);
        if (page.row_count > room)
            log_line("Options > Player: \"%s\" has %d rows but the screen has room for %d: the rest are left out", page.id, page.row_count, room);
        const bool bars = layout_has_sliders(field<const char *>(screen, kScreenPackage));
        for (int r = 0; r < page.row_count && r < room; ++r) add_page_row(screen, &info, r, r, bars);
        g_player_pages.push_back(&info);
        g_player_page_count = static_cast<int>(g_player_pages.size());
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
    g_windowed = false;
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

// The Video screen with a page's rows instead of the game's (SetupVideo is what adds the video options; the screen is the one Options > Video
// opens, so only the pages that were opened for it take it over).
MethodFn g_orig_setupvideo;
void FASTCALL hook_setupvideo(void *self, void *edx) {
    g_orig_setupvideo(self, edx);
    if (!g_page_next || options_category() != kCategoryVideo) return;
    g_windowed = false;
    g_page = g_page_next;
    g_page_next = nullptr;
    ensure_language_entries();
    g_player_rows_open = false;
    build_page_screen(self);
    // The Video layout's brightness text ("Adjust your display brightness control until you can clearly see two logos...", language string
    // 0xAFA456AC): the string 0x545570C6 in the group 0xAD6B204F, whose appear script SetupVideo plays. Hidden, and emptied in case a script shows it.
    const char *pkg = field<const char *>(self, kScreenPackage);
    game::FEngSetInvisiblePkg(pkg, 0xAD6B204F);
    game::FEngSetInvisiblePkg(pkg, 0x545570C6);
    game::FEPrintfPkg(pkg, static_cast<int>(0x545570C6), "%s", "");
    // Its "Basic" hint (X), which switches between the advanced and the basic video options (message kVideoBasicMsg, also refused in hook_notify):
    // the hint group 0x72958E0E and its hit-box 0x56E65DA8. Our Help hint takes its place.
    game::FEngSetInvisiblePkg(pkg, 0x72958E0E);
    game::FEngSetInvisiblePkg(pkg, 0x56E65DA8);
}

bool page_changed() { return g_page && g_page->page->changed && g_page->page->changed(); }

// Back asks "discard changes?" only when this says something changed; the page's values count as well.
BoolMethodFn g_orig_didnotchange;
bool FASTCALL hook_didnotchange(void *self, void *edx) {
    const bool unchanged = g_orig_didnotchange(self, edx);
    // On the Video screen the game applies the display settings (and asks "keep them?") when this says they changed: from Accept (0x545831) and from
    // leaving (0x545B47). A page has none, so there it is always "unchanged"; the page's changes are accepted on leaving.
    if (g_page && self == g_options_screen && g_page_category == kCategoryVideo) {
        const uintptr_t caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
        if (caller == 0x545831 || caller == 0x545B47) return true;
    }
    if (g_page && self == g_options_screen && page_changed()) return false;
    if (!g_page && player_rows_changed()) return false;
    return unchanged;
}

// The discard was confirmed: the game puts its own values back; put the page's back too.
MethodFn g_orig_restoreoriginals;
void FASTCALL hook_restoreoriginals(void *self, void *edx) {
    const bool player_rows = !g_page && player_rows_changed();
    // The Video screen would put its display settings back (and reset the renderer); a page has none.
    if (!(g_page && self == g_options_screen && g_page_category == kCategoryVideo)) g_orig_restoreoriginals(self, edx);
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
    if (g_page && options_category() == g_page_category) {
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
    return tex == kPcKey1Texture || tex == kPcKey4Texture || tex == kCircleTexture || tex == kPcKey2Texture || tex == kPcKey3Texture;
}

// What a clone needs from its original (see below).
void copy_object_state(void *orig, void *copy) {
    field<void *>(copy, fe::kCached) = nullptr;
    void *od = field<void *>(orig, fe::kData);
    void *cd = field<void *>(copy, fe::kData);
    const unsigned osz = field<unsigned>(orig, fe::kDataSize), csz = field<unsigned>(copy, fe::kDataSize);
    const unsigned n = osz < csz ? osz : csz;
    if (od && cd && od != cd && n >= 0x44 && n <= 0x400) memcpy(cd, od, n);
    else log_line("  cloned object %p: data not copied (orig %p/%u, copy %p/%u)", copy, od, osz, cd, csz);
    field<unsigned>(copy, fe::kFlags) |= fe::kDirtyColor | fe::kDirtyTransform | fe::kDirty;
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
    copy_object_state(orig, copy);

    const int type = field<int>(copy, fe::kType);
    if (type == fe::kString) {  // the label
        constexpr unsigned kHelpText = 0x61D8F34F;  // the game's own "Help"
        const char *help = nfsmw::language_text(kHelpText);
        game::FEPrintf(copy, "%s", help ? help : "Help");
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
        defaults = game::FEngFindGroup(pkg_name, kMainDefaultsHint);
        accept = game::FEngFindGroup(pkg_name, kMainAcceptHint);
    }
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
    if (setjmp(g_guard_jb) == 0) {
        g_guard_on = true;
        add_help_hint_unguarded(screen);
        g_guard_on = false;
    } else {
        g_guard_on = false;
        g_hint.n = 0;
        g_hint.clone = nullptr;
        log_line("Help hint: failed (access violation at eip=%#lx, address %#lx); the rest of the screen is unaffected", g_fault_eip,
                 g_fault_address);
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
    if (vtbl != g_row_vtbl && vtbl != g_selector_vtbl && vtbl != g_slider_vtbl) return;
    const int index = row_of(row);
    if (field<PageInfo *>(row, kRowPage) != g_page || index < 0 || index >= g_page->page->row_count) return;
    g_help_dialog_open = true;
    g_help_dialog_pending = true;
    // The pause menu's message box layout, or the front end's (what the screen itself asks for: UIOptionsScreen::mCalledFromPauseMenu).
    const char *dialog = field<unsigned char>(screen, kScreenFromPause) ? reinterpret_cast<const char *>(game::kInGameDialogName) : "Dialog.fng";
    game::ShowOneButton(field<const char *>(screen, kScreenPackage), dialog,
                        kDialogInfo, kDialogOkText, kDialogMessageNo, kDialogMessageNo, g_page->row_help_hash[index]);
}

MethodFn g_orig_dialog_build;
void FASTCALL hook_dialog_build(void *self, void *edx) {
    g_orig_dialog_build(self, edx);
    if (!g_help_dialog_pending || !g_page) return;
    g_help_dialog_pending = false;
    const char *pkg = field<const char *>(self, kScreenPackage);
    game::FEngSetLanguageHashPkg(pkg, kHeaderText, g_page->help_title_hash);
    game::FEPrintfPkg(pkg, static_cast<int>(kHeaderText), "%s", custom_menus::translations::translate(g_page->page->help_title));
    game::FEngSetInvisiblePkg(pkg, kDialogIcon1);
    game::FEngSetInvisiblePkg(pkg, kDialogIcon2);
}

// The controller's X button opens Help too: with the widescreen fix's gamepad icons, the "2" key glyph on our hint is
// drawn as X.
bool pad_x_down() { return pad_button_down(nfsmw::xinput::kButtonX); }

// The Options layout only forwards the input messages it has responses for (Defaults, Accept, Back, arrows), so a
// "2" key press never reaches this screen as a message. The screen does receive FEMSG_SCREEN_TICK every frame, so
// the Help key (and the controller's X button) is read on those ticks.
using NotifyFn = void(FASTCALL *)(void *, void *, unsigned, void *, unsigned, unsigned);
NotifyFn g_orig_notify = nullptr;

// A window of rows after it slid: a slider bar and a value are different widgets, so the slots' widgets are made again for the rows now in view,
// and the cursor goes to its slot.
//
// The screen shows `shown` (10) of its widgets at a time, from the view top (the game scrolls the rest into view as the cursor moves). The new
// widgets would start with the view top on the cursor's widget (SetInitialOption), which scrolls for no reason: the view top is kept instead,
// moved only as far as the cursor needs.
void rebuild_window(void *screen, int cursor_slot) {
    void *top = field<void *>(screen, 0x38);
    int view = top ? static_cast<int>(GetWidgetIndex(screen, top)) - 1 : 0;  // the first widget in view, 0-based
    const int shown = field<int>(screen, 0x10C);
    game::ClearWidgets(screen);
    const bool bars = layout_has_sliders(field<const char *>(screen, kScreenPackage));
    for (int k = 0; k < g_window_slots; ++k) add_page_row(screen, g_page, k, g_row_offset + k, bars);
    game::SetInitialOption(screen, cursor_slot + 1);
    if (cursor_slot < view) view = cursor_slot;
    else if (cursor_slot >= view + shown) view = cursor_slot - shown + 1;
    view = std::min(std::max(view, 0), std::max(g_window_slots - shown, 0));
    if (void *widget = GetWidget(screen, view + 1)) {
        field<void *>(screen, 0x38) = widget;
        Reposition(screen);
    }
    game::RefreshWidgets(screen);
}

// Up / down in a windowed page. The next row the cursor can go to is worked out here, with rows that are greyed out skipped and the ends of the
// page wrapping around. The game moves the focus itself when that row is a widget further along in the window (it skips greyed out widgets
// the same way). Otherwise the window slides to put the row in view (at the slot nearest to where the cursor was coming from), the widgets
// are made again, and the cursor goes to the row. Returns true when the message was handled here.
bool handle_window_scroll(void *screen, unsigned msg) {
    void *current = field<void *>(screen, 0x34);  // UIWidgetMenu::pCurrentOption
    if (!current || (field<void **>(current, 0) != g_row_vtbl && field<void **>(current, 0) != g_slider_vtbl)) return false;
    const int slots = g_window_slots, rows = g_window_rows, list = window_list_rows();
    const int direction = msg == kPadDown ? 1 : -1;
    const int row = field<int>(current, kRowIndex) + g_row_offset;
    int target = row;
    bool wrapped = false, found = false;
    for (int step = 0; step < rows - 1 && !found; ++step) {
        target += direction;
        if (target >= rows) target = 0, wrapped = true;
        if (target < 0) target = rows - 1, wrapped = true;
        found = row_is_enabled(g_page, target);
    }
    if (!found) return false;
    const bool in_view = target >= g_row_offset && target < g_row_offset + slots;
    if (in_view && !wrapped) return false;  // the game's own step lands on it
    int offset = g_row_offset;
    if (target < offset) offset = target;
    else if (target >= offset + slots) offset = target - slots + 1;
    offset = std::min(std::max(offset, 0), list - slots);
    g_row_offset = offset;
    rebuild_window(screen, target - offset);
    update_scroll_bar(screen);
    return true;
}

// Called every frame on a windowed page: the rows that can be reached may have become fewer (Vibration turned off greys out the rows below it), and
// the window then goes back to where the last of them is in view. Not from the row's own change, which is running in a widget that is about to
// be made again.
void fit_window(void *screen) {
    const int max_offset = window_list_rows() - g_window_slots;
    if (g_row_offset <= max_offset) return;
    void *current = field<void *>(screen, 0x34);
    const int slot = current ? field<int>(current, kRowIndex) : 0;
    const int row = slot + g_row_offset;
    g_row_offset = max_offset;
    rebuild_window(screen, std::min(std::max(row - g_row_offset, 0), g_window_slots - 1));
    update_scroll_bar(screen);
}

bool g_redirect_switch = false;  // the screen switch the page's exit makes goes to g_exit_callback instead

// cFEng::QueuePackageSwitch: the page that was opened by a mod (open_page) leaves through the mod's callback.
using QueueSwitchHookFn = void(FASTCALL *)(void *, void *, const char *, int, unsigned, bool);
QueueSwitchHookFn g_orig_queue_switch = nullptr;
void FASTCALL hook_queue_switch(void *feng, void *edx, const char *pkg, int arg, unsigned msg, bool flag) {
    if (custom_menus::resume_career::swallow_switch(pkg)) return;
    pkg = custom_menus::resume_career::redirect_switch(pkg);
    if (g_redirect_switch && g_exit_callback) {
        void (*go)() = g_exit_callback;
        g_redirect_switch = false;
        g_exit_callback = nullptr;
        log_line("page left: back to the screen that opened it instead of \"%s\"", pkg ? pkg : "?");
        go();
        return;
    }
    g_orig_queue_switch(feng, edx, pkg, arg, msg, flag);
}
void FASTCALL hook_notify(void *self, void *edx, unsigned msg, void *obj, unsigned p1, unsigned p2) {
    // The Video screen's switch to its basic options (its hidden "Basic" hint, the X key) does nothing on a page.
    if (g_page && self == g_options_screen && g_page_category == kCategoryVideo && msg == kVideoBasicMsg) return;
    if (g_page && g_windowed && self == g_options_screen && (msg == kPadUp || msg == kPadDown)) {
        if (handle_window_scroll(self, msg)) return;
    }
    if (g_page) {
        if (self == g_options_screen && (msg == kButton5Msg || msg == kDialogMessageYes))
            log_line("%s: %s", g_page->page->id, msg == kButton5Msg ? "Defaults pressed" : "Defaults confirmed");
        if (msg == kHelpClickMsg || (msg == kButton5Msg && obj && in_hint_clone(obj))) {
            if (!g_help_dialog_open) show_help_for_current_row(self);
            return;
        }
        if (msg == kScreenTick) {
            if (g_windowed && self == g_options_screen) fit_window(self);
            if (g_hint_pending_screen == self) {
                g_hint_pending_screen = nullptr;
                add_help_hint(self);
                update_scroll_bar(self);
            }
            sync_hint(self);
            static bool was_down = false;
            static DWORD last_ms = 0;
            const bool down = (GetAsyncKeyState('2') & 0x8000) || (GetAsyncKeyState(VK_NUMPAD2) & 0x8000) || pad_x_down();
            const DWORD now = GetTickCount();
            if (down && !was_down && !g_help_dialog_open && static_cast<LONG>(now - last_ms) > 400) {
                last_ms = now;
                log_line("Help requested (%s)", (GetAsyncKeyState('2') & 0x8000) || (GetAsyncKeyState(VK_NUMPAD2) & 0x8000) ? "key 2" : "controller X");
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
                // The game marks the profile's options as changed when it leaves (so that it is saved); on the Video screen it does not.
                if (g_page_category == kCategoryVideo) field<unsigned char>(*reinterpret_cast<void **>(game::kFEDatabase), 4) = 1;
            }
            // Opened by a mod: the game's own exit still runs (it marks the profile's options as changed), but the screen switch
            // it ends in is the mod's to make (hook_queue_switch).
            g_redirect_switch = g_exit_callback != nullptr;
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
    g_redirect_switch = false;
    // The game's own scrolling tells the scroll bar about its widgets only (hiding it, there being no more than fit).
    if (g_page && g_windowed && self == g_options_screen && (msg == kPadUp || msg == kPadDown || msg == kScreenTick)) update_scroll_bar(self);
}

// ------------------------------------------------------------------------------------------------
// The Options menu entries ("Camera" next to Audio / Video / Gameplay / Player / Controller), pause menu only.
// ------------------------------------------------------------------------------------------------
void THISCALL entry_react(void *self, const char *pkg, unsigned data, void *obj, unsigned p1, unsigned p2) {
    (void)pkg; (void)obj; (void)p1; (void)p2;
    if (data != kButtonPressed) return;
    // What OMPlayer::React does: select the category (the page's: Player, or Video for a long page). UIOptionsMain then opens its screen.
    PageInfo *page = field<PageInfo *>(self, kEntryPage);
    options_category() = page->category;
    g_page_next = page;
    g_exit_callback = nullptr;
}

void add_options_entry(void *menu, PageInfo *page) {
    void *opt_object = game::Alloc(kEntryPage + 4);
    if (!opt_object) return;
    game::IconOptionCtor(opt_object, page->icon, page->entry_hash, 0);
    field<void **>(opt_object, 0) = g_entry_vtbl;
    field<PageInfo *>(opt_object, kEntryPage) = page;
    field<unsigned char>(opt_object, 0x45) = 1;  // react immediately, like the game's own entries
    // The constructor gives the speed trap and drag icons their tutorial movie (a "View Tutorial" button); ours never have one.
    field<unsigned char>(opt_object, 0x46) = 0;
    field<unsigned>(opt_object, 0x48) = 0;
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

// The main menu's Options also has EA Trax and Credits, which our entries go before. The game adds them with AddOption; the
// first of them (EA Trax) is where ours are inserted. Pause menu (and online) lists have neither, and take them at the end.
using AddOptionFn = void(FASTCALL *)(void *, void *, void *);
AddOptionFn g_orig_addoption;
void FASTCALL hook_addoption(void *menu, void *edx, void *option) {
    constexpr unsigned kEaTraxIcon = 0xC52CCBF6, kCreditsIcon = 0x51009E20;
    const unsigned icon = field<unsigned>(option, 0xC);
    custom_menus::resume_career::option_added(menu, option);
    if (g_pending_optmain && menu == g_pending_optmain && (icon == kEaTraxIcon || icon == kCreditsIcon)) {
        g_pending_optmain = nullptr;
        for (int i = 0; i < g_page_count; ++i)
            if (g_pages[i].available) add_options_entry(menu, &g_pages[i]);
    }
    g_orig_addoption(menu, edx, option);
}

// A press on the icon strip is reported for the object the layout's response names. On a cloned slot that is the original's
// object (the response's target is not the GUID, so it cannot be retargeted), which the strip does not accept for the clone's
// entry, so the press is reported for the entry that is current: a press is always for that one.
NotifyFn g_orig_optmain_notify = nullptr;
constexpr unsigned kSlotGuidBase = 0xCA3E3000;  // GUIDs of the cloned slots
void FASTCALL hook_optmain_notify(void *self, void *edx, unsigned msg, void *obj, unsigned p1, unsigned p2) {
    if (msg == kButtonPressed) {
        void *current = field<void *>(self, kIconScrollerCurrent);
        void *current_obj = current ? field<void *>(current, kOptionObject) : nullptr;
        if (current_obj && obj != current_obj) {
            const unsigned guid = field<unsigned>(current_obj, fe::kGuid);
            if (guid >= kSlotGuidBase && guid < kSlotGuidBase + 0x100) obj = current_obj;
        }
    }
    g_orig_optmain_notify(self, edx, msg, obj, p1, p2);
}

// A mod's icons live in its own texture pack (a .tpk in the data folder), loaded once, the first time a page needs it.
void load_texture_pack(const char *path) {
    static std::vector<std::string> tried;
    if (!path || !*path) return;
    for (const std::string &t : tried)
        if (t == path) return;
    tried.push_back(path);
    nfsmw::load_texture_pack(path, g_log);  // optional: a mod ships one only with icons
}

// Opens a page from another screen the way UIOptionsMain does when one of its entries is pressed (its NotificationMessage's switch on
// the category): select the category, then switch to that category's layout.
using QueueSwitchFn = void(THISCALL *)(void *feng, const char *pkg, int arg, unsigned msg, bool flag);
void open_page(const char *id, bool in_game, void (*on_exit)()) {
    PageInfo *page = nullptr;
    for (int i = 0; i < g_page_count; ++i)
        if (strcmp(g_pages[i].page->id, id) == 0) page = &g_pages[i];
    if (!page) {
        log_line("open_page: no page \"%s\"", id);
        return;
    }
    load_texture_pack(page->page->texture_pack);
    page->icon = shown_icon(*page->page);
    options_category() = page->category;
    g_page_next = page;
    g_exit_callback = on_exit;
    // The pause menu's layout, unless the database says (bits 3 and 6 of its +0x12C) the screens come from the frontend.
    const unsigned flags = field<unsigned>(*reinterpret_cast<void **>(game::kFEDatabase), 0x12C);
    const bool pause_layout = in_game && !(flags & (1u << 3)) && !(flags & (1u << 6));
    log_line("open_page: \"%s\" (%s layout, %s screen)", id, pause_layout ? "pause menu" : "main menu",
             page->category == kCategoryVideo ? "Video" : "Player");
    const char *pkg = page->category == kCategoryVideo ? (pause_layout ? "Pause_PCDisplayOptions.fng" : "OptionsPCDisplay.fng")
                                                       : (pause_layout ? "Pause_Options.fng" : "Options.fng");
    reinterpret_cast<QueueSwitchFn>(0x525940)(*reinterpret_cast<void **>(game::kFEng), pkg, pause_layout ? 1 : 0, 0, false);
}

// The Options menu's icon strip is the layout's images OPTION_1.. OPTION_n (four of them are the scroller's end markers), and the game
// drops every entry past the last. More are cloned from the last one (unused ones are hidden in the layout, and so are the clones).
void ensure_icon_slots(const char *pkg_name, int needed) {
    char name[24];
    int count = 0;
    void *last = nullptr;
    for (;;) {
        snprintf(name, sizeof(name), "OPTION_%d", count + 1);
        void *img = game::FindImage(pkg_name, game::FEHashUpper(name));
        if (!img) break;
        last = img;
        ++count;
    }
    void *pkg = game::FindPackage(*reinterpret_cast<void **>(game::kFEng), pkg_name);
    if (!last || !pkg || count >= needed) return;
    bool parent_is_pkg = false;
    void *parent = find_parent(pkg, true, last, &parent_is_pkg);
    if (!parent) {
        log_line("Options menu: could not find the parent of the icon slots");
        return;
    }
    void *(THISCALL *clone_fn)(void *, int) = reinterpret_cast<void *(THISCALL *)(void *, int)>(field<void **>(last, 0)[1]);
    for (int i = count + 1; i <= needed; ++i) {
        void *copy = clone_fn(last, 0);
        if (!copy) break;
        snprintf(name, sizeof(name), "OPTION_%d", i);
        copy_object_state(last, copy);
        field<unsigned>(copy, fe::kNameHash) = game::FEHashUpper(name);
        field<unsigned>(copy, fe::kGuid) = kSlotGuidBase + i;
        // Into the same parent as the original (the layout may keep its icons in a group).
        if (parent_is_pkg) game::PackageAddObject(parent, copy);
        else game::GroupAddObject(parent, copy);
    }
    log_line("Options menu: the layout has %d icon slots, %d added for the entries", count, needed - count);
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
    const bool paused = game::IsPaused(reinterpret_cast<void *>(game::kGameFlowManager));
    bool any = false;
    for (int i = 0; i < g_page_count; ++i) {
        PageInfo &p = g_pages[i];
        // Rows of the Player options and hidden pages are not entries; the main menu gets only the pages that ask for it.
        const bool entry = p.page->placement == opt::kNewEntryEverywhere || (paused && p.page->placement == opt::kNewEntry);
        p.available = false;
        if (!entry) continue;
        p.available = !p.page->available || p.page->available();
        if (!p.available) continue;
        any = true;
        load_texture_pack(p.page->texture_pack);
        p.icon = shown_icon(*p.page);
    }
    if (any) g_pending_optmain = self;
    if (any) {
        int ours = 0;
        for (int i = 0; i < g_page_count; ++i) ours += g_pages[i].available;
        ensure_icon_slots(field<const char *>(self, kScreenPackage), ours + kGameIconSlots);
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
        {"addoption", "IconScrollerMenu::AddOption", 0x573960, {0x8B, 0x41, 0x2C, 0x56, 0x8B, 0x74, 0x24}, false,
         nfsmw::detour(&hook_addoption), nfsmw::orig(&g_orig_addoption)},
        {"optmain_notify", "UIOptionsMain::NotificationMessage", 0x545310, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A}, false,
         nfsmw::detour(&hook_optmain_notify), nfsmw::orig(&g_orig_optmain_notify)},
        {"setupplayer", "UIOptionsScreen::SetupPlayer", 0x529F10, {0x6A, 0xFF, 0x68, 0x80, 0xEE, 0x86, 0x00}, true,
         nfsmw::detour(&hook_setupplayer), nfsmw::orig(&g_orig_setupplayer)},
        {"setupvideo", "UIOptionsScreen::SetupVideo", 0x5295F0, {0x6A, 0xFF, 0x64, 0xA1, 0x00, 0x00, 0x00}, false,
         nfsmw::detour(&hook_setupvideo), nfsmw::orig(&g_orig_setupvideo)},
        {"queue_switch", "cFEng::QueuePackageSwitch", 0x525940, {0x83, 0x3D, 0x90, 0x5E, 0x92, 0x00, 0x06}, false,
         nfsmw::detour(&hook_queue_switch), nfsmw::orig(&g_orig_queue_switch)},
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
    g_video_hook = g_orig_setupvideo != nullptr;
    custom_menus::title_icon::install_hooks(g_log, g_ini);
    custom_menus::skip_dialogs::install_hooks(g_log, g_ini);
    custom_menus::resume_career::install_hooks(g_log, g_ini);
    custom_menus::skip_movies::install_hooks(g_log, g_ini);
}

}  // namespace

NFSMW_PLUGIN_MAIN() {
    g_log.open_in_data_folder("nfsmw_custom_menus.log");
    log_line("nfsmw_custom_menus 1.0.0 starting");
    AddVectoredExceptionHandler(1, guard_handler);
    nfsmw::data_file("nfsmw_custom_menus.ini", g_ini, sizeof(g_ini));
    nfsmw::xinput::load(&g_log);
    install_hooks();
    return NFSMW_OK;
}
