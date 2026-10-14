// Installing a table of MinHook hooks, the way every mod here does it: each hook names the game function it detours,
// with the first 7 bytes the function is expected to start with. A function that starts with a jump (E9: another mod's hook)
// is hooked on top of it, MinHook follows the jump; any other difference means this is not the exe the offsets are for
// (or a patch that cannot be hooked around), and the hook is not installed. Header only.
#pragma once

#include <MinHook.h>

#include "nfsmw_util.h"

namespace nfsmw {

struct Hook {
    const char *name;      // short name; `[hooks] <name>=0` in the mod's ini skips the hook
    const char *function;  // the game function, for the log
    uintptr_t addr;
    unsigned char prologue[7];  // the function's first bytes in speed.exe 1.3
    bool required;  // if a required hook cannot be installed, none of the table's hooks are
    void *detour;
    void **orig;
};

template <typename Fn>
void *detour(Fn fn) { return reinterpret_cast<void *>(fn); }
template <typename Fn>
void **orig(Fn *fn) { return reinterpret_cast<void **>(fn); }

struct HookResult {
    int wanted = 0, created = 0;
    bool ok = true;  // false when a required hook was missing or changed: nothing was installed
};

// Installs the table and enables it (and every other hook created so far). `ini` (a path, or null) is the mod's ini for
// the `[hooks]` skip list; `label` names the group in the log. Safe to call more than once, e.g. for hooks that depend on a condition found at start-up.
inline HookResult install_hooks(const Hook *hooks, size_t count, Logger &log, const char *ini = nullptr,
                                const char *label = "") {
    HookResult result;
    const auto intact = [](const Hook &h) {
        const unsigned char *code = reinterpret_cast<const unsigned char *>(h.addr);
        return code[0] == 0xE9 || memcmp(code, h.prologue, sizeof(h.prologue)) == 0;
    };
    for (size_t i = 0; i < count; ++i) {
        if (hooks[i].required && !intact(hooks[i])) {
            log.line("%s (%#x) was changed by another mod; not hooking anything", hooks[i].function,
                     static_cast<unsigned>(hooks[i].addr));
            result.ok = false;
        }
    }
    const MH_STATUS init = MH_Initialize();
    if (!result.ok || (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)) {
        result.ok = false;
        return result;
    }
    size_t made[32];
    size_t made_count = 0;
    for (size_t i = 0; i < count && made_count < 32; ++i) {
        const Hook &h = hooks[i];
        if (ini && !GetPrivateProfileIntA("hooks", h.name, 1, ini)) {
            log.line("hook %s disabled in the ini", h.name);
            continue;
        }
        if (!h.required && !intact(h)) {
            log.line("%s (%#x) was changed by another mod; not hooking it", h.function, static_cast<unsigned>(h.addr));
            continue;
        }
        ++result.wanted;
        const MH_STATUS s = MH_CreateHook(reinterpret_cast<void *>(h.addr), h.detour, h.orig);
        if (s == MH_OK) {
            ++result.created;
            made[made_count++] = i;
        } else {
            log.line("MH_CreateHook(%s) failed: %s", h.name, MH_StatusToString(s));
            if (h.required) {  // the parts depend on each other: back out
                for (size_t k = 0; k < made_count; ++k) MH_RemoveHook(reinterpret_cast<void *>(hooks[made[k]].addr));
                result.created = 0;
                result.ok = false;
                return result;
            }
        }
    }
    const MH_STATUS enabled = MH_EnableHook(MH_ALL_HOOKS);
    log.line("hooks installed%s%s: %d of %d%s", *label ? " - " : "", label, result.created, result.wanted,
             enabled == MH_OK ? "" : " (MH_EnableHook failed)");
    return result;
}

}  // namespace nfsmw
