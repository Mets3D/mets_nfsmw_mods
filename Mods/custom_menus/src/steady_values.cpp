// FEToggleWidget::SetScript 0x574550 plays a script on the four objects of a row (label, value, arrows): UnsetFocus and Enable play UNHIGHLIGHT, SetFocus HIGHLIGHT. The value of an
// Options row has no UNHIGHLIGHT, but that of Track_Options.fng (the race options) has one that darkens it and no HIGHLIGHT to undo it: a value turned dark when the cursor left its
// row and stayed dark, while the rows not yet visited kept the layout's white. INIT is that white, so it is played on the value right after UNHIGHLIGHT.
#include "steady_values.h"

#include "hooks.h"

namespace custom_menus::steady_values {

namespace {

constexpr uintptr_t kToggleSetScript = 0x574550, kFindScript = 0x5B1360, kSetScript = 0x514D10;  // FEToggleWidget::SetScript(hash), FEObject::FindScript, FEngSetScript(object, hash, restart)
constexpr unsigned kUnhighlight = 0x7AB5521A, kInit = 0x001744B3;                                 // FEHashUpper of the names
constexpr unsigned kEnabled = 0x28, kValue = 0x30;                                                // FEToggleWidget: enabled, the value's FEString

void(__fastcall *g_orig_set_script)(void *, void *, unsigned);

template <typename T>
T &field(void *object, unsigned offset) { return *reinterpret_cast<T *>(static_cast<char *>(object) + offset); }

bool has_script(void *object, unsigned id) { return reinterpret_cast<void *(__fastcall *)(void *, void *, unsigned)>(kFindScript)(object, nullptr, id) != nullptr; }

void __fastcall hook_set_script(void *widget, void *edx, unsigned script) {
    g_orig_set_script(widget, edx, script);
    if (script != kUnhighlight || !field<unsigned char>(widget, kEnabled)) return;  // (a greyed out row stays grey)
    void *value = field<void *>(widget, kValue);
    if (value && has_script(value, kUnhighlight) && has_script(value, kInit)) reinterpret_cast<void(__cdecl *)(void *, unsigned, bool)>(kSetScript)(value, kInit, true);
}

}  // namespace

void install_hooks(nfsmw::Logger &log, const char *ini) {
    if (!GetPrivateProfileIntA("features", "steady_option_values", 1, ini)) return;
    const nfsmw::Hook hooks[] = {
        {"steadyvalues", "FEToggleWidget::SetScript", kToggleSetScript, {0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C, 0x8B}, false, nfsmw::detour(&hook_set_script),
         nfsmw::orig(&g_orig_set_script)},
    };
    nfsmw::install_hooks(hooks, 1, log, ini, "steady values");
}

}  // namespace custom_menus::steady_values
