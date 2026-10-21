#include "view_api.h"

#include "split_screen_api.h"

namespace split_screen_support::view_api {

namespace {

nfsmw::split_screen_api::ViewDrawFn g_callbacks[8];
volatile int g_count = 0;

}  // namespace

void draw(IDirect3DDevice9 *device, const void *view_plat_info, float width, float height, int player) {
    const int count = g_count;
    for (int i = 0; i < count; ++i) g_callbacks[i](device, view_plat_info, width, height, player);
}

bool wanted() { return g_count > 0; }

}  // namespace split_screen_support::view_api

extern "C" __declspec(dllexport) void nfsmw_split_screen_support_on_view_draw(nfsmw::split_screen_api::ViewDrawFn callback) {
    using namespace split_screen_support::view_api;
    if (callback && g_count < 8) {
        g_callbacks[g_count] = callback;
        g_count = g_count + 1;
    }
}
