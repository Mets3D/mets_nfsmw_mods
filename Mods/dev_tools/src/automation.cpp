/*
 * Lets a program (or an AI agent) test the game without anyone at the keyboard. The scenario is a text file,
 * <data folder>/scenarios/<name>.txt, named by the environment variable NFSMW_SCENARIO (or `scenario=` under [automation] in
 * the ini); one command per line, `#` starts a comment, arguments are words or key=value:
 *
 *   wait <seconds>                     wait_frames <n>
 *   wait_until <condition> [timeout=<seconds>]   conditions: frontend, main_menu, racing, state=<game flow state>
 *   press <accept|back|up|down|left|right|start> [times]   a menu press, as the pad does it (holds the menu's button for 100 ms)
 *   wait_until screen=<package name>   e.g. screen=MainMenu.fng
 *   log_players                        every local player's car, position and render port go to the split screen mod's log
 *   press_start <0-3|keyboard>         the Press Start screen of the split screen menus is told that device let go of Start
 *   screenshot <name> [width=960]      <data folder>/screenshots/<date>-<time>_<scenario>/<name>.png
 *   state                              logs the game's state: flow state, boot screen, view mode, players
 *   log <text>
 *   quit [code]
 *
 * The commands run from a hook on IDirect3DDevice9::Present (vtable slot 17, the device is the game's, *0x982BDC), so once
 * per frame, on the game's thread, and a screenshot sees the finished frame. A command that is not understood, or a wait that
 * times out (default 120 s), ends the game with exit code 2 (the log says why). The scenario file is read again whenever it
 * has grown after its last line ran, so a program can append commands to a game that is already running; without a quit
 * line the game keeps running.
 *
 * Game flow state *0x925E90: 3 front end, 6 racing (see GameFlow.hpp). BootFlowManager *0x91CF48 is null once the main menu
 * has been reached.
 */
#include "automation.h"

#include <d3d9.h>
#include <tlhelp32.h>

#include <string>
#include <vector>

#include "dev_tools.h"
#include "screenshot.h"
#include "split_screen_api.h"

namespace dev_tools::automation {

namespace {

constexpr uintptr_t kDevice = 0x982BDC, kGameFlowState = 0x925E90, kBootFlowManager = 0x91CF48, kFeDatabase = 0x91CF90, kViewMode = 0x982B40;
constexpr uintptr_t kExitFlag = 0x9257EC;  // set by WM_CLOSE
constexpr uintptr_t kFEng = 0x91CADC, kFindPackageWithControl = 0x516A90;  // cFEng: *kFEng
constexpr int kPresentSlot = 17;
constexpr DWORD kDefaultTimeoutMs = 120000;

using PresentFn = HRESULT(WINAPI *)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
PresentFn g_orig_present = nullptr;

std::string g_script_path, g_screenshot_dir;
std::vector<std::string> g_lines;
size_t g_next = 0;  // the next line to run
DWORD g_last_size_check = 0;
unsigned g_frame = 0;
DWORD g_last_present_ms = 0, g_game_thread = 0;

// What the script is waiting for. Only one at a time.
DWORD g_wait_until_ms = 0;
unsigned g_wait_until_frame = 0;
std::string g_condition;  // empty = no condition
DWORD g_condition_deadline = 0;

int game_state() { return *reinterpret_cast<int *>(kGameFlowState); }
bool boot_flow_over() { return *reinterpret_cast<void **>(kBootFlowManager) == nullptr; }

// The screen (FEng package) that has the controls: what a press goes to.
const char *current_screen() {
    void *feng = *reinterpret_cast<void **>(kFEng);
    if (!feng) return nullptr;
    char *package = reinterpret_cast<char *(__thiscall *)(void *)>(kFindPackageWithControl)(feng);
    return package ? *reinterpret_cast<const char **>(package + 0xC) : nullptr;
}

// The menus' buttons: cFEngJoyInput::MapJoyEventToFEPad (0x8F3428, 16 entries of 0x1C bytes) keeps whether each is held in State[port] (+0xC
// + 4 * port); the FEng reads the held buttons of pad 0 (every device is port 0 on PC) every frame and makes the presses out of them, so
// holding one for a moment is a press. The real events (HandleJoy) only rewrite a State when a device sends something.
constexpr uintptr_t kPadTable = 0x8F3428;
constexpr int kPadEntrySize = 0x1C, kPadStateOffset = 0xC;
struct Button {
    const char *name;
    int entry;
};
const Button kButtons[] = {{"up", 0}, {"down", 1}, {"left", 2}, {"right", 3}, {"accept", 4}, {"back", 5}, {"start", 7}};
constexpr DWORD kHoldMs = 100, kGapMs = 120;

int g_presses_left = 0;  // presses still to make
int g_pressed_entry = -1;
DWORD g_press_phase_until = 0;
bool g_button_down = false;

int *pad_state(int entry) { return reinterpret_cast<int *>(kPadTable + entry * kPadEntrySize + kPadStateOffset); }

void press_tick() {
    if (g_presses_left <= 0 && !g_button_down) return;
    const DWORD now = GetTickCount();
    if (static_cast<LONG>(now - g_press_phase_until) < 0) return;
    if (g_button_down) {
        *pad_state(g_pressed_entry) = 0;
        g_button_down = false;
        g_press_phase_until = now + kGapMs;
    } else {
        *pad_state(g_pressed_entry) = 1;
        g_button_down = true;
        --g_presses_left;
        g_press_phase_until = now + kHoldMs;
    }
}

bool press(const std::string &name, int times) {
    for (const Button &b : kButtons) {
        if (name != b.name) continue;
        g_pressed_entry = b.entry;
        g_presses_left = times;
        return true;
    }
    return false;
}

bool g_quitting = false;  // the game was asked to end itself; nothing more runs

DWORD WINAPI end_after_grace(void *) {
    Sleep(20000);
    g_log.line("automation: the game did not end by itself");
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}

// Exit code 0: the game is asked to leave its main loop (what WM_CLOSE does: the byte at 0x9257EC), which ends the process the
// way closing the window does; ExitProcess from inside a frame left the process half dead (window gone, process kept). If it has
// not ended 20 s later, or for any other exit code, the process is ended at once.
void quit(int code, const char *why) {
    g_exiting = true;
    g_log.line("automation: quit (%s), exit code %d, frame %u", why, code, g_frame);
    if (code == 0) {
        g_quitting = true;
        *reinterpret_cast<unsigned *>(kExitFlag) = 1;
        CreateThread(nullptr, 0, end_after_grace, nullptr, 0, nullptr);
        return;
    }
    TerminateProcess(GetCurrentProcess(), code);
}

std::vector<std::string> words(const std::string &line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && isspace(static_cast<unsigned char>(line[i]))) ++i;
        const size_t start = i;
        while (i < line.size() && !isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i > start) out.push_back(line.substr(start, i - start));
    }
    return out;
}

