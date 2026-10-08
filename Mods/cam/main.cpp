/*
 * nfsmw_cam — camera preset overrides with a native Options screen, for speed.exe 1.3
 * (SHA-256 0C5675A0...2067E342).
 *
 *  - Hooks CameraAnchor::GetPov (0x4713C0), which the game calls every frame to build the active
 *    camera preset (POV) from the car's attribute data, and applies our per-preset overrides to the
 *    returned struct. Switching presets with the game's hotkey therefore keeps our values.
 *  - Adds "Camera" to the Options menu (pause > Options). It opens the game's own Options layout with
 *    the game's Camera selector as row 1 and one row per preset value below it; the rows follow
 *    whichever view the selector is on. The game's Defaults key (1) resets that view's overrides after
 *    the usual confirmation. A "2 Help" hint (a clone of the layout's Defaults hint) sits next to it, and
 *    the 2 key opens a "CAMERA INFO" message box explaining the highlighted row.
 *  - Overrides persist in nfsmw_cam.ini next to speed.exe.
 *
 * All struct offsets below were read from the PC disassembly (the decomp's offsets come from the
 * console builds and differ). Logs: nfsmw_cam.log, and nfsmw_cam_crash.log for access violations.
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

NFSMW_PLUGIN_DECLARE("NFSMW Camera Editor", "0.11.0", "Demeter")

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
// loader lock (the previous version could hang the game): raw WriteFile, module bases from VirtualQuery.
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
// Game addresses (speed.exe 1.3). Prologue bytes are checked before anything is hooked.
// ------------------------------------------------------------------------------------------------
constexpr uintptr_t kGetPov = 0x4713C0;              // CameraAnchor::GetPov(int type), thiscall ret 4
constexpr uintptr_t kOptionsMainSetup = 0x528F30;    // UIOptionsMain::Setup(): builds the Options icon row
constexpr uintptr_t kSetupPlayer = 0x529F10;         // UIOptionsScreen::SetupPlayer(): Options > Player rows
constexpr uintptr_t kRestoreDefaults = 0x51D170;     // UIOptionsScreen::RestoreDefaults(): after the Defaults dialog
constexpr uintptr_t kOptionsDidNotChange = 0x510360;  // UIOptionsScreen::OptionsDidNotChange(): Back asks before discarding
constexpr uintptr_t kRestoreOriginals = 0x510450;    // UIOptionsScreen::RestoreOriginals(): "discard changes" was confirmed
constexpr uintptr_t kDriveCamDraw = 0x51C910;        // PODriveCam::Draw(): the Camera row's label and value
constexpr uintptr_t kOptionsNotify = 0x545750;       // UIOptionsScreen::NotificationMessage(msg, obj, p1, p2), ret 0x10
constexpr uintptr_t kIconScrollerSetInitialPos = 0x592C90;  // IconScroller::SetInitialPos(int); this = menu+0x2C
constexpr uintptr_t kDialogBuild = 0x58F720;         // feDialogScreen::BuildFromConfig(): sets title, icon, text, buttons

constexpr uintptr_t kIconOptionCtor = 0x586FA0;      // IconOption::IconOption(tex, name, desc), thiscall ret 0xC
constexpr uintptr_t kIconOptionDelDtor = 0x51A500;   // IconOption scalar deleting destructor
constexpr uintptr_t kGameAlloc = 0x652AD0;           // operator new(size), cdecl
constexpr uintptr_t kSetTextureHash = 0x515B90;      // FEngSetTextureHash(FEImage*, hash), cdecl
constexpr uintptr_t kSetTextureHashPkg = 0x527EE0;   // FEngSetTextureHash(pkg, object_hash, texture_hash), cdecl
constexpr uintptr_t kSetLangHashPkg = 0x525220;      // FEngSetLanguageHash(pkg, object_hash, language_hash), cdecl
constexpr uintptr_t kFEPrintfPkg = 0x5252D0;         // FEPrintf(pkg, object_hash, fmt, ...), cdecl; strings AND groups
constexpr uintptr_t kFESetString = 0x515CC0;         // FESetString(FEString*, const int16*), cdecl
constexpr uintptr_t kFEPrintf = 0x515D70;            // FEPrintf(FEString*, const char*, ...), cdecl
constexpr uintptr_t kFEngSetInvisiblePkg = 0x527DB0;  // FEngSetInvisible(pkg, object_hash), cdecl
constexpr uintptr_t kShowOneButton = 0x5989B0;       // DialogInterface::ShowOneButton(...), cdecl, 7 args
constexpr uintptr_t kLangTablePtr = 0x91CF80;        // sorted language table: {hash, const char* text} entries
constexpr uintptr_t kLangCountVar = 0x91CF78;        // number of entries in that table

constexpr uintptr_t kClearWidgets = 0x588C90;        // UIWidgetMenu::ClearWidgets()
constexpr uintptr_t kRefreshWidgets = 0x588D20;      // UIWidgetMenu::RefreshWidgets(): Draw() on every row
constexpr uintptr_t kAddToggleOption = 0x588570;     // UIWidgetMenu::AddToggleOption(FEToggleWidget*, bool), thiscall ret 8
constexpr uintptr_t kToggleCtor = 0x589300;          // FEToggleWidget::FEToggleWidget(bool), thiscall ret 4
constexpr uintptr_t kToggleDelDtor = 0x589370;       // FEToggleWidget scalar deleting destructor
constexpr uintptr_t kDriveCamCtor = 0x51AA90;        // PODriveCam::PODriveCam(bool): the Options > Player > Camera row
constexpr uintptr_t kDriveCamAct = 0x51C870;         // PODriveCam::Act(pkg, data)
constexpr uintptr_t kDriveCamVtable = 0x89C048;      // 15 slots: [0] dtor [1] Act [3] Draw [14] BlinkArrows ...
constexpr uintptr_t kPovFromPlayerCamera = 0x56EBC0;  // POVTypes GetPOVTypeFromPlayerCamera(int), cdecl
constexpr uintptr_t kIsPausedFn = 0x64B680;          // GameFlowManager::IsPaused(), thiscall
constexpr uintptr_t kGameFlowObj = 0x925E70;
constexpr uintptr_t kFEDatabasePtr = 0x91CF90;       // FEDatabase*; +0x10 = options settings, category at +0x24
constexpr uintptr_t kPlayerToEditVar = 0x91CA24;     // UIOptionsScreen::PlayerToEdit
constexpr uintptr_t kInGameDialogName = 0x89736C;    // "InGameDialog.fng"

// FEng objects (FEObject: +0x10 name hash, +0x18 type, +0x1C flags, +0x2C data (colour at +0, alpha at +0xC),
// +0x58 cached render object; FEGroup children list: +0x60 count, +0x64 head; nodes link through +0x4)
constexpr uintptr_t kFEngFindGroup = 0x524880;       // FEngFindGroup(pkg, object_hash), cdecl
constexpr uintptr_t kFEngGetCenter = 0x524EE0;       // FEngGetCenter(FEObject*, float&, float&), cdecl
constexpr uintptr_t kFEngSetCenter = 0x525050;       // FEngSetCenter(FEObject*, float, float), cdecl
constexpr uintptr_t kFEngFindPackage = 0x516AF0;     // cFEng::FindPackage(pkg_name), thiscall (this = *0x91CADC)
constexpr uintptr_t kFEngVar = 0x91CADC;
constexpr uintptr_t kGroupClone = 0x5C49C0;          // FEGroup::Clone(bool reference), thiscall ret 4
constexpr uintptr_t kGroupAddObject = 0x5B5420;      // FEGroup::AddObject(FEObject*), thiscall ret 4
constexpr uintptr_t kPackageAddObject = 0x50DB90;    // FEPackage::AddObject(FEObject*), thiscall ret 4
constexpr uintptr_t kObjectSetScript = 0x5BC160;     // FEObject::SetScript(u32 id, bool force), thiscall ret 8

constexpr unsigned kHeaderTextHash = 0x42ADB44C;     // HEADER_TEXT: the big title at the top of a screen / dialog
constexpr unsigned kDialogIcon1 = 0x0D4F4069;        // the two icon objects of the message box (exclamation mark)
constexpr unsigned kDialogIcon2 = 0xFAC88427;
// The Options layout has two hint bars: console hints (DEFAULTS_BUTTON etc., hidden on PC) and the PC hints that are
// actually shown: top-level groups of {label string, clickable hit-box group (FF_MouseObject), key image}.
constexpr unsigned kPcDefaultsHint = 0x3FC72F9D;      // "1 Defaults"
constexpr unsigned kPcAcceptHint = 0x9502D148;        // "Enter Accept"
constexpr unsigned kHelpClickMsg = 0xCA3E0B01;        // what a click on our hint posts to the screen
constexpr uintptr_t kBuildMouseList = 0x5B88F0;       // FEPackage::BuildMouseObjectStateList(), thiscall
constexpr unsigned kHelpHintHash = 0xCA3E00A0;        // object name hash for our cloned hint
constexpr unsigned kPcKey2Texture = 0x68A29A8B;       // bHash("PC_KEY_2"), in the always-loaded GLOBALB texture pack
constexpr unsigned kPcKey1Texture = 0x68A29A8A, kPcKey4Texture = 0x68A29A8D, kCircleTexture = 0x54A68051;
constexpr unsigned kButton5Msg = 0xC519BFC4;          // __PAD_BUTTON5__ (the 1 key: Defaults)
constexpr unsigned kButtonPressed = 0x0C407210;
constexpr unsigned kPadLeft = 0x9120409E, kPadRight = 0xB5971BF1;
constexpr unsigned kExitComplete = 0xE1FDE1D1;       // FEHASH_EXITCOMPLETE: the screen finished leaving (Accept, or Back)
constexpr unsigned kScreenTick = 0xC98356BA;         // FEMSG_SCREEN_TICK, sent to the screen every frame
constexpr unsigned kDialogMessageNo = 0x34DC1BCF;
constexpr unsigned kDialogOkText = 0x417B2601;
constexpr int kDialogInfo = 2;                        // eDialogTitle dialog_info
constexpr unsigned kTuningIconHash = 0x483238FD;
constexpr unsigned kSpeedtrapIconHash = 0x66C9A7B6;  // MILESTONE_SPEEDTRAP (GLOBALTEXTURES): a camera
constexpr unsigned kCustomIconHash = 0x12F6B313;     // OPTIONS_CAMERA, in our own nfsmw_cam.tpk
constexpr uintptr_t kLoadResourceFile = 0x661760;    // LoadResourceFile(name, type, flags, callback, param, offset, size)
constexpr uintptr_t kServiceResourceLoading = 0x6626B0;  // ServiceResourceLoading(): advances queued loads
constexpr uintptr_t kGetTextureInfo = 0x503400;      // GetTextureInfo(hash, return_default, include_unloaded)
constexpr unsigned kHeaderIconHash = 0x08007B4C;     // the Options screen's header icon (SetupPlayer sets the Player one)
constexpr unsigned kDriftUpperHash = 0xA1211428;     // "DRIFT": what NFSMWExtraOptions shows for the Drift camera
constexpr unsigned kDriftHash = 0x6AEB7EE6;          // "Drift": the game's own string for the same word

// Our own language hashes (merged into the game's language table below).
constexpr unsigned kOptEntryHash = 0xCA3E0003;       // Options menu entry label
constexpr unsigned kTitleHash = 0xCA3E0030;          // screen title
constexpr unsigned kDialogTitleHash = 0xCA3E0031;    // message box title
constexpr unsigned kHelpHashBase = 0xCA3E0040;       // + parameter index (0..6); +7 = the camera selector row
constexpr unsigned kLabelHashBase = 0xCA3E0060;      // + parameter index: row labels
constexpr uintptr_t kSetLangHashStr = 0x515C00;      // FEngSetLanguageHash(FEString*, hash), cdecl
constexpr unsigned kScriptHighlight = 0x249DB7B7, kScriptUnhighlight = 0x7AB5521A;  // hint label hover scripts

struct Target {
    const char *name;
    uintptr_t addr;
    unsigned char want[7];
};

const Target kPrologues[] = {
    {"GetPov", kGetPov, {0x8B, 0x44, 0x24, 0x04, 0x83, 0xF8, 0x06}},
    {"UIOptionsMain::Setup", kOptionsMainSetup, {0x6A, 0xFF, 0x68, 0x20, 0xEB, 0x86, 0x00}},
    {"UIOptionsScreen::SetupPlayer", kSetupPlayer, {0x6A, 0xFF, 0x68, 0x80, 0xEE, 0x86, 0x00}},
    {"UIOptionsScreen::RestoreDefaults", kRestoreDefaults, {0x53, 0x55, 0x8B, 0xE9, 0x8B, 0x0D, 0x90}},
    {"UIOptionsScreen::NotificationMessage", kOptionsNotify, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x8B}},
    {"UIOptionsScreen::OptionsDidNotChange", kOptionsDidNotChange, {0x56, 0x8B, 0x35, 0x90, 0xCF, 0x91, 0x00}},
    {"UIOptionsScreen::RestoreOriginals", kRestoreOriginals, {0x56, 0x8B, 0x35, 0x90, 0xCF, 0x91, 0x00}},
    {"IconScroller::SetInitialPos", kIconScrollerSetInitialPos, {0x51, 0x53, 0x55, 0x56, 0x8B, 0xF1, 0x8B}},
    {"feDialogScreen::BuildFromConfig", kDialogBuild, {0x83, 0xEC, 0x08, 0x56, 0x8B, 0xF1, 0x8A}},
};

bool prologues_ok() {
    bool ok = true;
    for (const Target &t : kPrologues) {
        if (memcmp(reinterpret_cast<const void *>(t.addr), t.want, sizeof(t.want)) != 0) {
            log_line("prologue of %s (%#x) differs from expected - another mod patched it?", t.name,
                     static_cast<unsigned>(t.addr));
            ok = false;
        }
    }
    return ok;
}

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

Override g_ovr[kNumPresets];
Override g_ovr_open[kNumPresets];  // g_ovr when Options > Camera opened: what was last applied (and is in the ini)
float g_base[kNumPresets][NPARAM];  // the game's own values, captured before overriding
bool g_have_base[kNumPresets];
volatile int g_cur_type = 3;        // preset most recently requested from GetPov
void *volatile g_anchor = nullptr;  // CameraAnchor passed to GetPov (valid while a race is loaded)

bool g_degree_sign = true;  // [general] degree_symbol=0 falls back to " deg" if the font lacks the glyph
unsigned g_icon = kCustomIconHash;  // [general] icon = texture name or 8-digit hex hash
unsigned g_icon_shown = kSpeedtrapIconHash;  // g_icon if the game has that texture, else the built-in camera icon

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
    char *slash = strrchr(g_ini, 92);  // 92 = backslash
    if (slash) *(slash + 1) = 0;
    strncat(g_ini, "nfsmw_cam.ini", sizeof(g_ini) - strlen(g_ini) - 1);
}

void ini_load() {
    for (int t = 0; t < kNumPresets; ++t) {
        for (int i = 0; i < NPARAM; ++i) {
            char buf[64] = {};
            GetPrivateProfileStringA(kPresetName[t], kKey[i], "", buf, sizeof(buf), g_ini);
            if (buf[0]) {
                g_ovr[t].set[i] = true;
                g_ovr[t].val[i] = static_cast<float>(atof(buf));
            }
        }
    }
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
using GetPovFn = Pov *(__attribute__((fastcall)) *)(void *, void *, int);
GetPovFn g_orig_getpov = nullptr;
unsigned g_base_logged = 0;

Pov *__attribute__((fastcall)) hook_getpov(void *self, void *edx, int type) {
    Pov *p = g_orig_getpov(self, edx, type);
    if (p && type >= 0 && type < kNumPresets) {
        g_cur_type = type;
        g_anchor = self;
        float now[NPARAM];
        for (int i = 0; i < NPARAM; ++i) now[i] = get_field(*p, i);
        if (!g_have_base[type] || memcmp(now, g_base[type], sizeof(now)) != 0) {
            if (g_base_logged++ < 200)
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
// binary search over a sorted table of {hash, text pointer} entries (*0x91CF80, count *0x91CF78), so we merge our
// own entries into that table and the game's own code does the rest.
// ------------------------------------------------------------------------------------------------
using FEPrintfFn = int (*)(void *, const char *, ...);
using FEPrintfPkgFn = int (*)(const char *, int, const char *, ...);
using SetLangHashPkgFn = void (*)(const char *, unsigned, unsigned);

struct LangEntry {
    unsigned hash;
    const char *text;
};

LangEntry *g_merged_table = nullptr;

void ensure_language_entries() {
    LangEntry *cur = *reinterpret_cast<LangEntry **>(kLangTablePtr);
    if (!cur || cur == g_merged_table) return;  // not loaded yet, or already merged
    const unsigned n = *reinterpret_cast<unsigned *>(kLangCountVar);

    LangEntry ours[3 + NPARAM + 1 + NPARAM];
    unsigned m = 0;
    ours[m++] = {kOptEntryHash, "Camera"};
    ours[m++] = {kTitleHash, "CAMERA"};
    ours[m++] = {kDialogTitleHash, "CAMERA INFO"};
    for (int i = 0; i < NPARAM + 1; ++i) ours[m++] = {kHelpHashBase + static_cast<unsigned>(i), kHelp[i]};
    for (int i = 0; i < NPARAM; ++i) ours[m++] = {kLabelHashBase + static_cast<unsigned>(i), kLabel[i]};
    for (unsigned i = 1; i < m; ++i)  // insertion sort by hash (unsigned): the game's search is unsigned
        for (unsigned j = i; j > 0 && ours[j - 1].hash > ours[j].hash; --j) {
            const LangEntry t = ours[j];
            ours[j] = ours[j - 1];
            ours[j - 1] = t;
        }

    LangEntry *merged = static_cast<LangEntry *>(reinterpret_cast<void *(*)(unsigned)>(kGameAlloc)((n + m) * 8));
    if (!merged) return;
    unsigned a = 0, b = 0, o = 0;
    while (a < n || b < m) {
        if (b >= m || (a < n && cur[a].hash <= ours[b].hash)) merged[o++] = cur[a++];
        else merged[o++] = ours[b++];
    }
    *reinterpret_cast<unsigned *>(kLangCountVar) = n + m;
    *reinterpret_cast<LangEntry **>(kLangTablePtr) = merged;
    g_merged_table = merged;
    log_line("language table: merged %u entries into %u (table %p -> %p)", m, n, static_cast<void *>(cur),
             static_cast<void *>(merged));
}

// FESetString(FEString*, const 16-bit chars*) copies the text and marks the string dirty. It lets us pass the
// degree sign (U+00B0) without guessing the byte encoding that FEPrintf expects.
void set_wide_text(void *fe_string, const char *ascii) {
    static unsigned short buf[96];
    size_t n = 0;
    for (; ascii[n] && n < 95; ++n) {
        const unsigned char c = static_cast<unsigned char>(ascii[n]);
        buf[n] = c == 1 ? 0x00B0 : c;  // character 1 stands for the degree sign
    }
    buf[n] = 0;
    reinterpret_cast<void (*)(void *, const unsigned short *)>(kFESetString)(fe_string, buf);
}

// ------------------------------------------------------------------------------------------------
// Options > Camera: a category screen built from the game's own Options layout (Pause_Options.fng).
// It reuses the "Player" category (SetupPlayer) so the screen, its Defaults/Accept/Back buttons and the
// "settings changed?" logic stay native, then swaps the Player rows for ours:
//   row 1  the game's own Camera selector (PODriveCam, changes the player's camera view)
//   row 2+ one row per preset value, editing the preset that the selector currently points at
// ------------------------------------------------------------------------------------------------
volatile bool g_cam_opts_next = false;  // our Options entry was pressed
volatile bool g_cam_opts = false;       // the open Options screen is the camera screen
void *g_options_screen = nullptr;
void *g_pending_optmain = nullptr;
void *g_hint_pending_screen = nullptr;  // the Help hint is added on this screen's first tick

using ThisVoid1Fn = void(__attribute__((thiscall)) *)(void *);
using ThisVoidFn = void(__attribute__((fastcall)) *)(void *, void *);
using ToggleCtorFn = void *(__attribute__((thiscall)) *)(void *, int);
using AddToggleFn = unsigned(__attribute__((thiscall)) *)(void *, void *, int);
using ActFn = void(__attribute__((thiscall)) *)(void *, const char *, unsigned);
using BlinkFn = void(__attribute__((thiscall)) *)(void *, unsigned);
using VirtFn = void(__attribute__((thiscall)) *)(void *);
using AllocFn = void *(*)(unsigned);
ThisVoidFn g_orig_setupplayer, g_orig_optmain_setup, g_orig_restoredefaults, g_orig_dialog_build;

void *g_toggle_vtbl[15];    // our parameter rows (FEToggleWidget behaviour with our Act/Draw)
void *g_drivecam_vtbl[15];  // PODriveCam with Act wrapped so the other rows refresh when the view changes
void *g_drivecam_widget = nullptr;  // the game's Camera row on the open screen

void *options_settings() { return *reinterpret_cast<void **>(*reinterpret_cast<unsigned char **>(kFEDatabasePtr) + 0x10); }

int camera_type_being_edited() {
    // The selector row stores the chosen camera in PlayerSettings.CurCam; map it to a POV preset.
    const int player = *reinterpret_cast<int *>(kPlayerToEditVar);
    const int cam = *reinterpret_cast<int *>(static_cast<unsigned char *>(options_settings()) + (player * 5 + 0x23) * 4);
    const int type = reinterpret_cast<int (*)(int)>(kPovFromPlayerCamera)(cam);
    return (type >= 0 && type < kNumPresets) ? type : g_cur_type;
}

void format_value(char *out, size_t n, int path, float v) {
    const char *deg = g_degree_sign ? "\x01" : " deg";
    switch (path) {
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
void store_value(int type, int path, float v) {
    const float base = g_have_base[type] ? g_base[type][path] : v;
    if (std::fabs(v - base) < 1e-3f) {
        g_ovr[type].set[path] = false;
    } else {
        g_ovr[type].set[path] = true;
        g_ovr[type].val[path] = v;
    }
    log_line("edit %s.%s = %g (base %g)%s", kPresetName[type], kKey[path], v, base,
             g_ovr[type].set[path] ? "" : " -> back to game value");
}

void __attribute__((thiscall)) param_row_act(void *self, const char *pkg, unsigned data) {
    (void)pkg;
    if (data != kPadLeft && data != kPadRight) return;
    const int path = field<int>(self, 0x5C);
    if (path < 0 || path >= NPARAM) return;
    const int type = camera_type_being_edited();
    const Range &r = kRange[path];
    float v = effective(type, path);
    if (path == P_TILT) {
        v = data == kPadRight ? 1.0f : 0.0f;
    } else {
        const bool fine = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        v += (data == kPadRight ? 1.0f : -1.0f) * (fine ? r.fine : r.step);
        v = std::round(v / r.fine) * r.fine;  // on the fine grid, so the ini gets 2.3 rather than 2.3000002
        v = v < r.min ? r.min : (v > r.max ? r.max : v);
    }
    store_value(type, path, v);
    field<unsigned char>(self, 0x2A) = 1;  // same as PODriveCam::Act
    void **vtbl = field<void **>(self, 0);
    reinterpret_cast<BlinkFn>(vtbl[14])(self, data);  // arrow blink
    reinterpret_cast<VirtFn>(vtbl[3])(self);          // Draw
}

void __attribute__((thiscall)) param_row_draw(void *self) {
    const int path = field<int>(self, 0x5C);
    if (path < 0 || path >= NPARAM) return;
    char value[32];
    format_value(value, sizeof(value), path, effective(camera_type_being_edited(), path));
    // The label goes in as a language hash, like PODriveCam::Draw does for "Camera". FEStatWidget::Position
    // centres a label on the column edge using the width of its literal text, which a language-hash label leaves
    // out, so the game's labels end at the edge (right-aligned). Text set with FESetString was measured and centred.
    reinterpret_cast<void (*)(void *, unsigned)>(kSetLangHashStr)(field<void *>(self, 0x2C), kLabelHashBase + path);
    set_wide_text(field<void *>(self, 0x30), value);
}

// NFSMWExtraOptions (EnableHiddenCameraModes) names the Drift camera with the language string "DRIFT", the only
// all-caps name in the list. The game has its own "Drift" string, translated like the rest, so use that instead.
ThisVoidFn g_orig_drivecam_draw;
void __attribute__((fastcall)) hook_drivecam_draw(void *self, void *edx) {
    g_orig_drivecam_draw(self, edx);
    void *value = field<void *>(self, 0x30);
    if (value && field<unsigned>(value, 0x60) == kDriftUpperHash)  // FEString::LabelHash
        reinterpret_cast<void (*)(void *, unsigned)>(kSetLangHashStr)(value, kDriftHash);
}

void __attribute__((thiscall)) drivecam_row_act(void *self, const char *pkg, unsigned data) {
    reinterpret_cast<ActFn>(kDriveCamAct)(self, pkg, data);
    if (g_options_screen) reinterpret_cast<ThisVoid1Fn>(kRefreshWidgets)(g_options_screen);  // other rows follow the new view
}

void init_option_vtables() {
    void **src = reinterpret_cast<void **>(kDriveCamVtable);
    for (int i = 0; i < 15; ++i) g_toggle_vtbl[i] = g_drivecam_vtbl[i] = src[i];
    g_toggle_vtbl[0] = reinterpret_cast<void *>(kToggleDelDtor);
    g_toggle_vtbl[1] = reinterpret_cast<void *>(&param_row_act);
    g_toggle_vtbl[3] = reinterpret_cast<void *>(&param_row_draw);
    g_drivecam_vtbl[1] = reinterpret_cast<void *>(&drivecam_row_act);
}

void add_toggle_row(void *screen, void *widget) {
    reinterpret_cast<AddToggleFn>(kAddToggleOption)(screen, widget, 1);
}

// Our labels are positioned exactly like the Camera row's now; in case the menu lays them out differently anyway,
// they also take the Camera label's x (FEObjData::Pos.x) on every frame.
void keep_titles_right_aligned(void *screen) {
    void *cam_title = g_drivecam_widget ? field<void *>(g_drivecam_widget, 0x2C) : nullptr;
    void *cd = cam_title ? field<void *>(cam_title, 0x2C) : nullptr;
    if (!cd) return;
    void *head = static_cast<unsigned char *>(screen) + 0x2C;
    void *node = field<void *>(screen, 0x2C);
    for (int n = 0; node && node != head && n < 16; ++n) {
        void *w = static_cast<unsigned char *>(node) - 4;
        node = field<void *>(w, 4);
        if (field<void **>(w, 0) != g_toggle_vtbl) continue;
        void *title = field<void *>(w, 0x2C);
        void *td = title ? field<void *>(title, 0x2C) : nullptr;
        if (td && field<float>(td, 0x1C) != field<float>(cd, 0x1C)) {
            static int logged = 0;
            if (logged < 16) {
                ++logged;
                log_line("row %d label x %.1f -> %.1f (Camera row)", n, field<float>(td, 0x1C), field<float>(cd, 0x1C));
            }
            field<float>(td, 0x1C) = field<float>(cd, 0x1C);
            field<unsigned>(title, 0x1C) |= (1u << 24) | (1u << 25);  // FF_DirtyTransform | FF_Dirty
        }
    }
}

void build_camera_options(void *screen) {
    g_options_screen = screen;
    memcpy(g_ovr_open, g_ovr, sizeof(g_ovr));
    reinterpret_cast<ThisVoid1Fn>(kClearWidgets)(screen);  // drop the Player rows SetupPlayer just added

    const auto alloc = reinterpret_cast<AllocFn>(kGameAlloc);
    if (void *w = alloc(0x5C)) {  // the game's own Camera selector
        reinterpret_cast<ToggleCtorFn>(kDriveCamCtor)(w, 1);
        field<void **>(w, 0) = g_drivecam_vtbl;
        add_toggle_row(screen, w);
        g_drivecam_widget = w;
    }
    for (int p = 0; p < NPARAM; ++p) {
        void *w = alloc(0x60);  // FEToggleWidget (0x5C) plus our parameter index
        if (!w) continue;
        reinterpret_cast<ToggleCtorFn>(kToggleCtor)(w, 1);
        field<void **>(w, 0) = g_toggle_vtbl;
        field<int>(w, 0x5C) = p;
        add_toggle_row(screen, w);
    }
    // The title object is a group of strings (text and shadow). Set the hash the way the game's own screens do,
    // then also print into the group so it does not depend on the table lookup.
    const char *pkg = field<const char *>(screen, 0x10);
    reinterpret_cast<SetLangHashPkgFn>(kSetLangHashPkg)(pkg, kHeaderTextHash, kTitleHash);
    reinterpret_cast<FEPrintfPkgFn>(kFEPrintfPkg)(pkg, static_cast<int>(kHeaderTextHash), "%s", "CAMERA");
    reinterpret_cast<void (*)(const char *, unsigned, unsigned)>(kSetTextureHashPkg)(pkg, kHeaderIconHash, g_icon_shown);
    reinterpret_cast<ThisVoid1Fn>(kRefreshWidgets)(screen);
    g_hint_pending_screen = screen;  // the Help hint is cloned on the first tick, once the layout is live
    log_line("Options > Camera built (screen=%p, editing %s)", screen, kPresetName[camera_type_being_edited()]);
}


void __attribute__((fastcall)) hook_setupplayer(void *self, void *edx) {
    g_orig_setupplayer(self, edx);
    if (!g_cam_opts_next) {
        g_cam_opts = false;
        return;
    }
    g_cam_opts_next = false;
    g_cam_opts = true;
    ensure_language_entries();
    build_camera_options(self);
}

using BoolThisFn = bool(__attribute__((fastcall)) *)(void *, void *);
BoolThisFn g_orig_didnotchange;
ThisVoidFn g_orig_restoreoriginals;

// Back asks "discard changes?" only when this says something changed; the camera values count as well.
bool __attribute__((fastcall)) hook_didnotchange(void *self, void *edx) {
    const bool unchanged = g_orig_didnotchange(self, edx);
    if (g_cam_opts && self == g_options_screen && camera_changed()) return false;
    return unchanged;
}

// The discard was confirmed: the game puts its own values back; put ours back too.
void __attribute__((fastcall)) hook_restoreoriginals(void *self, void *edx) {
    g_orig_restoreoriginals(self, edx);
    if (g_cam_opts && self == g_options_screen && camera_changed()) {
        memcpy(g_ovr, g_ovr_open, sizeof(g_ovr));
        log_line("camera changes discarded");
    }
}

void __attribute__((fastcall)) hook_restoredefaults(void *self, void *edx) {
    const int category = *reinterpret_cast<int *>(static_cast<unsigned char *>(options_settings()) + 0x24);
    if (g_cam_opts && category == 4) {
        const int type = camera_type_being_edited();
        memset(&g_ovr[type], 0, sizeof(g_ovr[type]));
        log_line("Defaults: cleared all overrides for %s", kPresetName[type]);
        reinterpret_cast<ThisVoid1Fn>(kRefreshWidgets)(self);
        return;
    }
    g_orig_restoredefaults(self, edx);
}

// ------------------------------------------------------------------------------------------------
// The visible "2 Help" hint: a clone of the layout's own "1 Defaults" hint (FEGroup::Clone), placed one hint to the
// right. The clone is made on the screen's first tick, after the layout's own setup has run. Its alpha follows the
// Defaults hint every frame, so it fades in and out together with the rest of the hint bar.
// ------------------------------------------------------------------------------------------------
struct HintMirror {
    void *screen;
    void *clone;
    void *pkg;
    int ticks;
    int n;
    void *orig[8];
    void *copy[8];
};
HintMirror g_mirror;

void *find_parent(void *container, bool is_pkg, void *target, bool *parent_is_pkg) {
    void *n = field<void *>(container, is_pkg ? 0x4C : 0x64);  // package Objects head / group Children head
    for (int i = 0; n && i < 1024; ++i) {
        if (n == target) {
            *parent_is_pkg = is_pkg;
            return container;
        }
        if (field<int>(n, 0x18) == 5)
            if (void *r = find_parent(n, false, target, parent_is_pkg)) return r;
        n = field<void *>(n, 4);  // FEMinNode: vptr, next, prev
    }
    return nullptr;
}

bool is_key_glyph(unsigned tex) {
    return tex == kPcKey1Texture || tex == kPcKey4Texture || tex == kCircleTexture || tex == kPcKey2Texture;
}

// A clicked hint runs its hit-box's message responses (FEMessageResponse list at FEObject+0x34: count +0x38,
// head +0x3C; node: MsgID +0xC, Count +0x10, FEResponse array +0x14 of {ID, Param, Target}). On the clone, a
// response that posts BUTTON5 (Defaults) is changed to post our own message straight to the screen.
int retarget_responses(void *orig, void *copy) {
    int changed = 0;
    void *ro = field<void *>(orig, 0x3C);
    void *rc = field<void *>(copy, 0x3C);
    for (unsigned i = 0, n = field<unsigned>(copy, 0x38); rc && i < n && i < 32; ++i) {
        const unsigned msg = field<unsigned>(rc, 0xC);
        const unsigned cnt = field<unsigned>(rc, 0x10);
        unsigned *list = field<unsigned *>(rc, 0x14);
        const unsigned *olist = ro ? field<unsigned *>(ro, 0x14) : nullptr;
        for (unsigned k = 0; list && k < cnt && k < 16; ++k) {
            unsigned *r = list + k * 3;  // {ID, Param, Target}
            log_line("  hint response msg=%08x #%u: id=%u param=%08x target=%08x%s", msg, k, r[0], r[1], r[2],
                     list == olist ? " (shared with the original!)" : "");
            if (r[1] == kButton5Msg && (r[0] == 1 || r[0] == 2) && list != olist) {
                r[0] = 2;  // MR_PostMessageToGame: straight to the screen
                r[1] = kHelpClickMsg;
                ++changed;
            }
        }
        rc = field<void *>(rc, 4);
        ro = ro ? field<void *>(ro, 4) : nullptr;
    }
    return changed;
}

// FEObject's copy constructor never initialises the cached render object pointer (+0x58), so a clone carries
// garbage there. The renderer then draws nothing for it, and unloading the package crashes in
// FERenderObject::Clear. Clearing it lets the renderer build fresh render objects for the clone.
// Alpha mirroring keeps the clone fading in and out with the Defaults hint; the clickable hit-box (FF_MouseObject)
// is left alone so its own hover highlight still works.
// Every object of the clone gets its own name hash and GUID. The mouse code finds a hit-box's screen offset by
// searching for its name hash (OffsetCalculatron), and responses address other objects by GUID, so with the
// original's names and GUIDs the clone's hit area landed on the Defaults hint and hovering it lit up Defaults.
void *g_pair_orig[32];
void *g_pair_copy[32];
int g_pair_n = 0;

int prepare_clone(void *orig, void *copy, int depth, bool mirror) {
    int changed = 0;
    if (g_pair_n < 32) {
        g_pair_orig[g_pair_n] = orig;
        g_pair_copy[g_pair_n] = copy;
        field<unsigned>(copy, 0x10) = kHelpHintHash + 0x100 + g_pair_n;  // NameHash
        field<unsigned>(copy, 0xC) = 0xCA3E2000 + g_pair_n;               // GUID
        ++g_pair_n;
    }
    field<void *>(copy, 0x58) = nullptr;
    // The clone's object data (FEObjData: colour, pivot, position, rotation, size, plus image UVs) came out blank:
    // position (0,0,0) made the renderer skip it. Copy it from the original.
    void *od = field<void *>(orig, 0x2C);
    void *cd = field<void *>(copy, 0x2C);
    const unsigned osz = field<unsigned>(orig, 0x30), csz = field<unsigned>(copy, 0x30);
    const unsigned n = osz < csz ? osz : csz;
    if (od && cd && od != cd && n >= 0x44 && n <= 0x400) memcpy(cd, od, n);
    else log_line("  hint object %p: data not copied (orig %p/%u, copy %p/%u)", copy, od, osz, cd, csz);
    field<unsigned>(copy, 0x1C) |= (1u << 23) | (1u << 24) | (1u << 25);  // dirty colour, transform, object
    if (field<unsigned>(copy, 0x1C) & (1u << 17)) mirror = false;
    if (mirror && g_mirror.n < 8) {
        g_mirror.orig[g_mirror.n] = orig;
        g_mirror.copy[g_mirror.n] = copy;
        ++g_mirror.n;
    }
    changed += retarget_responses(orig, copy);
    const int type = field<int>(copy, 0x18);
    if (type == 2) {  // string: the label
        reinterpret_cast<FEPrintfFn>(kFEPrintf)(copy, "%s", "Help");
    } else if (type == 1) {  // image: the key glyph (hit-box backings keep their texture)
        const unsigned tex = field<unsigned>(copy, 0x24);
        const bool glyph = is_key_glyph(tex);
        log_line("  hint image %p texture %08x%s", copy, tex, glyph ? " -> PC_KEY_2" : "");
        if (glyph) reinterpret_cast<void (*)(void *, unsigned)>(kSetTextureHash)(copy, kPcKey2Texture);
    } else if (type == 5 && depth < 4) {
        void *o = field<void *>(orig, 0x64);
        void *c = field<void *>(copy, 0x64);
        for (unsigned i = 0, n = field<unsigned>(copy, 0x60); o && c && i < n && i < 16; ++i) {
            changed += prepare_clone(o, c, depth + 1, mirror);
            o = field<void *>(o, 4);
            c = field<void *>(c, 4);
        }
    }
    return changed;
}

bool in_hint_clone(void *obj) {
    for (int i = 0; i < g_pair_n; ++i)
        if (g_pair_copy[i] == obj) return true;
    return false;
}

// Responses that point at one of the original hint's objects (by GUID) are pointed at the clone's counterpart.
int remap_response_targets() {
    int remapped = 0;
    for (int i = 0; i < g_pair_n; ++i) {
        void *rc = field<void *>(g_pair_copy[i], 0x3C);
        for (unsigned k = 0, n = field<unsigned>(g_pair_copy[i], 0x38); rc && k < n && k < 32; ++k, rc = field<void *>(rc, 4)) {
            unsigned *list = field<unsigned *>(rc, 0x14);
            for (unsigned j = 0, cnt = field<unsigned>(rc, 0x10); list && j < cnt && j < 16; ++j) {
                unsigned &target = list[j * 3 + 2];
                for (int m = 0; m < g_pair_n && target; ++m) {
                    if (target == field<unsigned>(g_pair_orig[m], 0xC)) {
                        target = field<unsigned>(g_pair_copy[m], 0xC);
                        ++remapped;
                        break;
                    }
                }
            }
        }
    }
    return remapped;
}

bool responds_to(void *obj, unsigned msg) {
    void *r = field<void *>(obj, 0x3C);
    for (unsigned i = 0, n = field<unsigned>(obj, 0x38); r && i < n && i < 32; ++i, r = field<void *>(r, 4))
        if (field<unsigned>(r, 0xC) == msg) return true;
    return false;
}

// The label's hover look comes from script events, not responses: the hit-box's mouse-over script posts 0x696851AD
// (label: HIGHLIGHT) and its mouse-exit script posts 0x1C8E4190 (label: UNHIGHLIGHT), both addressed to the whole
// package (target 0xFFFFFFFC). The package hands them to the objects in its message target lists, which name the
// Defaults label only. So on the clone, an event like that is addressed straight to the clone object that responds
// to it. (FEObject scripts: list at +0x44, count +0x48, head +0x4C; FEScript events: count +0x24, array +0x28 of
// {EventID, Target, tTime}. The copy constructor gives each cloned script its own event array.)
int remap_script_events() {
    int remapped = 0;
    for (int i = 0; i < g_pair_n; ++i) {
        void *so = field<void *>(g_pair_orig[i], 0x4C);
        void *sc = field<void *>(g_pair_copy[i], 0x4C);
        for (unsigned k = 0, n = field<unsigned>(g_pair_copy[i], 0x48); sc && k < n && k < 32; ++k) {
            unsigned *ev = field<unsigned *>(sc, 0x28);
            const unsigned *oev = so ? field<unsigned *>(so, 0x28) : nullptr;
            for (unsigned j = 0, cnt = field<unsigned>(sc, 0x24); ev && ev != oev && j < cnt && j < 32; ++j) {
                const unsigned msg = ev[j * 3];
                unsigned &target = ev[j * 3 + 1];
                const unsigned before = target;
                if (target == 0 || target == 0xFFFFFFFC) {
                    int responder = -1, responders = 0;
                    for (int m = 0; m < g_pair_n; ++m)
                        if (responds_to(g_pair_orig[m], msg)) {
                            responder = m;
                            ++responders;
                        }
                    if (responders == 1 && msg != 0x1B3909AA) target = field<unsigned>(g_pair_copy[responder], 0xC);
                } else {
                    for (int m = 0; m < g_pair_n; ++m)
                        if (target == field<unsigned>(g_pair_orig[m], 0xC)) {
                            target = field<unsigned>(g_pair_copy[m], 0xC);
                            break;
                        }
                }
                if (target != before) ++remapped;
                log_line("  hint event: object %d script %08x msg %08x t=%u target %08x%s", i, field<unsigned>(sc, 0x30),
                         msg, ev[j * 3 + 2], before, target != before ? " -> clone" : "");
            }
            sc = field<void *>(sc, 4);
            so = so ? field<void *>(so, 4) : nullptr;
        }
    }
    return remapped;
}

void add_help_hint_unguarded(void *screen) {
    const char *pkg_name = field<const char *>(screen, 0x10);
    const auto find_group = reinterpret_cast<void *(*)(const char *, unsigned)>(kFEngFindGroup);
    void *defaults = find_group(pkg_name, kPcDefaultsHint);
    void *accept = find_group(pkg_name, kPcAcceptHint);
    if (!defaults) {
        log_line("Help hint: the PC Defaults hint group was not found in %s", pkg_name);
        return;
    }
    void *pkg = reinterpret_cast<void *(__attribute__((thiscall)) *)(void *, const char *)>(kFEngFindPackage)(
        *reinterpret_cast<void **>(kFEngVar), pkg_name);
    bool parent_is_pkg = false;
    void *parent = pkg ? find_parent(pkg, true, defaults, &parent_is_pkg) : nullptr;
    if (!parent) {
        log_line("Help hint: could not find the Defaults hint's parent");
        return;
    }
    void *clone = reinterpret_cast<void *(__attribute__((thiscall)) *)(void *, int)>(kGroupClone)(defaults, 0);
    if (!clone) {
        log_line("Help hint: Clone returned null");
        return;
    }
    g_mirror.screen = screen;
    g_mirror.n = 0;
    g_pair_n = 0;
    const int retargeted = prepare_clone(defaults, clone, 0, true);
    field<unsigned>(clone, 0x10) = kHelpHintHash;  // NameHash
    const int remapped = remap_response_targets();
    const int events = remap_script_events();
    log_line("Help hint: %d objects renamed, %d response targets and %d script events moved to the clone", g_pair_n,
             remapped, events);

    float dx = 0, dy = 0, ax = 0, ay = 0;
    const auto get_center = reinterpret_cast<void (*)(void *, float *, float *)>(kFEngGetCenter);
    get_center(defaults, &dx, &dy);
    float spacing = 120.0f;
    if (accept) {
        get_center(accept, &ax, &ay);
        const float gap = dx - ax;  // same gap as between the Accept and Defaults hints
        if (gap > 1.0f || gap < -1.0f) spacing = gap;
    }
    // Move only x: FEngSetCenter would also reset z (depth), which hides the object.
    if (void *data = field<void *>(clone, 0x2C)) field<float>(data, 0x1C) += spacing;

    if (parent_is_pkg) reinterpret_cast<void(__attribute__((thiscall)) *)(void *, void *)>(kPackageAddObject)(parent, clone);
    else reinterpret_cast<void(__attribute__((thiscall)) *)(void *, void *)>(kGroupAddObject)(parent, clone);
    // The package keeps a list of its mouse-reactive objects; rebuild it so the clone's hit-box is clickable.
    reinterpret_cast<ThisVoid1Fn>(kBuildMouseList)(pkg);
    g_mirror.clone = clone;
    g_mirror.pkg = pkg;
    g_mirror.ticks = 0;
    log_line("Help hint: added clone %p at (%.1f, %.1f) [defaults (%.1f, %.1f), accept (%.1f, %.1f)], parent %s, "
             "%d objects mirrored, %d click responses retargeted",
             clone, dx + spacing, dy, dx, dy, ax, ay, parent_is_pkg ? "package" : "group", g_mirror.n, retargeted);
}

void add_help_hint(void *screen) {
    if (!GetPrivateProfileIntA("features", "help_hint", 1, g_ini)) return;
    if (setjmp(g_guard_jb) == 0) {
        g_guard_on = true;
        add_help_hint_unguarded(screen);
        g_guard_on = false;
    } else {
        g_guard_on = false;
        g_mirror.n = 0;
        log_line("Help hint: failed (see nfsmw_cam_crash.log); the rest of the screen is unaffected");
    }
}

unsigned script_id(void *obj) {
    void *sc = field<void *>(obj, 0x54);  // pCurrentScript
    return sc ? field<unsigned>(sc, 0x30) : 0;  // FEScript::ID
}

// Each frame: run the same script as the original (appear, fade out, ...) and copy its alpha, so the clone looks
// and animates like the Defaults hint. The clone's hit-box is not in this list, so its hover highlight is its own.
void sync_hint(void *screen) {
    if (g_mirror.screen != screen || !g_mirror.clone) return;
    for (int i = 0; i < g_mirror.n; ++i) {
        void *o = g_mirror.orig[i];
        void *c = g_mirror.copy[i];
        const unsigned oid = script_id(o);
        // The labels' hover scripts are their own (see remap_script_events); only the bar's appear/fade is shared.
        if (field<int>(c, 0x18) == 2 && (oid == kScriptHighlight || oid == kScriptUnhighlight)) continue;
        if (oid && oid != script_id(c))
            reinterpret_cast<void(__attribute__((thiscall)) *)(void *, unsigned, int)>(kObjectSetScript)(c, oid, 1);
        void *od = field<void *>(o, 0x2C);
        void *cd = field<void *>(c, 0x2C);
        if (!od || !cd) continue;
        const int a = field<int>(od, 0xC);  // FEColor {b, g, r, a}
        if (field<int>(cd, 0xC) != a) {
            field<int>(cd, 0xC) = a;
            field<unsigned>(c, 0x1C) |= (1u << 23) | (1u << 25);  // FF_DirtyColor | FF_Dirty
        }
    }
    const int t = ++g_mirror.ticks;
    if (t == 3 || t == 90) {  // diagnostics: what the renderer sees for the original and the clone
        bool listed = false;
        void *n = field<void *>(g_mirror.pkg, 0x4C);
        for (int k = 0; n && k < 1024; ++k, n = field<void *>(n, 4)) listed |= n == g_mirror.clone;
        log_line("Help hint tick %d: clone in package object list: %s", t, listed ? "yes" : "NO");
        for (int i = 0; i < g_mirror.n; ++i) {
            void *pair[2] = {g_mirror.orig[i], g_mirror.copy[i]};
            for (int k = 0; k < 2; ++k) {
                void *obj = pair[k];
                void *d = field<void *>(obj, 0x2C);
                log_line("  %s %d %p type=%d flags=%08x script=%08x alpha=%d pos=(%.1f, %.1f, %.1f) cached=%p",
                         k ? "clone" : "orig ", i, obj, field<int>(obj, 0x18), field<unsigned>(obj, 0x1C), script_id(obj),
                         d ? field<int>(d, 0xC) : -1, d ? field<float>(d, 0x1C) : 0.0f, d ? field<float>(d, 0x20) : 0.0f,
                         d ? field<float>(d, 0x24) : 0.0f, field<void *>(obj, 0x58));
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Help key: the layout has no help panel, so the explanation opens in the game's own message box. Its title and
// icon are adjusted when the box builds itself (feDialogScreen::BuildFromConfig).
// ------------------------------------------------------------------------------------------------
using ShowOneButtonFn = unsigned (*)(const char *, const char *, int, unsigned, unsigned, unsigned, unsigned);
using NotifyFn = void(__attribute__((fastcall)) *)(void *, void *, unsigned, void *, unsigned, unsigned);
NotifyFn g_orig_notify = nullptr;
bool g_help_dialog_open = false;
bool g_help_dialog_pending = false;  // the next message box to build is ours

void show_help_for_current_row(void *screen) {
    ensure_language_entries();
    void *row = field<void *>(screen, 0x34);  // UIWidgetMenu::pCurrentOption
    if (!row) return;
    void **vtbl = field<void **>(row, 0);
    unsigned hash;
    if (vtbl == g_toggle_vtbl) {
        const int path = field<int>(row, 0x5C);
        if (path < 0 || path >= NPARAM) return;
        hash = kHelpHashBase + path;
    } else if (vtbl == g_drivecam_vtbl) {
        hash = kHelpHashBase + NPARAM;
    } else {
        return;
    }
    g_help_dialog_pending = true;
    reinterpret_cast<ShowOneButtonFn>(kShowOneButton)(field<const char *>(screen, 0x10),
                                                      reinterpret_cast<const char *>(kInGameDialogName), kDialogInfo,
                                                      kDialogOkText, kDialogMessageNo, kDialogMessageNo, hash);
}

void __attribute__((fastcall)) hook_dialog_build(void *self, void *edx) {
    g_orig_dialog_build(self, edx);
    if (!g_help_dialog_pending) return;
    g_help_dialog_pending = false;
    const char *pkg = field<const char *>(self, 0x10);
    reinterpret_cast<SetLangHashPkgFn>(kSetLangHashPkg)(pkg, kHeaderTextHash, kDialogTitleHash);
    reinterpret_cast<FEPrintfPkgFn>(kFEPrintfPkg)(pkg, static_cast<int>(kHeaderTextHash), "%s", "CAMERA INFO");
    if (GetPrivateProfileIntA("features", "hide_dialog_icon", 1, g_ini)) {
        const auto set_invisible = reinterpret_cast<void (*)(const char *, unsigned)>(kFEngSetInvisiblePkg);
        set_invisible(pkg, kDialogIcon1);
        set_invisible(pkg, kDialogIcon2);
    }
    log_line("Help message box styled (package %s)", pkg ? pkg : "?");
}

// The Options layout only forwards the input messages it has responses for (Defaults, Accept, Back, arrows), so a
// "2" key press never reaches this screen as a message. The screen does receive FEMSG_SCREEN_TICK every frame, so
// the Help key is read from the keyboard on those ticks.
void __attribute__((fastcall)) hook_notify(void *self, void *edx, unsigned msg, void *obj, unsigned p1, unsigned p2) {
    if (g_cam_opts) {
        if (msg == kHelpClickMsg || (msg == kButton5Msg && obj && in_hint_clone(obj))) {
            log_line("Help hint clicked (msg %08x, object %p)", msg, obj);
            if (!g_help_dialog_open) {
                g_help_dialog_open = true;
                show_help_for_current_row(self);
            }
            return;
        }
        if (obj && in_hint_clone(obj)) log_line("message %08x from the Help hint", msg);
        if (msg == kScreenTick) {
            if (g_hint_pending_screen == self) {
                g_hint_pending_screen = nullptr;
                add_help_hint(self);
            }
            sync_hint(self);
            keep_titles_right_aligned(self);
            static bool was_down = false;
            static DWORD last_ms = 0;
            const bool down = (GetAsyncKeyState('2') & 0x8000) || (GetAsyncKeyState(VK_NUMPAD2) & 0x8000);
            const DWORD now = GetTickCount();
            if (down && !was_down && !g_help_dialog_open && now - last_ms > 400) {
                last_ms = now;
                g_help_dialog_open = true;
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
                log_line("camera settings applied and saved to %s", g_ini);
            }
        }
    }
    g_orig_notify(self, edx, msg, obj, p1, p2);
}

// ---- Options main menu entry ("Camera" next to Audio / Video / Gameplay / Player / Controller) ----
void __attribute__((thiscall)) options_entry_react(void *self, const char *pkg, unsigned data, void *obj, unsigned p1,
                                                    unsigned p2) {
    (void)self; (void)pkg; (void)obj; (void)p1; (void)p2;
    if (data != kButtonPressed) return;
    // Exactly what OMPlayer::React does: select the Player category. UIOptionsMain then opens the Options layout.
    *reinterpret_cast<int *>(static_cast<unsigned char *>(options_settings()) + 0x24) = 4;
    g_cam_opts_next = true;
}

void *g_options_entry_vtable[2] = {reinterpret_cast<void *>(kIconOptionDelDtor),
                                   reinterpret_cast<void *>(&options_entry_react)};

bool game_is_paused() {
    return reinterpret_cast<bool(__attribute__((thiscall)) *)(void *)>(kIsPausedFn)(reinterpret_cast<void *>(kGameFlowObj));
}

void add_options_entry(void *menu) {
    void *opt = reinterpret_cast<AllocFn>(kGameAlloc)(0x5C);
    if (!opt) return;
    reinterpret_cast<void(__attribute__((thiscall)) *)(void *, unsigned, unsigned, unsigned)>(kIconOptionCtor)(
        opt, g_icon_shown, kOptEntryHash, 0);
    field<void **>(opt, 0) = g_options_entry_vtable;
    field<unsigned char>(opt, 0x45) = 1;  // react immediately, like the game's own entries
    void *row = static_cast<unsigned char *>(menu) + 0x2C;
    void **vtbl = field<void **>(row, 0);
    // Same as IconScrollerMenu::AddOption, keeping the result: the row binds options to UI objects
    // "option_1".."option_N" in the layout and silently drops extras.
    void *img = reinterpret_cast<void *(__attribute__((thiscall)) *)(void *, void *)>(vtbl[2])(row, opt);
    if (img) {
        reinterpret_cast<void (*)(void *, unsigned)>(kSetTextureHash)(img, g_icon_shown);
        log_line("Options menu: added Camera entry (image=%p)", img);
    } else {
        log_line("Options menu: no free slot for the Camera entry");
    }
}

using SetInitialPosFn = void(__attribute__((fastcall)) *)(void *, void *, int);
SetInitialPosFn g_orig_initialpos;

// UIOptionsMain::Setup adds its icons and then lays them out with the row's SetInitialPos (the call is
// inlined, so it cannot be hooked at SetInitialOption). Our entry must be added right before that call.
void __attribute__((fastcall)) hook_initialpos(void *row, void *edx, int index) {
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
    char path[MAX_PATH] = {}, game[MAX_PATH] = {};
    GetModuleFileNameA(static_cast<HMODULE>(g_self_base), path, sizeof(path) - 16);
    GetModuleFileNameA(nullptr, game, sizeof(game));
    char *slash = strrchr(path, 92);  // 92 = backslash
    char *game_slash = strrchr(game, 92);
    if (!slash || !game_slash) return;
    strcpy(slash + 1, "nfsmw_cam.tpk");
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        log_line("texture pack %s not found; using the game's camera icon", path);
        return;
    }
    // Relative to the game folder, like the game's own file names, when the .asi lives inside it.
    const size_t game_len = static_cast<size_t>(game_slash + 1 - game);
    const char *name = _strnicmp(path, game, game_len) == 0 ? path + game_len : path;
    void *file = reinterpret_cast<void *(*)(const char *, int, int, void *, void *, int, int)>(kLoadResourceFile)(
        name, 1, 0, nullptr, nullptr, 0, 0);  // RESOURCE_FILE_GLOBAL
    const DWORD start = GetTickCount();
    while (file && !field<int>(file, 0x30) && GetTickCount() - start < 3000) {  // ResourceFile::LoadingFinishedFlag
        reinterpret_cast<int (*)()>(kServiceResourceLoading)();
        Sleep(1);
    }
    log_line("texture pack %s: %s after %lu ms", name, file && field<int>(file, 0x30) ? "loaded" : "NOT loaded",
             GetTickCount() - start);
}

bool texture_exists(unsigned hash) {
    return reinterpret_cast<void *(*)(unsigned, int, int)>(kGetTextureInfo)(hash, 0, 0) != nullptr;
}

void __attribute__((fastcall)) hook_optmain_setup(void *self, void *edx) {
    g_cam_opts_next = false;
    g_cam_opts = false;
    g_pending_optmain = nullptr;
    g_hint_pending_screen = nullptr;
    g_mirror.screen = nullptr;
    g_mirror.clone = nullptr;
    g_mirror.n = 0;
    ensure_language_entries();
    if (g_anchor && game_is_paused()) {  // only offered from the in-game pause menu
        // Read every preset's own values from the game so each can be edited without having been the active view.
        for (int t = 0; t < kNumPresets; ++t) {
            if (Pov *p = g_orig_getpov(g_anchor, nullptr, t)) {
                for (int i = 0; i < NPARAM; ++i) g_base[t][i] = get_field(*p, i);
                g_have_base[t] = true;
            }
        }
        g_pending_optmain = self;
        load_texture_pack();
        g_icon_shown = texture_exists(g_icon) ? g_icon : kSpeedtrapIconHash;
        if (g_icon_shown != g_icon) log_line("icon texture %08x is not loaded; using MILESTONE_SPEEDTRAP", g_icon);
    }
    g_orig_optmain_setup(self, edx);
    g_pending_optmain = nullptr;
}

template <typename Fn, typename Orig>
int hook(const char *name, uintptr_t addr, Fn detour, Orig *orig_out) {
    if (!GetPrivateProfileIntA("hooks", name, 1, g_ini)) {
        log_line("hook %s disabled by [hooks] in the ini", name);
        return 1;  // counts as "ok" so the total stays comparable
    }
    const MH_STATUS s = MH_CreateHook(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(detour),
                                      reinterpret_cast<void **>(orig_out));
    if (s != MH_OK) log_line("MH_CreateHook(%s) failed: %s", name, MH_StatusToString(s));
    return s == MH_OK;
}

}  // namespace

NFSMW_PLUGIN_MAIN() {
    g_log = fopen("nfsmw_cam.log", "w");
    log_line("nfsmw_cam 0.11.0 starting");
    g_crash_file = CreateFileA("nfsmw_cam_crash.log", FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(reinterpret_cast<void *>(&crash_logger), &mbi, sizeof(mbi))) g_self_base = mbi.AllocationBase;
    }
    crash_write("---- nfsmw_cam 0.11.0 session (tick %lu) ----", GetTickCount());
    AddVectoredExceptionHandler(1, crash_logger);
    ini_path_init();
    ini_load();
    int loaded = 0;
    for (auto &o : g_ovr)
        for (bool s : o.set) loaded += s;
    log_line("loaded %d override value(s) from %s", loaded, g_ini);
    g_degree_sign = GetPrivateProfileIntA("general", "degree_symbol", 1, g_ini) != 0;
    {
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

    if (!prologues_ok() || MH_Initialize() != MH_OK) {
        log_line("not hooking anything");
        return NFSMW_OK;
    }
    init_option_vtables();
    int ok = 0;
    ok += hook("getpov", kGetPov, hook_getpov, &g_orig_getpov);
    ok += hook("optmain", kOptionsMainSetup, hook_optmain_setup, &g_orig_optmain_setup);
    ok += hook("setupplayer", kSetupPlayer, hook_setupplayer, &g_orig_setupplayer);
    ok += hook("restoredefaults", kRestoreDefaults, hook_restoredefaults, &g_orig_restoredefaults);
    ok += hook("notify", kOptionsNotify, hook_notify, &g_orig_notify);
    ok += hook("didnotchange", kOptionsDidNotChange, hook_didnotchange, &g_orig_didnotchange);
    ok += hook("restoreoriginals", kRestoreOriginals, hook_restoreoriginals, &g_orig_restoreoriginals);
    ok += hook("initialpos", kIconScrollerSetInitialPos, hook_initialpos, &g_orig_initialpos);
    ok += hook("dialogbuild", kDialogBuild, hook_dialog_build, &g_orig_dialog_build);
    static const unsigned char kDrawPrologue[7] = {0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x77, 0x2C};
    if (memcmp(reinterpret_cast<const void *>(kDriveCamDraw), kDrawPrologue, sizeof(kDrawPrologue)) == 0)
        ok += hook("drivecamdraw", kDriveCamDraw, hook_drivecam_draw, &g_orig_drivecam_draw);
    else
        log_line("PODriveCam::Draw was patched by another mod; the Drift camera keeps its name");
    ok += (MH_EnableHook(MH_ALL_HOOKS) == MH_OK);
    log_line("hooks installed: %d (expect 11)", ok);
    return NFSMW_OK;
}
