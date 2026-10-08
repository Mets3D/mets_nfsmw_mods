/*
 * nfsmw_cam: per-preset camera settings in a native Options screen, for speed.exe 1.3
 * (SHA-256 0C5675A0...2067E342).
 *
 *  - CameraAnchor::GetPov builds the active camera preset (POV) from the car's attributes every frame. Our
 *    per-preset overrides are applied to what it returns, so switching views with the game's hotkey keeps them.
 *  - Pause > Options gets a "Camera" entry, with its icon from our own texture pack (nfsmw_cam.tpk). It opens the
 *    game's own Options layout: row 1 is the game's Camera selector, the rows below edit the selected view (hold
 *    Shift for finer steps). Like the game's own options, edits are kept on Accept and Back offers to discard them;
 *    Defaults (1) resets the view after the game's confirmation. A "2 Help" hint (a clone of the Defaults hint) and
 *    the 2 key explain the highlighted row in the game's message box.
 *  - The Drift camera's name ("DRIFT", from NFSMWExtraOptions) is shown with the game's own "Drift" string.
 *
 * Files next to speed.exe: nfsmw_cam.ini (overrides and settings), nfsmw_cam.log, nfsmw_cam_crash.log.
 * ini settings, all optional:
 *   [general]  degree_symbol=0  writes " deg" instead of the degree sign
 *              icon=<texture name, or its hash as 8 hex digits>  the Options icon (default OPTIONS_CAMERA)
 *   [features] help_hint=0, hide_dialog_icon=0, texture_pack=0  turn those parts off
 *   [hooks]    <name>=0  skips one hook (names in install_hooks)
 *
 * Offsets come from the PC disassembly; the decomp's struct offsets are from the console builds and differ.
 */

#include <nfsmw_sdk/nfsmw_sdk.h>
#include <MinHook.h>

#include <csetjmp>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

NFSMW_PLUGIN_DECLARE("NFSMW Camera Editor", "1.0.0", "Demeter Dzadik")

#define THISCALL __attribute__((thiscall))
// FASTCALL (__fastcall) comes from windows.h.

