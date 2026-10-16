#include "translations.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "nfsmw_util.h"

namespace custom_menus::translations {

namespace {

constexpr uintptr_t kCurrentLanguage = 0x8F41C0;  // eLanguages
const auto GetLanguageName = reinterpret_cast<const char *(*)(int)>(0x56B7E0);  // "GERMAN"

std::map<std::string, std::string> g_table;
std::vector<std::string> g_folders;  // every folder a mod registered, to read again when the language changes

// The game's texts are Windows-1252 single bytes, except Polish, whose font has its letters at other places (found by comparing
// the Polish texts with the English ones).
bool g_polish = false;
unsigned char polish_byte(unsigned code) {
    static const struct { unsigned code; unsigned char byte; } kMap[] = {
        {0x105, 0x85}, {0x107, 0x82}, {0x119, 0x81}, {0x142, 0x83}, {0x144, 0x87}, {0x15B, 0x84}, {0x17A, 0x88}, {0x17C, 0x86},
        {0x104, 0x92}, {0x106, 0x8C}, {0x118, 0x8B}, {0x141, 0x8A}, {0x143, 0x90}, {0x15A, 0x89}, {0x179, 0x94}, {0x17B, 0x93},
    };  // ą ć ę ł ń ś ź ż and Ą Ć Ę Ł Ń Ś Ź Ż (ó and Ó are Latin-1)
    for (const auto &m : kMap)
        if (m.code == code) return m.byte;
    return code < 0x100 ? static_cast<unsigned char>(code) : '?';
}

// UTF-8 to the game's character set (anything it has no letter for is ?).
std::string to_game_text(const std::string &utf8) {
    std::string out;
    for (size_t i = 0; i < utf8.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        if (c < 0x80) out += static_cast<char>(c);
        else if ((c & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
            const unsigned code = ((c & 0x1F) << 6) | (static_cast<unsigned char>(utf8[++i]) & 0x3F);
            out += static_cast<char>(g_polish ? polish_byte(code) : (code < 0x100 ? code : '?'));
        } else {
            out += '?';
            while (i + 1 < utf8.size() && (static_cast<unsigned char>(utf8[i + 1]) & 0xC0) == 0x80) ++i;
        }
    }
    return out;
}

std::string trimmed(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

void load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char line[1024];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        std::string text = line;
        if (n++ == 0 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);  // byte order mark
        text = trimmed(text);
        if (text.empty() || text[0] == '#') continue;
        const size_t eq = text.find(" = ");
        if (eq == std::string::npos) continue;
        g_table[to_game_text(trimmed(text.substr(0, eq)))] = to_game_text(trimmed(text.substr(eq + 3)));
    }
    fclose(f);
}

// The name of the game's language as in its files (GERMAN -> German), or empty for English.
std::string language_name() {
    const int language = *reinterpret_cast<int *>(kCurrentLanguage);
    const char *upper = language >= 0 ? GetLanguageName(language) : nullptr;
    if (!upper || !*upper || strcmp(upper, "ENGLISH") == 0) return "";
    std::string name = upper;
    g_polish = name == "POLISH";
    for (size_t i = 1; i < name.size(); ++i) name[i] = static_cast<char>(tolower(name[i]));
    return name;
}

}  // namespace

void add_folder(const char *folder) {
    if (!folder || !*folder) return;
    g_folders.push_back(folder);
    const std::string name = language_name();
    if (!name.empty()) load_file((std::string(folder) + "\\" + name + ".txt").c_str());
}

void reload() {
    g_table.clear();
    const std::string name = language_name();
    if (name.empty()) return;
    for (const std::string &folder : g_folders) load_file((folder + "\\" + name + ".txt").c_str());
}

int count() { return static_cast<int>(g_table.size()); }

const char *translate(const char *english) {
    if (g_table.empty() || !english) return english;
    const auto it = g_table.find(english);
    return it == g_table.end() ? english : it->second.c_str();
}

}  // namespace custom_menus::translations
