// Helpers shared by the mods: per-mod log file, raw field access, the game's string hashes, and merging our own
// strings into the game's language table. Header only.
#pragma once

#include <nfsmw_sdk/nfsmw_sdk.h>

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <cstring>

namespace nfsmw {

// (The SDK already has nfsmw::field<T>(base, offset): a raw field of a game object, offsets from the PC disassembly.)

// A log file in the game folder, opened when the mod starts. Each mod keeps one and wraps vline() in its own
// printf-style log_line().
class Logger {
  public:
    // Opens `name` in the data folder (see data_file), starting it afresh.
    void open_in_data_folder(const char *name);
    void line(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
        va_list ap;
        va_start(ap, fmt);
        vline(fmt, ap);
        va_end(ap);
    }
    void vline(const char *fmt, va_list ap) {
        if (!file_) return;
        vfprintf(file_, fmt, ap);
        fputc('\n', file_);
        fflush(file_);
    }

  private:
    FILE *file_ = nullptr;
};

// Everything our mods write or ship (settings, logs, texture packs) lives in one folder, <game>/scripts/mets_mods/, next to the
// .asi plugins as other mods keep their settings, but in a folder of its own. Created on first use.
inline void data_file(const char *name, char *out, size_t out_size) {
    GetModuleFileNameA(nullptr, out, static_cast<DWORD>(out_size));
    if (char *slash = strrchr(out, '\\')) *(slash + 1) = 0;
    strncat(out, "scripts\\", out_size - strlen(out) - 1);
    CreateDirectoryA(out, nullptr);
    strncat(out, "mets_mods\\", out_size - strlen(out) - 1);
    CreateDirectoryA(out, nullptr);
    strncat(out, name, out_size - strlen(out) - 1);
}

inline void Logger::open_in_data_folder(const char *name) {
    char path[MAX_PATH];
    data_file(name, path, sizeof(path));
    file_ = fopen(path, "w");
}

// ---- hashes -----------------------------------------------------------------------------------------------------
// Language strings and textures: h = h * 33 + c from 0xFFFFFFFF (textures: names in upper case).
inline unsigned bstring_hash(const char *s) {
    unsigned h = 0xFFFFFFFFu;
    for (; *s; ++s) h = h * 33 + static_cast<unsigned char>(*s);
    return h;
}
inline unsigned texture_hash(const char *name) {
    unsigned h = 0xFFFFFFFFu;
    for (; *name; ++name) h = h * 33 + static_cast<unsigned char>(toupper(static_cast<unsigned char>(*name)));
    return h;
}

// ---- language table -----------------------------------------------------------------------------------------------
// Every reader of the game's text (labels, titles, dialogs) resolves a hash with one binary search over a sorted
// {hash, text} table, so our own strings are merged into that table and the game's code does the rest.
struct LangEntry {
    unsigned hash;
    const char *text;
};

namespace lang {
constexpr uintptr_t kTable = 0x91CF80;  // LangEntry*: the loaded language
constexpr uintptr_t kCount = 0x91CF78;  // its number of entries
}  // namespace lang

// Merges `entries` (any order; the texts must outlive the game) into the table. Does nothing when the table already
// holds the first entry's hash (this language is already merged, by us or an earlier call) or is not loaded yet.
// Returns true when it merged.
inline bool merge_language_entries(const LangEntry *entries, unsigned count) {
    LangEntry *cur = *reinterpret_cast<LangEntry **>(lang::kTable);
    const unsigned n = *reinterpret_cast<unsigned *>(lang::kCount);
    if (!cur || !n || !count) return false;
    unsigned lo = 0, hi = n;
    while (lo < hi) {
        const unsigned mid = (lo + hi) / 2;
        if (cur[mid].hash < entries[0].hash) lo = mid + 1;
        else hi = mid;
    }
    if (lo < n && cur[lo].hash == entries[0].hash) return false;

    LangEntry *ours = static_cast<LangEntry *>(malloc(count * sizeof(LangEntry)));
    if (!ours) return false;
    memcpy(ours, entries, count * sizeof(LangEntry));
    for (unsigned i = 1; i < count; ++i)  // insertion sort by hash (unsigned: the game's search is unsigned)
        for (unsigned j = i; j > 0 && ours[j - 1].hash > ours[j].hash; --j) {
            const LangEntry t = ours[j];
            ours[j] = ours[j - 1];
            ours[j - 1] = t;
        }
    const auto game_alloc = reinterpret_cast<void *(*)(unsigned)>(0x652AD0);  // operator new
    LangEntry *merged = static_cast<LangEntry *>(game_alloc((n + count) * sizeof(LangEntry)));
    if (merged) {
        unsigned a = 0, b = 0, o = 0;
        while (a < n || b < count) {
            if (b >= count || (a < n && cur[a].hash <= ours[b].hash)) merged[o++] = cur[a++];
            else merged[o++] = ours[b++];
        }
        *reinterpret_cast<unsigned *>(lang::kCount) = n + count;
        *reinterpret_cast<LangEntry **>(lang::kTable) = merged;
    }
    free(ours);
    return merged != nullptr;
}

}  // namespace nfsmw