namespace {

FILE *g_log = nullptr;

void log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_line(const char *fmt, ...) {
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

template <typename T>
T &field(void *base, unsigned off) {
    return *reinterpret_cast<T *>(static_cast<unsigned char *>(base) + off);
}

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
    if (mbi.AllocationBase == g_self_base) return "nfsmw_cam.asi";
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
const auto GetPOVTypeFromPlayerCamera = at<int (*)(int)>(0x56EBC0);
const auto LoadResourceFile = at<void *(*)(const char *, int type, int flags, void *callback, void *param, int offset,
                                           int size)>(0x661760);  // returns ResourceFile*
const auto ServiceResourceLoading = at<int (*)()>(0x6626B0);  // advances queued loads
const auto GetTextureInfo = at<void *(*)(unsigned hash, int return_default, int include_unloaded)>(0x503400);

// Strings and images
const auto FEngSetLanguageHash = at<void (*)(void *string, unsigned hash)>(0x515C00);
const auto FEngSetLanguageHashPkg = at<void (*)(const char *pkg, unsigned object, unsigned hash)>(0x525220);
const auto FEPrintf = at<int (*)(void *string, const char *fmt, ...)>(0x515D70);
const auto FEPrintfPkg = at<int (*)(const char *pkg, int object, const char *fmt, ...)>(0x5252D0);  // strings and groups
const auto FESetString = at<void (*)(void *string, const unsigned short *text)>(0x515CC0);
const auto FEngSetTextureHash = at<void (*)(void *image, unsigned hash)>(0x515B90);
const auto FEngSetTextureHashPkg = at<void (*)(const char *pkg, unsigned object, unsigned hash)>(0x527EE0);
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
constexpr uintptr_t kLangTable = 0x91CF80;       // LangEntry*: sorted {hash, text} table of the loaded language
constexpr uintptr_t kLangCount = 0x91CF78;       // its number of entries
constexpr uintptr_t kFEDatabase = 0x91CF90;      // FEDatabase*; +0x10 = options settings
constexpr uintptr_t kPlayerToEdit = 0x91CA24;    // UIOptionsScreen::PlayerToEdit
constexpr uintptr_t kGameFlowManager = 0x925E70;
constexpr uintptr_t kFEng = 0x91CADC;            // cFEng*
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
constexpr unsigned kLabelHash = 0x60;      // FEString
constexpr unsigned kPackageObjects = 0x4C;  // FEPackage: head of its top-level objects

constexpr unsigned kDataAlpha = 0x0C;
constexpr unsigned kDataPosX = 0x1C;

constexpr int kImage = 1, kString = 2, kGroup = 5;
constexpr unsigned kMouseObject = 1u << 17;  // hit-box: the package tracks the mouse over it
constexpr unsigned kDirtyColor = 1u << 23, kDirtyTransform = 1u << 24, kDirty = 1u << 25;
}  // namespace fe

// UIWidgetMenu (option screens) and its rows (FEStatWidget / FEToggleWidget).
constexpr unsigned kScreenPackage = 0x10;      // const char* package name
constexpr unsigned kScreenRows = 0x2C;         // row list head (rows link through row+4)
constexpr unsigned kScreenCurrentRow = 0x34;
constexpr unsigned kRowChanged = 0x2A;         // set by the game's rows when their value changes
constexpr unsigned kRowTitle = 0x2C;           // FEString* label
constexpr unsigned kRowData = 0x30;            // FEString* value
constexpr unsigned kRowParam = 0x5C;           // ours: parameter index, after the 0x5C-byte FEToggleWidget

// Hashes: FEng objects, messages, scripts, textures, language strings.
constexpr unsigned kHeaderText = 0x42ADB44C;    // HEADER_TEXT: the title of a screen or message box
constexpr unsigned kHeaderIcon = 0x08007B4C;    // the Options screen's title icon
constexpr unsigned kDialogIcon1 = 0x0D4F4069, kDialogIcon2 = 0xFAC88427;  // the message box's exclamation mark
// The Options layout has two hint bars: console hints (hidden on PC) and the PC hints that are shown, top-level
// groups of {label string, clickable hit-box group, key image}.
constexpr unsigned kPcDefaultsHint = 0x3FC72F9D;  // "1 Defaults"
constexpr unsigned kPcAcceptHint = 0x9502D148;    // "Enter Accept"
constexpr unsigned kHintName = 0xCA3E00A0;        // our clone's name; its objects get kHintName + 0x100 + i
constexpr unsigned kHintGuidBase = 0xCA3E2000;
constexpr unsigned kHelpClickMsg = 0xCA3E0B01;    // what a click on our hint posts to the screen

constexpr unsigned kButton5Msg = 0xC519BFC4;    // __PAD_BUTTON5__: the 1 key (Defaults)
constexpr unsigned kButtonPressed = 0x0C407210;
constexpr unsigned kPadLeft = 0x9120409E, kPadRight = 0xB5971BF1;
constexpr unsigned kScreenTick = 0xC98356BA;    // FEMSG_SCREEN_TICK: every frame
constexpr unsigned kExitComplete = 0xE1FDE1D1;  // FEHASH_EXITCOMPLETE: the screen finished leaving
constexpr unsigned kDialogMessageNo = 0x34DC1BCF;
constexpr unsigned kDialogOkText = 0x417B2601;
constexpr int kDialogInfo = 2;                  // eDialogTitle dialog_info
constexpr unsigned kEventSetButton = 0x1B3909AA;  // script event that moves the menu cursor
constexpr unsigned kScriptHighlight = 0x249DB7B7, kScriptUnhighlight = 0x7AB5521A;
constexpr int kCategoryPlayer = 4;              // OptionsSettings::CurrentCategory

constexpr unsigned kPcKey1Texture = 0x68A29A8A, kPcKey2Texture = 0x68A29A8B, kPcKey4Texture = 0x68A29A8D;
constexpr unsigned kCircleTexture = 0x54A68051;   // drawn as the 1 key on PC
constexpr unsigned kCustomIconTexture = 0x12F6B313;  // OPTIONS_CAMERA, in our nfsmw_cam.tpk
constexpr unsigned kSpeedtrapTexture = 0x66C9A7B6;   // MILESTONE_SPEEDTRAP (always loaded): the fallback icon
constexpr unsigned kDriftUpper = 0xA1211428;  // "DRIFT", NFSMWExtraOptions' name for the Drift camera
constexpr unsigned kDrift = 0x6AEB7EE6;       // "Drift", the game's own string

// Our language strings, merged into the game's table.
constexpr unsigned kOptEntryText = 0xCA3E0003;  // "Camera": the Options entry
constexpr unsigned kTitleText = 0xCA3E0030;     // "CAMERA": the screen title
constexpr unsigned kDialogTitleText = 0xCA3E0031;  // "CAMERA INFO": the help box title
constexpr unsigned kHelpTextBase = 0xCA3E0040;  // + parameter index; + NPARAM = the camera selector row
constexpr unsigned kLabelTextBase = 0xCA3E0060;  // + parameter index: row labels

// ------------------------------------------------------------------------------------------------
// Camera presets
// ------------------------------------------------------------------------------------------------
// POV as built by GetPov at CameraAnchor+0xC8 (PC layout).
struct Pov {
    short type;            // 0x00  0..6
    unsigned short angle;  // 0x02  16-bit angle, 65536 = 360 deg (signed wrap)
    float lag;             // 0x04
    float height;          // 0x08
    float lat_offset;      // 0x0C
    unsigned short fov;    // 0x10  16-bit angle
    short pad;             // 0x12
    float stiffness;       // 0x14
    short tilting;         // 0x18
};

enum Param { P_FOV, P_HEIGHT, P_LAG, P_ANGLE, P_LAT, P_STIFF, P_TILT, NPARAM };
constexpr int kNumPresets = 7;

const char *kPresetName[kNumPresets] = {"bumper", "hood", "close", "far", "super_far", "drift", "pursuit"};
const char *kKey[NPARAM] = {"fov", "height", "lag", "angle", "lat", "stiffness", "tilting"};
const char *kLabel[NPARAM] = {"FOV", "Height", "Lag / Distance", "Pitch", "Lateral Offset", "Stiffness", "Tilting"};

// Help texts (2 key on the Camera options screen). Wording for lag, pitch, lateral offset and tilting is
// inferred from the field names and values, so adjust here if a description does not match what you see.
const char *kHelp[NPARAM + 1] = {
    "Field of view in degrees. Higher values show more of the scene and exaggerate the feeling of speed. "
    "Lower values zoom in. Hold Shift for finer steps.",
    "How high the camera sits above the car. Higher values give a more top-down view. Hold Shift for finer steps.",
    "Camera lag distance. Negative values place the camera behind the car, and larger negative values pull it "
    "further back. Values near zero place it at the car. Hold Shift for finer steps.",
    "Tilts the camera up or down, in degrees. Hold Shift for finer steps.",
    "Moves the camera sideways, away from the center line of the car. Hold Shift for finer steps.",
    "How tightly the camera follows the car. Higher values keep it close to the car, lower values let it "
    "drift and lag behind. Hold Shift for finer steps.",
    "Lets the camera lean with the car as it rolls and turns.",
    "Selects the camera view whose settings you are editing. This is the same setting as the Camera option "
    "under Player.",
};

struct Range {
    float min, max, step, fine;  // fine: the step while Shift is held; values are kept on this grid
};
const Range kRange[NPARAM] = {
    {20.0f, 140.0f, 5.0f, 1.0f},    // FOV
    {0.0f, 8.0f, 0.25f, 0.05f},     // height
    {-30.0f, 3.0f, 0.5f, 0.1f},     // lag (negative = behind the car)
    {-20.0f, 20.0f, 0.5f, 0.1f},    // pitch
    {-3.0f, 3.0f, 0.25f, 0.05f},    // lateral
    {0.0f, 1.0f, 0.05f, 0.01f},     // stiffness
    {-1.0f, 1.0f, 2.0f, 2.0f},      // tilting off/on
};

struct Override {
    bool set[NPARAM];
    float val[NPARAM];
};

Override g_ovr[kNumPresets];       // the overrides in effect
Override g_ovr_open[kNumPresets];  // g_ovr when Options > Camera opened: what was last applied (and is in the ini)
float g_base[kNumPresets][NPARAM];  // the game's own values, captured before overriding
bool g_have_base[kNumPresets];
int g_cur_type = 3;                // preset most recently requested from GetPov
void *g_anchor = nullptr;          // CameraAnchor passed to GetPov (valid while a race is loaded)

bool g_degree_sign = true;              // [general] degree_symbol
unsigned g_icon = kCustomIconTexture;   // [general] icon
unsigned g_icon_shown = kSpeedtrapTexture;  // g_icon if the game has that texture, else the built-in camera icon

float ang_to_deg(unsigned short a) { return static_cast<short>(a) * 360.0f / 65536.0f; }
unsigned short deg_to_ang(float d) {
    return static_cast<unsigned short>(static_cast<int>(lroundf(d * 65536.0f / 360.0f)));
}

float get_field(const Pov &p, int i) {
    switch (i) {
        case P_FOV: return p.fov * 360.0f / 65536.0f;
        case P_HEIGHT: return p.height;
        case P_LAG: return p.lag;
        case P_ANGLE: return ang_to_deg(p.angle);
        case P_LAT: return p.lat_offset;
        case P_STIFF: return p.stiffness;
        default: return p.tilting ? 1.0f : 0.0f;
    }
}

void apply_field(Pov *p, int i, float v) {
    switch (i) {
        case P_FOV: p->fov = deg_to_ang(v); break;
        case P_HEIGHT: p->height = v; break;
        case P_LAG: p->lag = v; break;
        case P_ANGLE: p->angle = deg_to_ang(v); break;
        case P_LAT: p->lat_offset = v; break;
        case P_STIFF: p->stiffness = v; break;
        default: p->tilting = v >= 0.5f ? 1 : 0; break;
    }
}

float effective(int type, int i) {
    if (g_ovr[type].set[i]) return g_ovr[type].val[i];
    return g_have_base[type] ? g_base[type][i] : 0.0f;
}

// Like the game's own options: edits take effect at once, but only count as applied when the screen is left with
// Accept. Back asks before discarding (it sees camera_changed()), and a confirmed discard restores g_ovr_open.
bool camera_changed() {
    for (int t = 0; t < kNumPresets; ++t)
        for (int i = 0; i < NPARAM; ++i) {
            if (g_ovr[t].set[i] != g_ovr_open[t].set[i]) return true;
            if (g_ovr[t].set[i] && std::fabs(g_ovr[t].val[i] - g_ovr_open[t].val[i]) > 1e-4f) return true;
        }
    return false;
}

// ---- persistence ----
char g_ini[MAX_PATH];

void ini_path_init() {
    GetModuleFileNameA(nullptr, g_ini, sizeof(g_ini));
    char *slash = strrchr(g_ini, '\\');
    if (slash) *(slash + 1) = 0;
    strncat(g_ini, "nfsmw_cam.ini", sizeof(g_ini) - strlen(g_ini) - 1);
}

void ini_load() {
    int loaded = 0;
    for (int t = 0; t < kNumPresets; ++t) {
        for (int i = 0; i < NPARAM; ++i) {
            char buf[64] = {};
            GetPrivateProfileStringA(kPresetName[t], kKey[i], "", buf, sizeof(buf), g_ini);
            if (buf[0]) {
                g_ovr[t].set[i] = true;
                g_ovr[t].val[i] = static_cast<float>(atof(buf));
                ++loaded;
            }
        }
    }
    log_line("loaded %d override value(s) from %s", loaded, g_ini);

    g_degree_sign = GetPrivateProfileIntA("general", "degree_symbol", 1, g_ini) != 0;

    // A texture's name (hashed like the game: h = h * 33 + c from 0xFFFFFFFF) or its hash as 8 hex digits.
    // Texture names longer than 23 characters are cut short in the game files, so use the hash for those.
    char icon[64] = {};
    GetPrivateProfileStringA("general", "icon", "", icon, sizeof(icon), g_ini);
    char *end = nullptr;
    const unsigned long hex = strtoul(icon, &end, 16);
    if (strlen(icon) == 8 && end && !*end) {
        g_icon = hex;
    } else if (icon[0]) {
        unsigned h = 0xFFFFFFFFu;
        for (const char *c = icon; *c; ++c) h = h * 33 + static_cast<unsigned char>(toupper(*c));
        g_icon = h;
    }
    log_line("icon texture %08x%s%s", g_icon, icon[0] ? " from the ini: " : " (default)", icon);
}

void ini_save() {
    for (int t = 0; t < kNumPresets; ++t) {
        for (int i = 0; i < NPARAM; ++i) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", g_ovr[t].val[i]);
            WritePrivateProfileStringA(kPresetName[t], kKey[i], g_ovr[t].set[i] ? buf : nullptr, g_ini);
        }
    }
}

