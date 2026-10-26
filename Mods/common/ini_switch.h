// Turning an option of another mod's ini off, for a mod that cannot work with it on. The other mod reads its ini as soon as its
// .asi is loaded, so this has to run when ours is loaded (NFSMW_PLUGIN_EARLY), which is before it only if ours sorts before it:
// the loader loads a folder's plugins alphabetically. Only the digit of the option's line is changed (comments and spacing stay).
// Header only, and only the file API: usable from DllMain.
#pragma once

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace nfsmw::ini_switch {

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

// Finds the value of the `key` line in the ini's text. Returns false when there is none (or it is a comment).
// `on` is whether the value is not 0; with `zero` the value is also changed to 0 in the text.
inline bool find_value(std::string &text, const char *key, bool zero, bool *on) {
    const size_t key_size = strlen(key);
    size_t line = 0;
    while (line < text.size()) {
        size_t end = text.find('\n', line);
        if (end == std::string::npos) end = text.size();
        size_t at = line;
        while (at < end && (text[at] == ' ' || text[at] == '\t')) ++at;
        if (end - at > key_size && _strnicmp(text.c_str() + at, key, key_size) == 0) {
            at += key_size;
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

// The path of an ini: next to the plugins (scripts\), or where older versions keep it (the game folder). False if neither exists.
inline bool find_path(const char *file_name, char *out, size_t size) {
    char game[MAX_PATH];
    GetModuleFileNameA(nullptr, game, sizeof(game));
    if (char *slash = strrchr(game, '\\')) *(slash + 1) = 0;
    for (const char *place : {"scripts\\", ""}) {
        snprintf(out, size, "%s%s%s", game, place, file_name);
        if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return true;
    }
    return false;
}

// Sets the option `key` to 0 in the ini at `path`.
inline Change turn_off(const char *path, const char *key) {
    std::string text;
    bool on = false;
    if (!detail::read_file(path, &text) || !detail::find_value(text, key, true, &on)) return Change::kNone;
    if (!on) return Change::kAlreadyOff;
    return detail::write_file(path, text) ? Change::kTurnedOff : Change::kFailed;
}

}  // namespace nfsmw::ini_switch
