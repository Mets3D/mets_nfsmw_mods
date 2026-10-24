#include "steam_pads.h"

namespace pad::steam_pads {

namespace {

constexpr unsigned short kValveVendor = 0x28DE, kSteamVirtualPad = 0x11FF;
constexpr int kMaxSlots = 16;

struct Entry {
    bool valid;
    char name[64];
    unsigned short vid, pid;
    char type[16];
    unsigned long long handle;
};
Entry g_entries[kMaxSlots];
char g_path[MAX_PATH];
bool g_path_known = false;
unsigned long long g_stamp = 0;
DWORD g_next = 0;

void find_path() {
    g_path_known = true;
    g_path[0] = 0;
    const DWORD n = GetEnvironmentVariableA("SteamVirtualGamepadInfo", g_path, sizeof(g_path));
    if (n > 0 && n < sizeof(g_path)) return;
    g_path[0] = 0;
    HKEY key;
    char steam[MAX_PATH] = {};
    DWORD size = sizeof(steam) - 1;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExA(key, "SteamPath", nullptr, nullptr, reinterpret_cast<BYTE *>(steam), &size) != ERROR_SUCCESS) steam[0] = 0;
        RegCloseKey(key);
    }
    if (!steam[0]) return;
    snprintf(g_path, sizeof(g_path), "%s\\config\\virtualgamepadinfo.txt", steam);
    for (char *c = g_path; *c; ++c)
        if (*c == '/') *c = '\\';
}

unsigned long long file_stamp() {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!g_path[0] || !GetFileAttributesExA(g_path, GetFileExInfoStandard, &a)) return 0;
    return (static_cast<unsigned long long>(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime;
}

// "[slot N]" starts an entry; its lines are key=value (name, VID, PID, type, handle). Numbers are decimal or 0x hex.
void parse(char *text) {
    memset(g_entries, 0, sizeof(g_entries));
    int slot = -1;
    for (char *line = text; *line;) {
        char *end = line + strcspn(line, "\r\n");
        const char saved = *end;
        *end = 0;
        int number;
        if (sscanf(line, "[slot %d]", &number) == 1) {
            slot = number >= 0 && number < kMaxSlots ? number : -1;
            if (slot >= 0) g_entries[slot].valid = true;
        } else if (slot >= 0) {
            if (char *equals = strchr(line, '=')) {
                *equals = 0;
                const char *key = line, *value = equals + 1;
                Entry &e = g_entries[slot];
                if (!strcmp(key, "name")) snprintf(e.name, sizeof(e.name), "%s", value);
                else if (!strcmp(key, "VID")) e.vid = static_cast<unsigned short>(strtoul(value, nullptr, 0));
                else if (!strcmp(key, "PID")) e.pid = static_cast<unsigned short>(strtoul(value, nullptr, 0));
                else if (!strcmp(key, "type")) snprintf(e.type, sizeof(e.type), "%s", value);
                else if (!strcmp(key, "handle")) e.handle = _strtoui64(value, nullptr, 0);
            }
        }
        if (!saved) break;
        line = end + 1;
    }
}

// Reads the file again when it changed (looked at every 3 seconds at most).
void refresh() {
    const DWORD now = GetTickCount();
    if (g_path_known && static_cast<LONG>(now - g_next) < 0) return;
    g_next = now + 3000;
    if (!g_path_known) {
        find_path();
        log_line("Steam Input: the virtual controllers' file is %s", g_path[0] ? g_path : "not found (Steam's path is unknown)");
    }
    const unsigned long long stamp = file_stamp();
    if (!stamp || stamp == g_stamp) return;
    g_stamp = stamp;
    char text[4096] = {};
    if (FILE *f = fopen(g_path, "rb")) {
        fread(text, 1, sizeof(text) - 1, f);
        fclose(f);
    }
    parse(text);
    for (int i = 0; i < kMaxSlots; ++i) {
        const Entry &e = g_entries[i];
        if (e.valid) log_line("Steam Input: virtual controller %d is %s (%04X:%04X, %s, handle %#llx)", i, e.name, e.vid, e.pid, e.type, e.handle);
    }
}

const Entry *entry(int slot) {
    refresh();
    return slot >= 0 && slot < kMaxSlots && g_entries[slot].valid ? &g_entries[slot] : nullptr;
}

// What vendor 045E, 054C or 057E say about the kind of controller, for one that is not behind Steam.
const char *type_of_vendor(unsigned short vid) {
    return vid == 0x045E ? "xbox" : vid == 0x054C ? "ps" : vid == 0x057E ? "switch" : "";
}

}  // namespace