// ---- GetPov hook: apply the overrides to the preset the game just built ----
using GetPovFn = Pov *(FASTCALL *)(void *, void *, int);
GetPovFn g_orig_getpov = nullptr;

Pov *FASTCALL hook_getpov(void *self, void *edx, int type) {
    Pov *p = g_orig_getpov(self, edx, type);
    if (p && type >= 0 && type < kNumPresets) {
        g_cur_type = type;
        g_anchor = self;
        float now[NPARAM];
        for (int i = 0; i < NPARAM; ++i) now[i] = get_field(*p, i);
        if (!g_have_base[type] || memcmp(now, g_base[type], sizeof(now)) != 0) {
            static unsigned logged = 0;
            if (logged++ < 200)
                log_line("base %-9s fov=%.1f height=%.3f lag=%.3f pitch=%.1f lat=%.3f stiff=%.3f tilt=%d",
                         kPresetName[type], now[P_FOV], now[P_HEIGHT], now[P_LAG], now[P_ANGLE],
                         now[P_LAT], now[P_STIFF], p->tilting);
            memcpy(g_base[type], now, sizeof(now));
            g_have_base[type] = true;
        }
        for (int i = 0; i < NPARAM; ++i)
            if (g_ovr[type].set[i]) apply_field(p, i, g_ovr[type].val[i]);
    }
    return p;
}

