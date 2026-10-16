// Options > Player > Language. The game can change its language while it runs: SetCurrentLanguage 0x57E6F0 (what sets it at start-up
// from the registry value `Language`) unloads the old language's texts and fonts, loads the new ones and tells the open screens to fetch
// their strings again (agent_docs/menus-and-camera.md). The row picks a language among those whose file is in the game's LANGUAGES
// folder; the language changes when the Player options are accepted. The choice is kept the way the game keeps it: the registry value
// `Language`, written through the game's own imports (RegCreateKeyA, RegSetValueExA... in speed.exe's import table), so it lands wherever
// the game's registry settings go (the widescreen fix with WriteSettingsToFile = 1 answers them from Settings.ini) and the next start-up
// reads it by itself.
#include "language.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "nfsmw_util.h"

namespace custom_menus::language {

namespace {

namespace opt = nfsmw::custom_menus;

constexpr uintptr_t kCurrentLanguage = 0x8F41C0;            // eLanguages (-1 before the first is set)
constexpr uintptr_t kLanguageTable = 0x8F40F8;              // LanguageInfoTable: 0x14 bytes each, id, name ("GERMAN"), file ("LANGUAGES\GERMAN.BIN")...
constexpr int kLanguageCount = 10;
constexpr uintptr_t kSetCurrentLanguage = 0x57E6F0;         // cdecl (eLanguages)
constexpr uintptr_t kFeDatabase = 0x91CF90;                 // pointer; +0x10: the gameplay settings, whose byte +0x3B is the speedometer unit
constexpr unsigned kSpeedoUnits = 0x3B;
constexpr uintptr_t kRegCreateKeyA = 0x890000, kRegCloseKey = 0x890008, kRegSetValueExA = 0x89000C, kRegOpenKeyExA = 0x890004;  // speed.exe's IAT
constexpr uintptr_t kRegistryKey = 0x8AEB7C;                // "Software\EA Games\Need for Speed Most Wanted"

nfsmw::Logger *g_log = nullptr;
void (*g_changed)() = nullptr;

struct Choice {
    int id;
    std::string registry;  // the name the game's registry value takes: English, French...
    std::string native;    // as the player knows it
};
std::vector<Choice> g_choices;
int g_pending = -1;  // the language id the row shows

int current() { return *reinterpret_cast<int *>(kCurrentLanguage); }

// The language's own name (Windows-1252 bytes, which is what the game's fonts have).
const char *native_name(int id) {
    switch (id) {
        case 0: return "English";
        case 1: return "Fran\xE7" "ais";
        case 2: return "Deutsch";
        case 3: return "Italiano";
        case 4: return "Espa\xF1ol";
        case 5: return "Nederlands";
        case 6: return "Svenska";
        case 7: return "Dansk";
        case 12: return "Polski";
        case 13: return "Suomi";
        default: return nullptr;
    }
}

// The languages the game has a file for.
void list_choices() {
    g_choices.clear();
    for (int i = 0; i < kLanguageCount; ++i) {
        const unsigned char *entry = reinterpret_cast<const unsigned char *>(kLanguageTable) + i * 0x14;
        const int id = *reinterpret_cast<const int *>(entry);
        const char *name = *reinterpret_cast<const char *const *>(entry + 4), *file = *reinterpret_cast<const char *const *>(entry + 8);
        if (!name || !file || !native_name(id) || GetFileAttributesA(file) == INVALID_FILE_ATTRIBUTES) continue;
        std::string registry = name;  // GERMAN -> German
        for (size_t c = 1; c < registry.size(); ++c) registry[c] = static_cast<char>(tolower(registry[c]));
        g_choices.push_back({id, registry, native_name(id)});
    }
}

int index_of(int id) {
    for (size_t i = 0; i < g_choices.size(); ++i)
        if (g_choices[i].id == id) return static_cast<int>(i);
    return -1;
}

// Remembers the choice the way the game remembers its own settings.
void save(const Choice &choice) {
    using OpenFn = LONG(WINAPI *)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
    using CreateFn = LONG(WINAPI *)(HKEY, LPCSTR, PHKEY);
    using SetFn = LONG(WINAPI *)(HKEY, LPCSTR, DWORD, DWORD, const BYTE *, DWORD);
    using CloseFn = LONG(WINAPI *)(HKEY);
    const auto open = *reinterpret_cast<OpenFn *>(kRegOpenKeyExA);
    const auto create = *reinterpret_cast<CreateFn *>(kRegCreateKeyA);
    const auto set = *reinterpret_cast<SetFn *>(kRegSetValueExA);
    const auto close = *reinterpret_cast<CloseFn *>(kRegCloseKey);
    const char *key = reinterpret_cast<const char *>(kRegistryKey);
    HKEY handle = nullptr;
    LONG result = open(HKEY_LOCAL_MACHINE, key, 0, KEY_ALL_ACCESS, &handle);
    if (result != ERROR_SUCCESS) result = create(HKEY_LOCAL_MACHINE, key, &handle);
    if (result == ERROR_SUCCESS)
        result = set(handle, "Language", 0, REG_SZ, reinterpret_cast<const BYTE *>(choice.registry.c_str()),
                     static_cast<DWORD>(choice.registry.size() + 1));
    if (result == ERROR_SUCCESS && handle) close(handle);
    g_log->line("language: saved \"%s\" as the game's Language setting: %s (%ld)", choice.registry.c_str(), result == ERROR_SUCCESS ? "ok" : "failed",
                static_cast<long>(result));
}

void apply(const Choice &choice) {
    // LanguageHasChanged sets the speedometer to km/h for every language but English: keep the player's setting.
    const auto settings = [] {
        unsigned char *db = *reinterpret_cast<unsigned char **>(kFeDatabase);
        return db ? *reinterpret_cast<unsigned char **>(db + 0x10) : nullptr;
    };
    unsigned char *s = settings();
    const unsigned char units = s ? s[kSpeedoUnits] : 0;
    g_log->line("language: switching to %s", choice.registry.c_str());
    reinterpret_cast<void(__cdecl *)(int)>(kSetCurrentLanguage)(choice.id);
    s = settings();
    if (s) s[kSpeedoUnits] = units;
    g_log->line("language: now %d", current());
    if (g_changed) g_changed();
    save(choice);
}

// ---- the row ----
constexpr opt::Row kRows[] = {
    {opt::kValueRow, "Language", 0,
     "Changes the language of the menus and texts. The game's speech stays as it is. It changes when you accept the changes."},
};

bool available() {
    list_choices();
    return g_choices.size() > 1;
}

void opened() {
    list_choices();
    g_pending = current();
}

unsigned value(int, char *out, unsigned size) {
    const int i = index_of(g_pending);
    snprintf(out, size, "%s", i >= 0 ? g_choices[i].native.c_str() : "?");
    return 0;
}

void change(int, int direction, bool) {
    if (g_choices.empty()) return;
    int i = index_of(g_pending);
    const int count = static_cast<int>(g_choices.size());
    i = i < 0 ? 0 : (i + (direction > 0 ? 1 : count - 1)) % count;
    g_pending = g_choices[i].id;
}

bool changed() { return g_pending != current(); }

void accept() {
    const int i = index_of(g_pending);
    if (g_pending != current() && i >= 0) apply(g_choices[i]);
}

void discard() { g_pending = current(); }
void defaults() {}  // the language is not one of the player's options to reset

opt::Page g_page;

}  // namespace

void provide(const opt::Host *host, nfsmw::Logger *log, void (*changed_fn)()) {
    g_log = log;
    g_changed = changed_fn;
    char translations[MAX_PATH];
    nfsmw::data_file("translations\\custom_menus", translations, sizeof(translations));
    host->register_translations(translations);
    g_page = {"language", "Language", "LANGUAGE", "LANGUAGE", 0, 0, nullptr, 1, kRows, available, opened, value, change, changed, accept, discard,
              defaults, opt::kPlayerOptions};
    host->register_page(&g_page);
}

}  // namespace custom_menus::language
