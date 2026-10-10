// Loading a texture pack (.tpk) of our own through the game's resource loader, like the packs other mods add: the
// loader registers the pack's textures by hash, and from then on the game's menus and HUD can show them. Header only.
#pragma once

#include "nfsmw_util.h"

namespace nfsmw {

// Loads `path` (a .tpk) and waits, up to 3 seconds, until the game finished loading it. Returns whether it did. The
// game names resource files relative to its own folder, so a path inside it is cut to that. Call it on the game's
// thread, once the game is running.
inline bool load_texture_pack(const char *path, Logger &log) {
    const auto load_resource_file = reinterpret_cast<void *(*)(const char *, int type, int flags, void *callback,
                                                               void *param, int offset, int size)>(0x661760);
    const auto service_resource_loading = reinterpret_cast<int (*)()>(0x6626B0);  // advances queued loads
    if (!path || GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return false;
    char game_dir[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, game_dir, sizeof(game_dir));
    char *game_slash = strrchr(game_dir, '\\');
    if (!game_slash) return false;
    const size_t game_len = static_cast<size_t>(game_slash + 1 - game_dir);
    const char *name = _strnicmp(path, game_dir, game_len) == 0 ? path + game_len : path;
    void *file = load_resource_file(name, 1, 0, nullptr, nullptr, 0, 0);  // RESOURCE_FILE_GLOBAL
    const DWORD start = GetTickCount();
    while (file && !field<int>(file, 0x30) && static_cast<LONG>(GetTickCount() - start) < 3000) {  // LoadingFinishedFlag
        service_resource_loading();
        Sleep(1);
    }
    const bool loaded = file && field<int>(file, 0x30);
    log.line("texture pack %s: %s after %lu ms", name, loaded ? "loaded" : "NOT loaded", GetTickCount() - start);
    return loaded;
}

}  // namespace nfsmw