// ------------------------------------------------------------------------------------------------
// Text: our strings are language hashes. Every reader (labels, titles, dialogs, scrollers) resolves a hash with one
// binary search over the game's sorted {hash, text} table, so we merge our own entries into that table and the
// game's own code does the rest.
// ------------------------------------------------------------------------------------------------
struct LangEntry {
    unsigned hash;
    const char *text;
};

LangEntry *g_merged_table = nullptr;

void ensure_language_entries() {
    LangEntry *cur = *reinterpret_cast<LangEntry **>(game::kLangTable);
    if (!cur || cur == g_merged_table) return;  // not loaded yet, or already merged
    const unsigned n = *reinterpret_cast<unsigned *>(game::kLangCount);

    LangEntry ours[3 + (NPARAM + 1) + NPARAM];
    unsigned m = 0;
    ours[m++] = {kOptEntryText, "Camera"};
    ours[m++] = {kTitleText, "CAMERA"};
    ours[m++] = {kDialogTitleText, "CAMERA INFO"};
    for (int i = 0; i < NPARAM + 1; ++i) ours[m++] = {kHelpTextBase + static_cast<unsigned>(i), kHelp[i]};
    for (int i = 0; i < NPARAM; ++i) ours[m++] = {kLabelTextBase + static_cast<unsigned>(i), kLabel[i]};
    for (unsigned i = 1; i < m; ++i)  // insertion sort by hash (unsigned): the game's search is unsigned
        for (unsigned j = i; j > 0 && ours[j - 1].hash > ours[j].hash; --j) {
            const LangEntry t = ours[j];
            ours[j] = ours[j - 1];
            ours[j - 1] = t;
        }

    LangEntry *merged = static_cast<LangEntry *>(game::Alloc((n + m) * sizeof(LangEntry)));
    if (!merged) return;
    unsigned a = 0, b = 0, o = 0;
    while (a < n || b < m) {
        if (b >= m || (a < n && cur[a].hash <= ours[b].hash)) merged[o++] = cur[a++];
        else merged[o++] = ours[b++];
    }
    *reinterpret_cast<unsigned *>(game::kLangCount) = n + m;
    *reinterpret_cast<LangEntry **>(game::kLangTable) = merged;
    g_merged_table = merged;
    log_line("language table: merged %u entries into %u", m, n);
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
// Options > Camera: a category screen built from the game's own Options layout (Pause_Options.fng).
// It reuses the "Player" category (SetupPlayer) so the screen, its Defaults/Accept/Back buttons and the
// "settings changed?" logic stay native, then swaps the Player rows for ours:
//   row 1  the game's own Camera selector (PODriveCam, changes the player's camera view)
//   row 2+ one row per preset value, editing the preset that the selector currently points at
// ------------------------------------------------------------------------------------------------
using MethodFn = void(FASTCALL *)(void *, void *);  // a hooked thiscall method without arguments
using BoolMethodFn = bool(FASTCALL *)(void *, void *);
using BlinkFn = void(THISCALL *)(void *, unsigned);  // vtable slot 14: blink the arrow for an Act data value
using DrawFn = void(THISCALL *)(void *);            // vtable slot 3

bool g_cam_opts_next = false;  // our Options entry was pressed
bool g_cam_opts = false;       // the open Options screen is the camera screen
void *g_options_screen = nullptr;
void *g_pending_optmain = nullptr;      // the Options menu being set up, which gets our entry
void *g_hint_pending_screen = nullptr;  // the Help hint is added on this screen's first tick

void *g_toggle_vtbl[15];    // our parameter rows (FEToggleWidget behaviour with our Act/Draw)
void *g_drivecam_vtbl[15];  // PODriveCam with Act wrapped so the other rows refresh when the view changes

void *options_settings() { return field<void *>(*reinterpret_cast<void **>(game::kFEDatabase), 0x10); }
int &options_category() { return field<int>(options_settings(), 0x24); }

int camera_type_being_edited() {
    // The selector row stores the chosen camera in PlayerSettings[player].CurCam; map it to a POV preset.
    const int player = *reinterpret_cast<int *>(game::kPlayerToEdit);
    const int cam = field<int>(options_settings(), 0x80 + player * 0x14 + 0x0C);
    const int type = game::GetPOVTypeFromPlayerCamera(cam);
    return (type >= 0 && type < kNumPresets) ? type : g_cur_type;
}

void format_value(char *out, size_t n, int param, float v) {
    const char *deg = g_degree_sign ? "\x01" : " deg";
    switch (param) {
        case P_FOV: snprintf(out, n, "%.0f%s", v, deg); break;
        case P_HEIGHT: snprintf(out, n, "%.2f", v); break;
        case P_LAG: snprintf(out, n, "%.1f", v); break;
        case P_ANGLE: snprintf(out, n, "%.1f%s", v, deg); break;
        case P_LAT: snprintf(out, n, "%.2f", v); break;
        case P_STIFF: snprintf(out, n, "%.2f", v); break;
        default: snprintf(out, n, "%s", v >= 0.5f ? "On" : "Off"); break;
    }
}

// Store an override unless it equals the game's own value for that preset.
void store_value(int type, int param, float v) {
    const float base = g_have_base[type] ? g_base[type][param] : v;
    if (std::fabs(v - base) < 1e-3f) {
        g_ovr[type].set[param] = false;
    } else {
        g_ovr[type].set[param] = true;
        g_ovr[type].val[param] = v;
    }
    log_line("edit %s.%s = %g (base %g)%s", kPresetName[type], kKey[param], v, base,
             g_ovr[type].set[param] ? "" : " -> back to game value");
}

void THISCALL param_row_act(void *self, const char *pkg, unsigned data) {
    (void)pkg;
    if (data != kPadLeft && data != kPadRight) return;
    const int param = field<int>(self, kRowParam);
    if (param < 0 || param >= NPARAM) return;
    const int type = camera_type_being_edited();
    const Range &r = kRange[param];
    float v = effective(type, param);
    if (param == P_TILT) {
        v = data == kPadRight ? 1.0f : 0.0f;
    } else {
        const bool fine = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        v += (data == kPadRight ? 1.0f : -1.0f) * (fine ? r.fine : r.step);
        v = std::round(v / r.fine) * r.fine;  // on the fine grid, so the ini gets 2.3 rather than 2.3000002
        v = v < r.min ? r.min : (v > r.max ? r.max : v);
    }
    store_value(type, param, v);
    field<unsigned char>(self, kRowChanged) = 1;  // as PODriveCam::Act does
    void **vtbl = field<void **>(self, 0);
    reinterpret_cast<BlinkFn>(vtbl[14])(self, data);  // arrow blink
    reinterpret_cast<DrawFn>(vtbl[3])(self);
}

void THISCALL param_row_draw(void *self) {
    const int param = field<int>(self, kRowParam);
    if (param < 0 || param >= NPARAM) return;
    char value[32];
    format_value(value, sizeof(value), param, effective(camera_type_being_edited(), param));
    // The label goes in as a language hash, like PODriveCam::Draw does for "Camera". FEStatWidget::Position
    // centres a label on the column edge using the width of its literal text, which a language-hash label leaves
    // out, so the game's labels end at the edge (right-aligned). Text set with FESetString was measured and centred.
    game::FEngSetLanguageHash(field<void *>(self, kRowTitle), kLabelTextBase + param);
    set_wide_text(field<void *>(self, kRowData), value);
}

void THISCALL drivecam_row_act(void *self, const char *pkg, unsigned data) {
    game::DriveCamAct(self, pkg, data);
    if (g_options_screen) game::RefreshWidgets(g_options_screen);  // the other rows follow the new view
}

void init_option_vtables() {
    void **src = reinterpret_cast<void **>(game::kDriveCamVtable);
    for (int i = 0; i < 15; ++i) g_toggle_vtbl[i] = g_drivecam_vtbl[i] = src[i];
    g_toggle_vtbl[0] = reinterpret_cast<void *>(game::kToggleDelDtor);
    g_toggle_vtbl[1] = reinterpret_cast<void *>(&param_row_act);
    g_toggle_vtbl[3] = reinterpret_cast<void *>(&param_row_draw);
    g_drivecam_vtbl[1] = reinterpret_cast<void *>(&drivecam_row_act);
}

// NFSMWExtraOptions (EnableHiddenCameraModes) names the Drift camera with the language string "DRIFT", the only
// all-caps name in the list. The game has its own "Drift" string, translated like the rest, so use that instead.
// Hooked in the game's PODriveCam::Draw, so Options > Player shows it too.
MethodFn g_orig_drivecam_draw;
void FASTCALL hook_drivecam_draw(void *self, void *edx) {
    g_orig_drivecam_draw(self, edx);
    void *value = field<void *>(self, kRowData);
    if (value && field<unsigned>(value, fe::kLabelHash) == kDriftUpper) game::FEngSetLanguageHash(value, kDrift);
}

void build_camera_options(void *screen) {
    g_options_screen = screen;
    memcpy(g_ovr_open, g_ovr, sizeof(g_ovr));
    game::ClearWidgets(screen);  // drop the Player rows SetupPlayer just added

    if (void *w = game::Alloc(0x5C)) {  // the game's own Camera selector
        game::DriveCamCtor(w, 1);
        field<void **>(w, 0) = g_drivecam_vtbl;
        game::AddToggleOption(screen, w, 1);
    }
    for (int p = 0; p < NPARAM; ++p) {
        void *w = game::Alloc(kRowParam + 4);
        if (!w) continue;
        game::ToggleWidgetCtor(w, 1);
        field<void **>(w, 0) = g_toggle_vtbl;
        field<int>(w, kRowParam) = p;
        game::AddToggleOption(screen, w, 1);
    }
    // The title object is a group of strings (text and shadow). Set the hash the way the game's own screens do,
    // then also print into the group so it does not depend on the table lookup.
    const char *pkg = field<const char *>(screen, kScreenPackage);
    game::FEngSetLanguageHashPkg(pkg, kHeaderText, kTitleText);
    game::FEPrintfPkg(pkg, static_cast<int>(kHeaderText), "%s", "CAMERA");
    game::FEngSetTextureHashPkg(pkg, kHeaderIcon, g_icon_shown);
    game::RefreshWidgets(screen);
    g_hint_pending_screen = screen;  // the Help hint is cloned on the first tick, once the layout is live
    log_line("Options > Camera built (editing %s)", kPresetName[camera_type_being_edited()]);
}

MethodFn g_orig_setupplayer;
void FASTCALL hook_setupplayer(void *self, void *edx) {
    g_orig_setupplayer(self, edx);
    g_cam_opts = g_cam_opts_next;
    g_cam_opts_next = false;
    if (!g_cam_opts) return;
    ensure_language_entries();
    build_camera_options(self);
}

// Back asks "discard changes?" only when this says something changed; the camera values count as well.
BoolMethodFn g_orig_didnotchange;
bool FASTCALL hook_didnotchange(void *self, void *edx) {
    const bool unchanged = g_orig_didnotchange(self, edx);
    if (g_cam_opts && self == g_options_screen && camera_changed()) return false;
    return unchanged;
}

// The discard was confirmed: the game puts its own values back; put ours back too.
MethodFn g_orig_restoreoriginals;
void FASTCALL hook_restoreoriginals(void *self, void *edx) {
    g_orig_restoreoriginals(self, edx);
    if (g_cam_opts && self == g_options_screen && camera_changed()) {
        memcpy(g_ovr, g_ovr_open, sizeof(g_ovr));
        log_line("camera changes discarded");
    }
}

// Defaults was confirmed: clear the selected view's overrides instead of resetting the Player settings.
MethodFn g_orig_restoredefaults;
void FASTCALL hook_restoredefaults(void *self, void *edx) {
    if (g_cam_opts && options_category() == kCategoryPlayer) {
        const int type = camera_type_being_edited();
        memset(&g_ovr[type], 0, sizeof(g_ovr[type]));
        log_line("Defaults: cleared all overrides for %s", kPresetName[type]);
        game::RefreshWidgets(self);
        return;
    }
    g_orig_restoredefaults(self, edx);
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
        log_line("Help hint: failed (see nfsmw_cam_crash.log); the rest of the screen is unaffected");
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
    ensure_language_entries();
    void *row = field<void *>(screen, kScreenCurrentRow);
    if (!row) return;
    void **vtbl = field<void **>(row, 0);
    unsigned text;
    if (vtbl == g_toggle_vtbl) {
        const int param = field<int>(row, kRowParam);
        if (param < 0 || param >= NPARAM) return;
        text = kHelpTextBase + param;
    } else if (vtbl == g_drivecam_vtbl) {
        text = kHelpTextBase + NPARAM;
    } else {
        return;
    }
    g_help_dialog_open = true;
    g_help_dialog_pending = true;
    game::ShowOneButton(field<const char *>(screen, kScreenPackage), reinterpret_cast<const char *>(game::kInGameDialogName),
                        kDialogInfo, kDialogOkText, kDialogMessageNo, kDialogMessageNo, text);
}

MethodFn g_orig_dialog_build;
void FASTCALL hook_dialog_build(void *self, void *edx) {
    g_orig_dialog_build(self, edx);
    if (!g_help_dialog_pending) return;
    g_help_dialog_pending = false;
    const char *pkg = field<const char *>(self, kScreenPackage);
    game::FEngSetLanguageHashPkg(pkg, kHeaderText, kDialogTitleText);
    game::FEPrintfPkg(pkg, static_cast<int>(kHeaderText), "%s", "CAMERA INFO");
    if (GetPrivateProfileIntA("features", "hide_dialog_icon", 1, g_ini)) {
        game::FEngSetInvisiblePkg(pkg, kDialogIcon1);
        game::FEngSetInvisiblePkg(pkg, kDialogIcon2);
    }
}

// The Options layout only forwards the input messages it has responses for (Defaults, Accept, Back, arrows), so a
// "2" key press never reaches this screen as a message. The screen does receive FEMSG_SCREEN_TICK every frame, so
// the Help key is read from the keyboard on those ticks.
using NotifyFn = void(FASTCALL *)(void *, void *, unsigned, void *, unsigned, unsigned);
NotifyFn g_orig_notify = nullptr;
void FASTCALL hook_notify(void *self, void *edx, unsigned msg, void *obj, unsigned p1, unsigned p2) {
    if (g_cam_opts) {
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
            const bool down = (GetAsyncKeyState('2') & 0x8000) || (GetAsyncKeyState(VK_NUMPAD2) & 0x8000);
            const DWORD now = GetTickCount();
            if (down && !was_down && !g_help_dialog_open && now - last_ms > 400) {
                last_ms = now;
                show_help_for_current_row(self);
            }
            was_down = down;
        } else if (msg == kDialogMessageNo) {
            g_help_dialog_open = false;  // our message box (or the Defaults one) closed
            g_help_dialog_pending = false;
        } else if (msg == kExitComplete && self == g_options_screen) {
            // Left the screen. A discard has already put g_ovr back, so what differs now was accepted.
            if (camera_changed()) {
                ini_save();
                memcpy(g_ovr_open, g_ovr, sizeof(g_ovr));
                log_line("camera settings applied and saved");
            }
        }
    }
    g_orig_notify(self, edx, msg, obj, p1, p2);
}