// The value of `key=` among the words, or `def`.
std::string arg(const std::vector<std::string> &w, const char *key, const char *def = "") {
    const size_t n = strlen(key);
    for (size_t i = 1; i < w.size(); ++i)
        if (w[i].compare(0, n, key) == 0 && w[i].size() > n && w[i][n] == '=') return w[i].substr(n + 1);
    return def;
}

bool condition_met(const std::string &c) {
    if (c.compare(0, 7, "screen=") == 0) {
        const char *screen = current_screen();
        return screen && _stricmp(screen, c.c_str() + 7) == 0;
    }
    if (c == "frontend") return game_state() == 3;
    if (c == "racing") return game_state() == 6;
    if (c == "main_menu") return game_state() == 3 && boot_flow_over();
    if (c.compare(0, 6, "state=") == 0) return game_state() == atoi(c.c_str() + 6);
    return false;
}

bool known_condition(const std::string &c) {
    return c == "frontend" || c == "racing" || c == "main_menu" || c.compare(0, 6, "state=") == 0 || c.compare(0, 7, "screen=") == 0;
}

void log_state() {
    const char *screen = nullptr;
    if (char *manager = *reinterpret_cast<char **>(kBootFlowManager)) {
        if (char *current = *reinterpret_cast<char **>(manager + 0xC)) screen = *reinterpret_cast<const char **>(current + 0xC);
    }
    const unsigned char *db = *reinterpret_cast<unsigned char **>(kFeDatabase);
    const char *top = current_screen();
    g_log.line("state: frame %u, %lu ms, game flow %d, screen %s, boot screen %s, view mode %d, players %d, game mode %#x", g_frame, since_start_ms(), game_state(),
               top ? top : "(none)", screen ? screen : "(none)", *reinterpret_cast<int *>(kViewMode), db ? db[0] : -1,
               db ? *reinterpret_cast<const unsigned *>(db + 0x12C) : 0u);
}

void read_script() {
    FILE *file = fopen(g_script_path.c_str(), "r");
    if (!file) return;
    std::vector<std::string> lines;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), file)) {
        std::string line = buffer;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        lines.push_back(line);
    }
    fclose(file);
    if (lines.size() > g_lines.size()) g_lines = lines;
}

