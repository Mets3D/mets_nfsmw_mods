#include "text_guard.h"

#include "hooks.h"

namespace custom_menus::text_guard {

namespace {

nfsmw::Logger *g_log = nullptr;
unsigned g_width_nulls = 0, g_render_nulls = 0;

// FEngFont::GetTextWidth(const wchar_t *text, unsigned flags), returns the width in st0.
using GetTextWidthFn = float(FASTCALL *)(void *, void *, const wchar_t *, unsigned);
GetTextWidthFn g_orig_width;

float FASTCALL hook_width(void *font, void *edx, const wchar_t *text, unsigned flags) {
    if (!text) {
        if (g_width_nulls++ < 2 && g_log) g_log->line("text guard: GetTextWidth was given no text (called from %p); measured as empty", __builtin_return_address(0));
        text = L"";
    }
    return g_orig_width(font, edx, text, flags);
}

// cFEngRender::RenderString(FEString *string, FERenderObject *, FEPackageRenderInfo *): draws a text object. Its text is at +0x64.
using RenderStringFn = void(FASTCALL *)(void *, void *, void *, void *, void *);
RenderStringFn g_orig_render;

void FASTCALL hook_render(void *self, void *edx, void *string, void *render_object, void *info) {
    if (string && !nfsmw::field<void *>(string, 0x64)) {
        if (g_render_nulls++ < 2 && g_log) g_log->line("text guard: the text object %p has no text and is not drawn", string);
        return;
    }
    g_orig_render(self, edx, string, render_object, info);
}

}  // namespace

void install_hooks(nfsmw::Logger &log, const char *ini) {
    g_log = &log;
    const nfsmw::Hook hooks[] = {
        {"textguard", "FEngFont::GetTextWidth", 0x586210, {0x56, 0x8B, 0x74, 0x24, 0x08, 0x57, 0x6A}, false,
         nfsmw::detour(&hook_width), nfsmw::orig(&g_orig_width)},
        {"textguard_render", "cFEngRender::RenderString", 0x5A4390, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x81}, false,
         nfsmw::detour(&hook_render), nfsmw::orig(&g_orig_render)},
    };
    nfsmw::install_hooks(hooks, sizeof(hooks) / sizeof(hooks[0]), log, ini, "text guard");
}

}  // namespace custom_menus::text_guard
