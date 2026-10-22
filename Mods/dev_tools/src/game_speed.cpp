/*
 * The game's simulation runs at a speed (SimSystem + 0x24, what Sim::GetSpeed 0x6E8DC0 returns; the game's own slow motion, the speedbreaker, sets it every frame through
 * ITimeManager::OnManageTime). SimSystem::UpdateFrame 0x6F6CF0 makes the step of the frame from it: `mTimeStep = mScheduleStep * mSpeed` (the fld / fmul at 0x6F6D94,
 * 0x6F6D9D, the result stored at +0x1C by the fst at 0x6F6DA6), and everything that moves in the simulation takes its time from that step. So the step is multiplied by one
 * more number of ours: the six bytes at 0x6F6DA0 (a `mov edi, [0x982B78]` that does not touch the x87 stack) become a jump to a stub that does `fmul [g_scale]`, the mov
 * and a jump back. Nothing else reads the unscaled speed to the effect of the step, so the game runs slower or faster as a whole; but a step that is much longer than the
 * game's own (above about 2) makes the physics coarse.
 * The keys are polled by a hook on UpdateFrame itself (once per frame): each press halves or doubles the scale (1/16 to 8).
 */
#include "game_speed.h"

#include <algorithm>
#include <cmath>

#include "dev_tools.h"
#include "hooks.h"
#include "keys.h"
#include "patch.h"

namespace dev_tools::game_speed {

namespace {

constexpr uintptr_t kUpdateFrame = 0x6F6CF0, kPatch = 0x6F6DA0, kPatchBack = 0x6F6DA6;
constexpr float kSlowest = 1.0f / 16.0f, kFastest = 8.0f;

float g_scale = 1.0f;  // read by the stub
int g_slower = 0, g_faster = 0;
bool g_slower_before = false, g_faster_before = false;

using UpdateFrameFn = void(__fastcall *)(void *, void *);
UpdateFrameFn g_orig_update_frame = nullptr;

void change(float factor) {
    const float scale = std::clamp(g_scale * factor, kSlowest, kFastest);
    if (scale == g_scale) return;
    g_scale = scale;
    g_log.line("game speed: x%g", scale);
}

void __fastcall hook_update_frame(void *self, void *edx) {
    if (nfsmw::key_clicked(g_slower, g_slower_before)) change(0.5f);
    if (nfsmw::key_clicked(g_faster, g_faster_before)) change(2.0f);
    g_orig_update_frame(self, edx);
}

}  // namespace

void install() {
    char key[24];
    GetPrivateProfileStringA("game_speed", "slower_key", "Minus", key, sizeof(key), g_ini);
    g_slower = nfsmw::key_from_name(key);
    GetPrivateProfileStringA("game_speed", "faster_key", "Plus", key, sizeof(key), g_ini);
    g_faster = nfsmw::key_from_name(key);

    nfsmw::Stub stub(32);
    stub.emit({0xD8, 0x0D});
    stub.emit_u32(reinterpret_cast<uintptr_t>(&g_scale));  // fmul dword ptr [g_scale]
    stub.emit({0x8B, 0x3D});
    stub.emit_u32(0x982B78);  // mov edi, [0x982B78]: what the six bytes did
    stub.emit_rel32(0xE9, kPatchBack);
    stub.done();
    const unsigned char expected[6] = {0x8B, 0x3D, 0x78, 0x2B, 0x98, 0x00};
    if (!nfsmw::jump_to_stub(kPatch, expected, 6, stub)) {
        g_log.line("game speed: NOT patched (the code differs)");
        return;
    }
    const nfsmw::Hook hooks[] = {
        {"update_frame", "SimSystem::UpdateFrame", kUpdateFrame, {0x51, 0x56, 0x8B, 0xF1, 0x57, 0xB9, 0x90}, true, nfsmw::detour(&hook_update_frame), nfsmw::orig(&g_orig_update_frame)},
    };
    if (nfsmw::install_hooks(hooks, 1, g_log, g_ini, "game speed").ok) g_log.line("game speed: keys %d (slower) and %d (faster), the speed starts at x1", g_slower, g_faster);
}

}  // namespace dev_tools::game_speed
