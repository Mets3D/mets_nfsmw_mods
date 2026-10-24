#include "profile.h"

namespace pad::profile {

namespace {

constexpr uintptr_t kFEDatabase = 0x91CF90;  // FEDatabase*; the first player's UserProfile at +0x10, its name in the first 0x20 bytes
constexpr uintptr_t kMemoryCard = 0x91CB20;  // MemoryCard*; the save system's interface at +0xC8
constexpr int kInterfaceAt = 0xC8, kRootAt = 0x18C, kNameBytes = 0x20;
// The save system (the interface's first member, an implementation object) keeps the save root as an absolute path at +0x18C, and loads
// "%s/%s/%s" of root, profile name, profile name (_ProcessLoad 0x7F5255). Only read it: sub_7F3D00, which looks like its getter, is the
// setter (`_fullpath(impl+0x18C, argument)`; sub_6CBF00 uses it to set the root to Documents\NFS Most Wanted): calling it with an empty
// buffer made the root the current directory.
constexpr char kFileName[] = "controller_settings.ini";

char g_file[MAX_PATH];
char g_name[kNameBytes + 1];  // the profile name the file was worked out for
DWORD g_retry = 0;            // while there is no folder: when to look again
bool g_known = false;

// The current profile's name (reads memory only).
void read_name(char (&out)[kNameBytes + 1]) {
    void *database = *reinterpret_cast<void **>(kFEDatabase);
    const char *name = database ? static_cast<const char *>(field<void *>(database, 0x10)) : nullptr;
    memset(out, 0, sizeof(out));
    if (name) memcpy(out, name, kNameBytes);
}

bool profile_folder(const char (&name)[kNameBytes + 1], char *out, size_t size) {
    void *card = *reinterpret_cast<void **>(kMemoryCard);
    void *iface = card ? field<void *>(card, kInterfaceAt) : nullptr;
    void *impl = iface ? field<void *>(iface, 0) : nullptr;
    if (!name[0] || !impl) return false;
    char root[MAX_PATH] = {};
    memcpy(root, static_cast<const char *>(impl) + kRootAt, sizeof(root) - 1);
    if (!root[0] || root[1] != ':') return false;  // an absolute path with a drive, as _fullpath makes it
    snprintf(out, size, "%s\\%s", root, name);
    const DWORD attributes = GetFileAttributesA(out);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

unsigned long long write_time(const char *path) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!path[0] || !GetFileAttributesExA(path, GetFileExInfoStandard, &a)) return 0;
    return (static_cast<unsigned long long>(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime;
}

}  // namespace

// The disk is only looked at when the profile's name changes, and, while it has no folder (never saved), every two seconds.
const char *file() {
    char name[kNameBytes + 1];
    read_name(name);
    const DWORD now = GetTickCount();
    const bool same = g_known && !strcmp(name, g_name);
    if (same && (g_file[0] || static_cast<LONG>(now - g_retry) < 0)) return g_file;
    g_known = true;
    g_retry = now + 2000;
    memcpy(g_name, name, sizeof(g_name));
    char folder[MAX_PATH];
    if (profile_folder(name, folder, sizeof(folder))) snprintf(g_file, sizeof(g_file), "%s\\%s", folder, kFileName);
    else g_file[0] = 0;
    return g_file;
}

bool get_string(const char *section, const char *key, char *out, size_t size) {
    out[0] = 0;
    const char *profile = file();
    if (profile[0]) GetPrivateProfileStringA(section, key, "", out, static_cast<DWORD>(size), profile);
    if (!out[0]) GetPrivateProfileStringA(section, key, "", out, static_cast<DWORD>(size), g_ini);
    return out[0] != 0;
}

float get_float(const char *section, const char *key, float def) {
    char text[32];
    return get_string(section, key, text, sizeof(text)) ? static_cast<float>(atof(text)) : def;
}

int get_int(const char *section, const char *key, int def) {
    char text[32];
    return get_string(section, key, text, sizeof(text)) ? atoi(text) : def;
}

void set(const char *section, const char *key, const char *value) {
    const char *profile = file();
    WritePrivateProfileStringA(section, key, value, profile[0] ? profile : g_ini);
}

unsigned long long stamp() { return write_time(g_ini) + write_time(file()); }

}  // namespace pad::profile