// ------------------------------------------------------------------------------------------------
// The Options menu entry ("Camera" next to Audio / Video / Gameplay / Player / Controller), pause menu only.
// ------------------------------------------------------------------------------------------------
void THISCALL options_entry_react(void *self, const char *pkg, unsigned data, void *obj, unsigned p1, unsigned p2) {
    (void)self; (void)pkg; (void)obj; (void)p1; (void)p2;
    if (data != kButtonPressed) return;
    // Exactly what OMPlayer::React does: select the Player category. UIOptionsMain then opens the Options layout.
    options_category() = kCategoryPlayer;
    g_cam_opts_next = true;
}

void *g_options_entry_vtable[2] = {reinterpret_cast<void *>(game::kIconOptionDelDtor),
                                   reinterpret_cast<void *>(&options_entry_react)};

void add_options_entry(void *menu) {
    void *opt = game::Alloc(0x5C);
    if (!opt) return;
    game::IconOptionCtor(opt, g_icon_shown, kOptEntryText, 0);
    field<void **>(opt, 0) = g_options_entry_vtable;
    field<unsigned char>(opt, 0x45) = 1;  // react immediately, like the game's own entries
    void *row = static_cast<unsigned char *>(menu) + 0x2C;
    void **vtbl = field<void **>(row, 0);
    // Same as IconScrollerMenu::AddOption, keeping the result: the row binds options to UI objects
    // "option_1".."option_N" in the layout and silently drops extras.
    void *img = reinterpret_cast<void *(THISCALL *)(void *, void *)>(vtbl[2])(row, opt);
    if (img) game::FEngSetTextureHash(img, g_icon_shown);
    else log_line("Options menu: no free slot for the Camera entry");
}

