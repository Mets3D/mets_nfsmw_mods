/*
 * ExitProcess and TerminateProcess (of this process) are logged with the places in the game's code the call came from, and
 * the first three access violations with their registers. The places are values on the stack that point right after a
 * call instruction in speed.exe's code or in one of our .asi files; the first access violation, the exit code 0xC0000005 and the ExitProcess that
 * followed are how the Direct3D failure of a second full screen copy was found. A thread also watches the TLS directories of our
 * .asi files (see watch_tls).
 */
#include "diagnostics.h"

#include <string>

#include <MinHook.h>

#include "dev_tools.h"

namespace dev_tools::diagnostics {

namespace {

constexpr unsigned long kCodeStart = 0x401008, kCodeEnd = 0x88F2A5;  // speed.exe's code

// Copies memory that may not be mapped (false when it isn't).
bool read_memory(const void *from, void *to, size_t size) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), from, to, size, &got) && got == size;
}

// "module+offset" of an address inside a loaded module (the game's, ours, a system DLL), or "".
std::string where(unsigned long address) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(address), &module))
        return "";
    char name[MAX_PATH] = "?";
    GetModuleFileNameA(module, name, sizeof(name));
    const char *slash = strrchr(name, '\\');
    char text[MAX_PATH + 24];
    snprintf(text, sizeof(text), "%s+%#lx", slash ? slash + 1 : name, address - reinterpret_cast<unsigned long>(module));
    return text;
}

// Up to 14 values from the stack that look like return addresses (a call right before them), into the game or into one of the mods'
// .asi files, which the game's own frames hide between them.
void log_game_stack(const unsigned long *sp) {
    int shown = 0;
    const unsigned long *top = static_cast<const unsigned long *>(reinterpret_cast<NT_TIB *>(NtCurrentTeb())->StackBase);
    for (int i = 0; sp + i < top && i < 6000 && shown < 14; ++i) {  // d3d9 frames can be big
        const unsigned long v = sp[i];
        std::string module;
        if (v < kCodeStart || v > kCodeEnd) {
            if (v < 0x10000 || v >= 0x80000000) continue;
            module = where(v);
            if (module.compare(0, 5, "nfsmw") != 0) continue;
        }
        unsigned char before[6];
        if (!read_memory(reinterpret_cast<const void *>(v - 6), before, sizeof(before))) continue;
        if (before[1] == 0xE8 || (before[0] == 0xFF && before[1] == 0x15) || (before[4] == 0xFF && (before[5] & 0xF8) == 0xD0) ||
            (before[3] == 0xFF && (before[4] & 0x38) == 0x10)) {  // call rel32, call [abs], call reg, call [reg+disp8]
            g_log.line("    stack: %#lx %s", v, module.c_str());
            ++shown;
        }
    }
}

void log_caller_trace(const char *what, unsigned code) {
    g_log.line("%s(%u) called from %p", what, code, __builtin_return_address(1));
    log_game_stack(static_cast<const unsigned long *>(__builtin_frame_address(0)));
}


// ---- the TLS directories of our .asi files ----
// Every time a thread starts or ends, Wine's loader reads each module's TLS directory (PE data directory 9) and runs the callbacks it
// lists. Several crashes in a row were that code (ntdll, call_tls_callbacks) reading a callbacks pointer that is text ("sect", "prot")
// from the directory of one of our .asi files: something overwrote it. This thread checks the directories every 50 ms, and logs the
// memory around the first one that stops pointing into its own module, which says when it happens and what the bytes look like.
const char *const kWatched[] = {"nfsmw_custom_menus.asi", "nfsmw_camera_settings.asi", "mets_controller_support.asi",
                                "nfsmw_split_screen_support.asi", "nfsmw_dev_tools.asi"};
struct Watched {
    const char *name;
    unsigned char *base;
    unsigned long image_size;
    unsigned char *directory;  // the IMAGE_TLS_DIRECTORY32
    unsigned long callbacks;   // its AddressOfCallBacks when we first looked
    bool reported;
};
Watched g_watched[sizeof(kWatched) / sizeof(kWatched[0])];
int g_watched_count = 0;

void dump_memory(const char *label, const unsigned char *at, size_t size) {
    unsigned char bytes[0x80];
    if (size > sizeof(bytes) || !read_memory(at, bytes, size)) {
        g_log.line("    %s %p: not readable", label, at);
        return;
    }
    for (size_t row = 0; row < size; row += 16) {
        char hex[64] = {}, text[17] = {};
        for (size_t i = 0; i < 16 && row + i < size; ++i) {
            snprintf(hex + i * 3, 4, "%02x ", bytes[row + i]);
            text[i] = bytes[row + i] >= 0x20 && bytes[row + i] < 0x7F ? static_cast<char>(bytes[row + i]) : '.';
        }
        g_log.line("    %s %p: %-48s %s", label, at + row, hex, text);
    }
}

