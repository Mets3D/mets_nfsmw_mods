// The widescreen fix's ini (NFSMostWanted.WidescreenFix.ini) and its ImproveGamepadSupport option, which replaces the game's
// keyboard pictures in the menus by controller pictures. Its older versions (the one in Redux v3) do it by loading a texture
// pack that redefines the pictures' names (PC_KEY_ENTER, ...), so they show controller buttons whatever the player is using,
// and the button prompts of nfsmw_controller_support, which switch between the keyboard and the controller pictures by the
// last input, have no keyboard picture left to switch to. nfsmw_controller_support does that job itself, so the option is
// turned off. Header only, and only the file API: usable from DllMain.
#pragma once

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace nfsmw::widescreen_ini {

enum class Change { kNone, kAlreadyOff, kTurnedOff, kFailed };

namespace detail {

inline bool read_file(const char *path, std::string *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) out->append(buffer, n);
    fclose(f);
    return true;
}

inline bool write_file(const char *path, const std::string &text) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    return fclose(f) == 0 && ok;
}

// Finds the value of the ImproveGamepadSupport line in the ini's text. Returns false when there is none (or it is a comment).
// `on` is whether the value is not 0; with `zero` the value is also changed to 0 in the text (comments and spacing stay).
inline bool find_value(std::string &text, bool zero, bool *on) {
    static const char kKey[] = "ImproveGamepadSupport";
    size_t line = 0;
    while (line < text.size()) {
        size_t end = text.find('\n', line);
        if (end == std::string::npos) end = text.size();
        size_t at = line;
        while (at < end && (text[at] == ' ' || text[at] == '\t')) ++at;
        if (end - at > sizeof(kKey) && _strnicmp(text.c_str() + at, kKey, sizeof(kKey) - 1) == 0) {
            at += sizeof(kKey) - 1;
            while (at < end && (text[at] == ' ' || text[at] == '\t')) ++at;
            if (at < end && text[at] == '=') {
                ++at;
                while (at < end && (text[at] == ' ' || text[at] == '\t')) ++at;
                size_t digits = at;
                while (digits < end && text[digits] >= '0' && text[digits] <= '9') ++digits;
                if (digits > at) {
                    *on = text.compare(at, digits - at, "0") != 0;
                    if (zero && *on) text.replace(at, digits - at, "0");
                    return true;
                }
            }
        }
        line = end + 1;
    }
    return false;
}

}  // namespace detail

// The path of the ini: next to the plugins (scripts\), or where older versions keep it (the game folder). False if neither exists.
inline bool find_path(char *out, size_t size) {
    char game[MAX_PATH];
    GetModuleFileNameA(nullptr, game, sizeof(game));
    if (char *slash = strrchr(game, '\\')) *(slash + 1) = 0;
    for (const char *place : {"scripts\\", ""}) {
        snprintf(out, size, "%s%sNFSMostWanted.WidescreenFix.ini", game, place);
        if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return true;
    }
    return false;
}

// Whether the ini has ImproveGamepadSupport on (not 0).
inline bool gamepad_support_on(const char *path) {
    std::string text;
    bool on = false;
    return detail::read_file(path, &text) && detail::find_value(text, false, &on) && on;
}

// Sets ImproveGamepadSupport to 0.
inline Change turn_off_gamepad_support(const char *path) {
    std::string text;
    bool on = false;
    if (!detail::read_file(path, &text) || !detail::find_value(text, true, &on)) return Change::kNone;
    if (!on) return Change::kAlreadyOff;
    return detail::write_file(path, text) ? Change::kTurnedOff : Change::kFailed;
}

}  // namespace nfsmw::widescreen_ini