int steam_slot_of_xinput(int xinput_slot) {
    xinput::CapabilitiesEx caps;
    if (!xinput::capabilities_ex(xinput_slot, &caps) || caps.vendor_id != kValveVendor || caps.product_id != kSteamVirtualPad) return -1;
    return static_cast<int>(caps.steam_slot);
}

int steam_slot_of_path(const wchar_t *path) {
    const wchar_t *ig = wcsstr(path, L"IG_");
    int slot = -1;
    return ig && swscanf(ig, L"IG_%d", &slot) == 1 ? slot : -1;
}

bool device_of_slot(int xinput_slot, RealDevice *out) {
    xinput::CapabilitiesEx caps;
    if (!xinput::capabilities_ex(xinput_slot, &caps)) return false;
    *out = {};
    out->steam_slot = -1;
    if (caps.vendor_id == kValveVendor && caps.product_id == kSteamVirtualPad) {
        out->steam_slot = static_cast<int>(caps.steam_slot);
        out->vid = caps.vendor_id;
        out->pid = caps.product_id;
        if (const Entry *e = entry(out->steam_slot)) {
            snprintf(out->name, sizeof(out->name), "%s", e->name);
            snprintf(out->type, sizeof(out->type), "%s", e->type);
            out->vid = e->vid;
            out->pid = e->pid;
            out->handle = e->handle;
        }
    } else {
        out->vid = caps.vendor_id;
        out->pid = caps.product_id;
        snprintf(out->type, sizeof(out->type), "%s", type_of_vendor(caps.vendor_id));
    }
    return true;
}

bool key_of_slot(int xinput_slot, char *out, size_t size) {
    RealDevice device;
    if (!device_of_slot(xinput_slot, &device) || device.steam_slot < 0 || !device.name[0]) return false;
    int same_before = 0;
    for (int s = 0; s < xinput_slot; ++s) {
        RealDevice other;
        if (device_of_slot(s, &other) && other.steam_slot >= 0 && other.vid == device.vid && other.pid == device.pid) ++same_before;
    }
    if (same_before) snprintf(out, size, "%04X_%04X_%d", device.vid, device.pid, same_before + 1);
    else snprintf(out, size, "%04X_%04X", device.vid, device.pid);
    return true;
}

void name_of_slot(int xinput_slot, char *out, size_t size) {
    RealDevice device;
    out[0] = 0;
    if (xinput_slot >= 0 && device_of_slot(xinput_slot, &device)) snprintf(out, size, "%s", device.name);
}

void describe(int xinput_slot, char *out, size_t size) {
    RealDevice d;
    if (xinput_slot < 0 || !device_of_slot(xinput_slot, &d)) snprintf(out, size, "nothing");
    else if (d.steam_slot >= 0 && !d.name[0]) snprintf(out, size, "Steam virtual controller %d, not in Steam's file", d.steam_slot);
    else if (d.steam_slot >= 0) snprintf(out, size, "%s, %04X:%04X, %s, Steam virtual controller %d, handle %#llx", d.name, d.vid, d.pid, d.type, d.steam_slot, d.handle);
    else snprintf(out, size, "%04X:%04X%s%s, not behind Steam", d.vid, d.pid, d.type[0] ? ", " : "", d.type);
}

}  // namespace pad::steam_pads
