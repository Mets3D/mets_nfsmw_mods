/*
 * The boot flow's first screen (MC_Bootup.fng, UIMemcardBoot) starts MemoryCard::BootupCheck on its FEHASH_INITCOMPLETE and
 * only moves on when the check reports back (MemcardCallbacks::BootupCheckDone 0x559AC0 queues 0x461A18EE). The check can
 * stop to show a message and wait for an answer (MemcardCallbacks::ShowMessage 0x560980 sets MemoryCard+0x14). So a black
 * screen at start-up is one of: the check waiting on a message, the check never finishing, or the game loop not running.
 * This polls that state from a thread of its own and logs each change, and hooks ShowMessage for the message's text.
 *
 * PC layouts: BootFlowManager (*0x91CF48) current screen +0xC, BootFlowScreen name +0xC; MemoryCard (*0x91CB20) waiting
 * for an answer +0x14 (byte), current operation +0x40 (1 = boot-up check, 0 = none), last error +0x48 (u16).
 */
#include "boot_trace.h"

#include "dev_tools.h"
#include "hooks.h"

namespace dev_tools::boot_trace {

namespace {

constexpr uintptr_t kBootFlowManager = 0x91CF48, kMemoryCard = 0x91CB20;
constexpr uintptr_t kFrameCounter = 0x982B78, kRealTimeFrames = 0x8F82F4;  // eFrameCounter, RealTimeFrames

void(__fastcall *g_orig_show_message)(void *, void *, const wchar_t *, unsigned, const wchar_t **);

void narrow(const wchar_t *w, char *out, int size) {
    out[0] = 0;
    if (w) WideCharToMultiByte(CP_UTF8, 0, w, -1, out, size, nullptr, nullptr);
}

void __fastcall hook_show_message(void *self, void *edx, const wchar_t *msg, unsigned count, const wchar_t **options) {
    char text[512], option[64];
    narrow(msg, text, sizeof(text));
    g_log.line("boot: %lu ms: memory card message \"%s\" (%u options)", since_start_ms(), text, count);
    for (unsigned i = 0; options && i < count && i < 4; ++i) {
        narrow(options[i], option, sizeof(option));
        g_log.line("boot:   option %u \"%s\"", i, option);
    }
    g_orig_show_message(self, edx, msg, count, options);
}

// Runs on our own thread while the game may delete the manager (at MainMenu.fng), so every step is checked first.
void screen_name(char *out, size_t size) {
    snprintf(out, size, "(none)");
    char *manager = *reinterpret_cast<char **>(kBootFlowManager);
    if (!manager || IsBadReadPtr(manager + 0xC, 4)) return;
    char *screen = *reinterpret_cast<char **>(manager + 0xC);
    if (!screen || IsBadReadPtr(screen + 0xC, 4)) return;
    const char *name = *reinterpret_cast<const char **>(screen + 0xC);
    if (name && !IsBadStringPtrA(name, size)) snprintf(out, size, "%s", name);
}

DWORD WINAPI poll(void *) {
    char last_screen[64] = "?", screen[64];
    int last_op = -2, last_wait = -2, last_error = -2;
    unsigned last_frames = *reinterpret_cast<unsigned *>(kFrameCounter);
    unsigned last_real = *reinterpret_cast<unsigned *>(kRealTimeFrames);
    unsigned long last_beat = since_start_ms(), done_at = 0;
    bool seen_manager = false;
    for (;;) {
        const unsigned long now = since_start_ms();
        screen_name(screen, sizeof(screen));
        if (*reinterpret_cast<void **>(kBootFlowManager)) seen_manager = true;
        else if (seen_manager && !done_at) done_at = now;
        char *card = *reinterpret_cast<char **>(kMemoryCard);
        const int op = card ? *reinterpret_cast<int *>(card + 0x40) : -1;
        const int wait = card ? *reinterpret_cast<unsigned char *>(card + 0x14) : -1;
        const int error = card ? *reinterpret_cast<unsigned short *>(card + 0x48) : -1;
        if (strcmp(screen, last_screen) || op != last_op || wait != last_wait || error != last_error) {
            g_log.line("boot: %lu ms: screen %s, memory card operation %d, waiting for an answer %d, last error %d", now, screen,
                       op, wait, error);
            snprintf(last_screen, sizeof(last_screen), "%s", screen);
            last_op = op;
            last_wait = wait;
            last_error = error;
        }
        if (now - last_beat >= 5000) {
            const unsigned frames = *reinterpret_cast<unsigned *>(kFrameCounter);
            const unsigned real = *reinterpret_cast<unsigned *>(kRealTimeFrames);
            g_log.line("boot: %lu ms: eFrameCounter +%u, RealTimeFrames +%u in %lu ms", now, frames - last_frames, real - last_real,
                       now - last_beat);
            last_frames = frames;
            last_real = real;
            last_beat = now;
        }
        if ((done_at && now - done_at > 10000) || now > 180000) break;
        Sleep(100);
    }
    g_log.line("boot: trace ends (%s)", done_at ? "boot flow finished" : "3 minutes");
    return 0;
}

}  // namespace

void install() {
    const nfsmw::Hook hooks[] = {
        {"memcardmessage", "MemcardCallbacks::ShowMessage", 0x560980, {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A}, false,
         nfsmw::detour(&hook_show_message), nfsmw::orig(&g_orig_show_message)},
    };
    nfsmw::install_hooks(hooks, 1, g_log, g_ini, "boot trace");
    CreateThread(nullptr, 0, poll, nullptr, 0, nullptr);
}

}  // namespace dev_tools::boot_trace