// The TLS directory of a module as it is in memory now (false if its header or directory can't be read).
bool read_tls_directory(Watched &w, unsigned long *callbacks) {
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS32 nt;
    if (!read_memory(w.base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) return false;
    if (!read_memory(w.base + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE) return false;
    const IMAGE_DATA_DIRECTORY &dir = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    w.image_size = nt.OptionalHeader.SizeOfImage;
    w.directory = w.base + dir.VirtualAddress;
    IMAGE_TLS_DIRECTORY32 tls;
    if (!dir.VirtualAddress || !read_memory(w.directory, &tls, sizeof(tls))) return false;
    *callbacks = tls.AddressOfCallBacks;
    return true;
}

void check_tls_directories(const char *when) {
    for (int i = 0; i < g_watched_count; ++i) {
        Watched &w = g_watched[i];
        unsigned long callbacks = 0;
        const bool read = read_tls_directory(w, &callbacks);
        const bool inside = read && callbacks >= reinterpret_cast<unsigned long>(w.base) &&
                            callbacks < reinterpret_cast<unsigned long>(w.base) + w.image_size;
        if (inside || w.reported) continue;
        w.reported = true;
        g_log.line("TLS DIRECTORY OF %s IS WRONG (%s, %lu ms after start): base %p, directory %p, callbacks %#lx (was %#lx, %s)", w.name, when,
                   since_start_ms(), w.base, w.directory, callbacks, w.callbacks, read ? "readable" : "not readable");
        MEMORY_BASIC_INFORMATION info = {};
        if (VirtualQuery(w.directory, &info, sizeof(info)))
            g_log.line("    region %p+%#lx allocated from %p, state %#lx protect %#lx type %#lx", info.BaseAddress,
                       static_cast<unsigned long>(info.RegionSize), info.AllocationBase, info.State, info.Protect, info.Type);
        dump_memory("before", w.directory - 0x40, 0x40);
        dump_memory("directory", w.directory, 0x40);
        dump_memory("header", w.base, 0x40);
    }
}

DWORD WINAPI watch_tls(LPVOID) {
    for (const char *name : kWatched) {
        const HMODULE module = GetModuleHandleA(name);
        if (!module) continue;
        Watched &w = g_watched[g_watched_count];
        w = {name, reinterpret_cast<unsigned char *>(module), 0, nullptr, 0, false};
        unsigned long callbacks = 0;
        if (!read_tls_directory(w, &callbacks)) continue;
        w.callbacks = callbacks;
        ++g_watched_count;
        g_log.line("tls watch: %s at %p, directory %p, callbacks %#lx", name, w.base, w.directory, callbacks);
    }
    for (;; Sleep(50)) check_tls_directories("watch");
}

LONG CALLBACK exception_logger(EXCEPTION_POINTERS *e) {
    static int count = 0;
    const DWORD code = e->ExceptionRecord->ExceptionCode;
    // The crashes that end the game: not the C++ exceptions and debugger messages the game uses on purpose.
    const bool fatal = code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_BREAKPOINT || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
                       code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_PRIV_INSTRUCTION ||
                       code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED;
    if (!fatal || count++ >= 3) return EXCEPTION_CONTINUE_SEARCH;
    const CONTEXT *c = e->ContextRecord;
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(c->Eip), &module))
        GetModuleFileNameA(module, name, sizeof(name));
    const char *slash = strrchr(name, '\\');
    if (code == EXCEPTION_ACCESS_VIOLATION)
        g_log.line("ACCESS VIOLATION at eip=%#lx (%s+%#lx) %s address %#lx", c->Eip, slash ? slash + 1 : name,
                   c->Eip - reinterpret_cast<unsigned long>(module),
                   e->ExceptionRecord->ExceptionInformation[0] == 0 ? "reading" : "writing",
                   static_cast<unsigned long>(e->ExceptionRecord->ExceptionInformation[1]));
    else
        g_log.line("EXCEPTION %#lx at eip=%#lx (%s+%#lx)", code, c->Eip, slash ? slash + 1 : name, c->Eip - reinterpret_cast<unsigned long>(module));
    g_log.line("    eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx", c->Eax, c->Ebx, c->Ecx,
               c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    const struct { const char *name; unsigned long value; } regs[] = {{"eax", c->Eax}, {"ebx", c->Ebx}, {"ecx", c->Ecx}, {"edx", c->Edx},
                                                                    {"esi", c->Esi}, {"edi", c->Edi}};
    for (const auto &r : regs) {
        const std::string place = where(r.value);
        if (!place.empty()) g_log.line("    %s points into %s", r.name, place.c_str());
    }
    check_tls_directories("at the access violation");
    log_game_stack(reinterpret_cast<const unsigned long *>(c->Esp));
    return EXCEPTION_CONTINUE_SEARCH;
}

using ExitProcessFn = void(WINAPI *)(UINT);
ExitProcessFn g_orig_exit_process;
void WINAPI hook_exit_process(UINT code) {
    g_exiting = true;
    log_caller_trace("ExitProcess", code);
    g_orig_exit_process(code);
}

using TerminateProcessFn = BOOL(WINAPI *)(HANDLE, UINT);
TerminateProcessFn g_orig_terminate_process;
BOOL WINAPI hook_terminate_process(HANDLE process, UINT code) {
    if (process == GetCurrentProcess() || GetProcessId(process) == GetCurrentProcessId())
        log_caller_trace("TerminateProcess", code);
    return g_orig_terminate_process(process, code);
}

}  // namespace

void install() {
    AddVectoredExceptionHandler(1, exception_logger);
    if (HANDLE watcher = CreateThread(nullptr, 0, watch_tls, nullptr, 0, nullptr)) CloseHandle(watcher);
    if (MH_CreateHookApi(L"kernel32", "ExitProcess", reinterpret_cast<void *>(&hook_exit_process),
                         reinterpret_cast<void **>(&g_orig_exit_process)) == MH_OK &&
        MH_CreateHookApi(L"kernel32", "TerminateProcess", reinterpret_cast<void *>(&hook_terminate_process),
                         reinterpret_cast<void **>(&g_orig_terminate_process)) == MH_OK)
        MH_EnableHook(MH_ALL_HOOKS);
    else
        g_log.line("could not hook ExitProcess / TerminateProcess");
}

}  // namespace dev_tools::diagnostics