// UIOptionsMain::Setup adds its icons and then lays them out with the row's SetInitialPos (the call is inlined,
// so it cannot be hooked at SetInitialOption). Our entry must be added right before that call.
using SetInitialPosFn = void(FASTCALL *)(void *, void *, int);
SetInitialPosFn g_orig_initialpos;
void FASTCALL hook_initialpos(void *row, void *edx, int index) {
    void *menu = static_cast<unsigned char *>(row) - 0x2C;  // the icon row object lives at menu+0x2C
    if (g_pending_optmain && menu == g_pending_optmain) {
        g_pending_optmain = nullptr;
        add_options_entry(menu);
    }
    g_orig_initialpos(row, edx, index);
}

// Our icon lives in our own texture pack, nfsmw_cam.tpk next to the .asi (built from Mods/cam/OPTIONS_CAMERA.png by
// Tools/tpk_build.py). It goes through the game's own resource loader, like the packs other mods add, which
// registers its textures by hash. Loaded once, the first time the pause menu's Options opens.
void load_texture_pack() {
    static bool tried = false;
    if (tried || !GetPrivateProfileIntA("features", "texture_pack", 1, g_ini)) return;
    tried = true;
    char path[MAX_PATH] = {}, game_dir[MAX_PATH] = {};
    GetModuleFileNameA(static_cast<HMODULE>(g_self_base), path, sizeof(path) - 16);
    GetModuleFileNameA(nullptr, game_dir, sizeof(game_dir));
    char *slash = strrchr(path, '\\');
    char *game_slash = strrchr(game_dir, '\\');
    if (!slash || !game_slash) return;
    strcpy(slash + 1, "nfsmw_cam.tpk");
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        log_line("texture pack %s not found", path);
        return;
    }
    // Relative to the game folder, like the game's own file names, when the .asi lives inside it.
    const size_t game_len = static_cast<size_t>(game_slash + 1 - game_dir);
    const char *name = _strnicmp(path, game_dir, game_len) == 0 ? path + game_len : path;
    void *file = game::LoadResourceFile(name, 1, 0, nullptr, nullptr, 0, 0);  // RESOURCE_FILE_GLOBAL
    const DWORD start = GetTickCount();
    while (file && !field<int>(file, 0x30) && GetTickCount() - start < 3000) {  // ResourceFile::LoadingFinishedFlag
        game::ServiceResourceLoading();
        Sleep(1);
    }
    log_line("texture pack %s: %s after %lu ms", name, file && field<int>(file, 0x30) ? "loaded" : "NOT loaded",
             GetTickCount() - start);
}