void run(const std::string &line, IDirect3DDevice9 *device) {
    const std::vector<std::string> w = words(line.substr(0, line.find('#')));
    if (w.empty()) return;
    const std::string &cmd = w[0];
    g_log.line("automation: > %s", line.c_str());

    if (cmd == "wait" && w.size() >= 2) {
        g_wait_until_ms = GetTickCount() + static_cast<DWORD>(atof(w[1].c_str()) * 1000);
    } else if (cmd == "wait_frames" && w.size() >= 2) {
        g_wait_until_frame = g_frame + atoi(w[1].c_str());
    } else if (cmd == "wait_until" && w.size() >= 2) {
        if (!known_condition(w[1])) return quit(2, "unknown condition");
        g_condition = w[1];
        const std::string timeout = arg(w, "timeout");
        g_condition_deadline = GetTickCount() + (timeout.empty() ? kDefaultTimeoutMs : static_cast<DWORD>(atof(timeout.c_str()) * 1000));
    } else if (cmd == "press" && w.size() >= 2) {
        if (!press(w[1], w.size() >= 3 ? atoi(w[2].c_str()) : 1)) return quit(2, "press: unknown button");
    } else if (cmd == "log_players") {
        static const auto log_players = nfsmw::split_screen_api::find_log_players();
        if (!log_players) return quit(2, "log_players: nfsmw_split_screen_support is not loaded");
        log_players();
    } else if (cmd == "press_start" && w.size() >= 2) {
        static const auto press_start = nfsmw::split_screen_api::find_press_start();
        if (!press_start) return quit(2, "press_start: nfsmw_split_screen_support is not loaded");
        press_start(w[1] == "keyboard" ? -1 : atoi(w[1].c_str()));
    } else if (cmd == "screenshot" && w.size() >= 2) {
        const std::string path = g_screenshot_dir + w[1] + ".png";
        if (!screenshot::save(device, path.c_str(), atoi(arg(w, "width", "960").c_str()))) g_log.line("automation: the screenshot failed");
    } else if (cmd == "state") {
        log_state();
    } else if (cmd == "log") {
        g_log.line("automation: %s", line.substr(line.find("log") + 3).c_str());
    } else if (cmd == "quit") {
        quit(w.size() >= 2 ? atoi(w[1].c_str()) : 0, "script");
        return;
    } else {
        return quit(2, "command not understood");
    }
}

void tick(IDirect3DDevice9 *device) {
    ++g_frame;
    if (g_quitting) return;
    press_tick();
    if (const char *screen = current_screen()) {
        static char last[64];
        if (strncmp(screen, last, sizeof(last)) != 0) {
            strncpy(last, screen, sizeof(last) - 1);
            g_log.line("screen: %s (frame %u, %lu ms)", screen, g_frame, since_start_ms());
        }
    }
    for (;;) {
        if (g_presses_left > 0 || g_button_down) return;  // a press is being made: the next command waits for it
        if (g_wait_until_frame) {
            if (g_frame < g_wait_until_frame) return;
            g_wait_until_frame = 0;
        }
        if (g_wait_until_ms) {
            if (static_cast<LONG>(GetTickCount() - g_wait_until_ms) < 0) return;
            g_wait_until_ms = 0;
        }
        if (!g_condition.empty()) {
            if (condition_met(g_condition)) {
                g_log.line("automation: %s reached (frame %u, %lu ms)", g_condition.c_str(), g_frame, since_start_ms());
                g_condition.clear();
            } else if (static_cast<LONG>(GetTickCount() - g_condition_deadline) >= 0) {
                log_state();
                return quit(2, "timed out waiting");
            } else {
                return;
            }
        }
        if (g_next >= g_lines.size()) {
            const DWORD now = GetTickCount();
            if (now - g_last_size_check >= 500) {
                g_last_size_check = now;
                read_script();
            }
            if (g_next >= g_lines.size()) return;
        }
        run(g_lines[g_next++], device);
        if (g_quitting) return;
        if (g_quitting) return;
    }
}

HRESULT WINAPI hook_present(IDirect3DDevice9 *device, const RECT *source, const RECT *dest, HWND window, const RGNDATA *dirty) {
    g_game_thread = GetCurrentThreadId();
    g_last_present_ms = GetTickCount();
    tick(device);
    return g_orig_present(device, source, dest, window, dirty);
}

