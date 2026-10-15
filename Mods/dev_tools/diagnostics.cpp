/*
 * ExitProcess and TerminateProcess (of this process) are logged with the places in the game's code the call came from, and
 * the first three access violations with their registers. The places are values on the stack that point right after a
 * call instruction in speed.exe's code; the first access violation, the exit code 0xC0000005 and the ExitProcess that
 * followed are how the Direct3D failure of a second full screen copy was found.
 */
#include "diagnostics.h"

#include <MinHook.h>

#include "dev_tools.h"

namespace dev_tools::diagnostics {

namespace {

constexpr unsigned long kCodeStart = 0x401008, kCodeEnd = 0x88F2A5;  // speed.exe's code

// Up to 14 values from the stack that look like return addresses into the game.
void log_game_stack(const unsigned long *sp) {
    int shown = 0;
    const unsigned long *top = static_cast<const unsigned long *>(reinterpret_cast<NT_TIB *>(NtCurrentTeb())->StackBase);
    for (int i = 0; sp + i < top && i < 6000 && shown < 14; ++i) {  // d3d9 frames can be big
        const unsigned long v = sp[i];
        if (v < kCodeStart || v > kCodeEnd) continue;
        const unsigned char *at = reinterpret_cast<const unsigned char *>(v);
        if (at[-5] == 0xE8 || (at[-6] == 0xFF && at[-5] == 0x15) || (at[-2] == 0xFF && (at[-1] & 0xF8) == 0xD0)) {
            g_log.line("    stack: %#lx", v);
            ++shown;
        }
    }
}

void log_caller_trace(const char *what, unsigned code) {
    g_log.line("%s(%u) called from %p", what, code, __builtin_return_address(1));
    log_game_stack(static_cast<const unsigned long *>(__builtin_frame_address(0)));
}

LONG CALLBACK exception_logger(EXCEPTION_POINTERS *e) {
    static int count = 0;
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || count++ >= 3) return EXCEPTION_CONTINUE_SEARCH;
    const CONTEXT *c = e->ContextRecord;
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(c->Eip), &module))
        GetModuleFileNameA(module, name, sizeof(name));
    const char *slash = strrchr(name, '\\');
    g_log.line("ACCESS VIOLATION at eip=%#lx (%s+%#lx) %s address %#lx", c->Eip, slash ? slash + 1 : name,
               c->Eip - reinterpret_cast<unsigned long>(module),
               e->ExceptionRecord->ExceptionInformation[0] == 0 ? "reading" : "writing",
               static_cast<unsigned long>(e->ExceptionRecord->ExceptionInformation[1]));
    g_log.line("    eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx", c->Eax, c->Ebx, c->Ecx,
               c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    log_game_stack(reinterpret_cast<const unsigned long *>(c->Esp));
    return EXCEPTION_CONTINUE_SEARCH;
}

using ExitProcessFn = void(WINAPI *)(UINT);
ExitProcessFn g_orig_exit_process;
void WINAPI hook_exit_process(UINT code) {
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
    if (MH_CreateHookApi(L"kernel32", "ExitProcess", reinterpret_cast<void *>(&hook_exit_process),
                         reinterpret_cast<void **>(&g_orig_exit_process)) == MH_OK &&
        MH_CreateHookApi(L"kernel32", "TerminateProcess", reinterpret_cast<void *>(&hook_terminate_process),
                         reinterpret_cast<void **>(&g_orig_terminate_process)) == MH_OK)
        MH_EnableHook(MH_ALL_HOOKS);
    else
        g_log.line("could not hook ExitProcess / TerminateProcess");
}

}  // namespace dev_tools::diagnostics