MethodFn g_orig_optmain_setup;
void FASTCALL hook_optmain_setup(void *self, void *edx) {
    g_cam_opts_next = false;
    g_cam_opts = false;
    g_pending_optmain = nullptr;
    g_hint_pending_screen = nullptr;
    g_hint.screen = nullptr;
    g_hint.clone = nullptr;
    g_hint.n = 0;
    ensure_language_entries();
    if (g_anchor && game::IsPaused(reinterpret_cast<void *>(game::kGameFlowManager))) {
        // Read every preset's own values from the game so each can be edited without having been the active view.
        for (int t = 0; t < kNumPresets; ++t) {
            if (Pov *p = g_orig_getpov(g_anchor, nullptr, t)) {
                for (int i = 0; i < NPARAM; ++i) g_base[t][i] = get_field(*p, i);
                g_have_base[t] = true;
            }
        }
        load_texture_pack();
        g_icon_shown = game::GetTextureInfo(g_icon, 0, 0) ? g_icon : kSpeedtrapTexture;
        if (g_icon_shown != g_icon) log_line("icon texture %08x is not loaded; using MILESTONE_SPEEDTRAP", g_icon);
        g_pending_optmain = self;
    }
    g_orig_optmain_setup(self, edx);
    g_pending_optmain = nullptr;
}