// A game that stops drawing (a hang, a loading loop) is told by where its thread is: after 4 s without a frame, and every 5 s
// after that (up to 6 times), the thread's instruction pointer and the code addresses on its stack go to the log.
// Is the memory at `p` readable? (IsBadReadPtr works by faulting, which the crash log would report.)
bool readable(const void *p) {
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(p, &info, sizeof(info)) || info.State != MEM_COMMIT) return false;
    return (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) == 0;
}

DWORD WINAPI stall_watch(void *) {
    int reports = 0;
    for (;;) {
        Sleep(1000);
        const DWORD now = GetTickCount();
        if (g_exiting) return 0;
        if (now - g_last_present_ms < 4000) {
            reports = 0;
            continue;
        }
        if (reports >= 6 || now - g_last_present_ms < 4000 + 5000u * reports) continue;
        ++reports;
        // Every thread of the process but this one: where each is now.
        const DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 entry = {sizeof(entry)};
        for (BOOL more = snapshot != INVALID_HANDLE_VALUE && Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
            if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == me) continue;
            HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, entry.th32ThreadID);
            if (!thread) continue;
            CONTEXT context = {};
            context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
            char line[600];
            int n = 0;
            if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
                if (GetThreadContext(thread, &context)) {
                    n = snprintf(line, sizeof(line), "no frame for %lu ms: thread %lu%s eip %#lx, code addresses on the stack:", now - g_last_present_ms,
                                 entry.th32ThreadID, entry.th32ThreadID == g_game_thread ? " (the game's)" : "", context.Eip);
                    const unsigned long *stack = reinterpret_cast<const unsigned long *>(context.Esp);
                    for (int i = 0; i < 256 && n < static_cast<int>(sizeof(line)) - 12; ++i) {
                        if (!readable(&stack[i])) break;
                        if (stack[i] >= 0x401000 && stack[i] < 0x900000) n += snprintf(line + n, sizeof(line) - n, " %#lx", stack[i]);
                    }
                }
                ResumeThread(thread);
            }
            CloseHandle(thread);
            if (n) g_log.line("automation: %s", line);
        }
        if (snapshot != INVALID_HANDLE_VALUE) CloseHandle(snapshot);
    }
}

// The game makes a new device when it applies video settings (a profile load does), so the hook is checked every 200 ms and put
// back into whatever vtable the current device has.
DWORD WINAPI keep_hooked(void *) {
    bool watching = false;
    for (;; Sleep(200)) {
        auto *device = *reinterpret_cast<IDirect3DDevice9 **>(kDevice);
        if (!device) continue;
        void **vtable = *reinterpret_cast<void ***>(device);
        if (vtable[kPresentSlot] == reinterpret_cast<void *>(&hook_present)) continue;
        DWORD old = 0;
        if (!VirtualProtect(&vtable[kPresentSlot], sizeof(void *), PAGE_READWRITE, &old)) continue;
        g_orig_present = reinterpret_cast<PresentFn>(vtable[kPresentSlot]);
        vtable[kPresentSlot] = reinterpret_cast<void *>(&hook_present);
        VirtualProtect(&vtable[kPresentSlot], sizeof(void *), old, &old);
        g_log.line("automation: Present hooked (device %p, vtable %p, %lu ms after the process started)", device, vtable, since_start_ms());
        if (!watching) {
            watching = true;
            g_game_thread = 0;
            g_last_present_ms = GetTickCount();
            CreateThread(nullptr, 0, stall_watch, nullptr, 0, nullptr);
        }
    }
}

}  // namespace

void install() {
    char name[128] = "";
    if (!GetEnvironmentVariableA("NFSMW_SCENARIO", name, sizeof(name)))
        GetPrivateProfileStringA("automation", "scenario", "", name, sizeof(name), g_ini);
    if (!name[0]) return;

    char path[MAX_PATH];
    nfsmw::data_file("scenarios\\", path, sizeof(path));
    CreateDirectoryA(path, nullptr);
    g_script_path = std::string(path) + name + ".txt";
    nfsmw::data_file("screenshots\\", path, sizeof(path));
    CreateDirectoryA(path, nullptr);
    SYSTEMTIME t;
    GetLocalTime(&t);
    char run[96];
    snprintf(run, sizeof(run), "%04d%02d%02d-%02d%02d%02d_%s\\", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, name);
    g_screenshot_dir = std::string(path) + run;  // one folder per run, so pictures of older runs stay
    CreateDirectoryA(g_screenshot_dir.c_str(), nullptr);

    read_script();
    if (g_lines.empty()) g_log.line("automation: %s is missing or empty; waiting for it to be written", g_script_path.c_str());
    CreateThread(nullptr, 0, keep_hooked, nullptr, 0, nullptr);
}

}  // namespace dev_tools::automation