// ------------------------------------------------------------------------------------------------
// Hooks. Required ones are checked first: if another mod changed any of their first bytes, nothing is hooked,
// because the parts depend on each other. Optional ones are skipped on their own.
// ------------------------------------------------------------------------------------------------
struct Hook {
    const char *name;  // [hooks] <name>=0 in the ini skips it
    const char *function;
    uintptr_t addr;
    unsigned char prologue[7];
    bool required;
    void *detour;
    void **orig;
};

template <typename Fn>
void *detour(Fn fn) { return reinterpret_cast<void *>(fn); }
template <typename Fn>
void **orig(Fn *fn) { return reinterpret_cast<void **>(fn); }

void install_hooks() {
    const Hook hooks[] = {
        {"getpov", "CameraAnchor::GetPov", 0x4713C0, {0x8B, 0x44, 0x24, 0x04, 0x83, 0xF8, 0x06}, true,
         detour(&hook_getpov), orig(&g_orig_getpov)},
        {"optmain", "UIOptionsMain::Setup", 0x528F30, {0x6A, 0xFF, 0x68, 0x20, 0xEB, 0x86, 0x00}, true,
         detour(&hook_optmain_setup), orig(&g_orig_optmain_setup)},
        {"initialpos", "IconScroller::SetInitialPos", 0x592C90, {0x51, 0x53, 0x55, 0x56, 0x8B, 0xF1, 0x8B}, true,
         detour(&hook_initialpos), orig(&g_orig_initialpos)},
        {"setupplayer", "UIOptionsScreen::SetupPlayer", 0x529F10, {0x6A, 0xFF, 0x68, 0x80, 0xEE, 0x86, 0x00}, true,
         detour(&hook_setupplayer), orig(&g_orig_setupplayer)},
        {"notify", "UIOptionsScreen::NotificationMessage", 0x545750, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x8B}, true,
         detour(&hook_notify), orig(&g_orig_notify)},
        {"restoredefaults", "UIOptionsScreen::RestoreDefaults", 0x51D170, {0x53, 0x55, 0x8B, 0xE9, 0x8B, 0x0D, 0x90},
         true, detour(&hook_restoredefaults), orig(&g_orig_restoredefaults)},
        {"didnotchange", "UIOptionsScreen::OptionsDidNotChange", 0x510360, {0x56, 0x8B, 0x35, 0x90, 0xCF, 0x91, 0x00},
         true, detour(&hook_didnotchange), orig(&g_orig_didnotchange)},
        {"restoreoriginals", "UIOptionsScreen::RestoreOriginals", 0x510450, {0x56, 0x8B, 0x35, 0x90, 0xCF, 0x91, 0x00},
         true, detour(&hook_restoreoriginals), orig(&g_orig_restoreoriginals)},
        {"dialogbuild", "feDialogScreen::BuildFromConfig", 0x58F720, {0x83, 0xEC, 0x08, 0x56, 0x8B, 0xF1, 0x8A}, true,
         detour(&hook_dialog_build), orig(&g_orig_dialog_build)},
        {"drivecamdraw", "PODriveCam::Draw", 0x51C910, {0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x77, 0x2C}, false,
         detour(&hook_drivecam_draw), orig(&g_orig_drivecam_draw)},
    };
    const auto intact = [](const Hook &h) {
        return memcmp(reinterpret_cast<const void *>(h.addr), h.prologue, sizeof(h.prologue)) == 0;
    };
    bool ok = true;
    for (const Hook &h : hooks) {
        if (h.required && !intact(h)) {
            log_line("%s (%#x) was changed by another mod", h.function, static_cast<unsigned>(h.addr));
            ok = false;
        }
    }
    if (!ok || MH_Initialize() != MH_OK) {
        log_line("not hooking anything");
        return;
    }
    init_option_vtables();
    int wanted = 0, created = 0;
    for (const Hook &h : hooks) {
        if (!GetPrivateProfileIntA("hooks", h.name, 1, g_ini)) {
            log_line("hook %s disabled in the ini", h.name);
            continue;
        }
        if (!h.required && !intact(h)) {
            log_line("%s (%#x) was changed by another mod; not hooking it", h.function, static_cast<unsigned>(h.addr));
            continue;
        }
        ++wanted;
        const MH_STATUS s = MH_CreateHook(reinterpret_cast<void *>(h.addr), h.detour, h.orig);
        if (s == MH_OK) ++created;
        else log_line("MH_CreateHook(%s) failed: %s", h.name, MH_StatusToString(s));
    }
    const MH_STATUS s = MH_EnableHook(MH_ALL_HOOKS);
    log_line("hooks installed: %d of %d%s", created, wanted, s == MH_OK ? "" : " (MH_EnableHook failed)");
}

}  // namespace

NFSMW_PLUGIN_MAIN() {
    g_log = fopen("nfsmw_cam.log", "w");
    log_line("nfsmw_cam 0.11.1 starting");
    g_crash_file = CreateFileA("nfsmw_cam_crash.log", FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<void *>(&crash_logger), &mbi, sizeof(mbi))) g_self_base = mbi.AllocationBase;
    crash_write("---- nfsmw_cam 0.11.1 session (tick %lu) ----", GetTickCount());
    AddVectoredExceptionHandler(1, crash_logger);
    ini_path_init();
    ini_load();
    install_hooks();
    return NFSMW_OK;
}
