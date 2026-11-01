/*
 * Rows of choices in the race options of a split screen race (Track_Options.fng), under the Players row (player_count.cpp) and before the AI rows: Layout (layout.h),
 * Start (start_grid.h) and, in a tollbooth race, Time (tollbooth.h). Each is kept in the ini ([race_options]) when it changes. Left and right step through the
 * values; the texts are language strings registered through nfsmw_custom_menus (without it there are no rows, and the ini's values are used).
 */
#include "race_rows.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "hooks.h"
#include "layout.h"
#include "option_row.h"
#include "patch.h"
#include "players.h"
#include "split_screen_support.h"
#include "start_grid.h"
#include "tollbooth.h"

namespace split_screen_support::race_rows {

namespace {

constexpr uintptr_t kSetupCallSite = 0x7BF73D, kGetRaceType = 0x5FAA20;  // (the constructor's call of UIQRTrackOptions::Setup, chained by player_count and the AI rows too)
constexpr unsigned kScreenRace = 0x128;                                    // the screen's GRaceParameters
constexpr int kRaceTollbooth = 4;
constexpr int kTimeLowest = 30, kTimeStep = 5, kTimeValues = (300 - kTimeLowest) / kTimeStep + 1;

struct Choice {
    const char *key;    // in [race_options]; the language strings are SPLIT_<key> and SPLIT_<key>_<value>
    const char *title;
    std::vector<std::string> words, texts;  // the ini's value and the row's text of each value
    int value;
    void (*apply)(int value);
    bool tollbooth_only;
    unsigned title_hash = 0;
    std::vector<unsigned> hashes;
    void *vtable[option_row::kSlots];
};

std::vector<Choice> g_choices;
bool g_texts = false;

unsigned hash_of(const char *key, int value) {
    char name[64];
    if (value < 0) snprintf(name, sizeof(name), "SPLIT_%s", key);
    else snprintf(name, sizeof(name), "SPLIT_%s_%d", key, value);
    for (char *c = name; *c; ++c) *c = static_cast<char>(toupper(static_cast<unsigned char>(*c)));
    return nfsmw::bstring_hash(name);
}

template <int N>
void __fastcall draw(void *self, void *) {
    const Choice &c = g_choices[N];
    option_row::set_texts(self, c.title_hash, c.hashes[c.value]);
}

template <int N>
void __fastcall act(void *self, void *, const char *, unsigned data) {
    if (data != option_row::kPadLeft && data != option_row::kPadRight) return;
    Choice &c = g_choices[N];
    const int count = static_cast<int>(c.words.size());
    c.value = (c.value + (data == option_row::kPadRight ? 1 : count - 1)) % count;
    c.apply(c.value);
    WritePrivateProfileStringA("race_options", c.key, c.words[c.value].c_str(), g_ini);
    option_row::value_changed(self, data);
    g_log.line("race options: %s %s", c.key, c.words[c.value].c_str());
}

using SetupFn = void(__fastcall *)(void *, void *);
uintptr_t g_setup = 0;

void __fastcall setup_and_rows(void *screen, void *edx) {
    reinterpret_cast<SetupFn>(g_setup)(screen, edx);
    if (!players::split_screen() || !g_texts) return;
    void *race = *reinterpret_cast<void **>(static_cast<char *>(screen) + kScreenRace);
    const bool tollbooth = race && reinterpret_cast<int(__fastcall *)(void *)>(kGetRaceType)(race) == kRaceTollbooth;
    for (Choice &c : g_choices)
        if (!c.tollbooth_only || tollbooth) option_row::add(screen, c.vtable);
    option_row::refresh(screen);
}

void add(const char *key, const char *title, std::vector<std::string> words, std::vector<std::string> texts, int fallback, void (*apply)(int), bool tollbooth_only) {
    Choice c{key, title, std::move(words), std::move(texts), fallback, apply, tollbooth_only};
    char saved[32];
    GetPrivateProfileStringA("race_options", key, "", saved, sizeof(saved), g_ini);
    for (size_t i = 0; i < c.words.size(); ++i)
        if (_stricmp(saved, c.words[i].c_str()) == 0) c.value = static_cast<int>(i);
    c.apply(c.value);
    g_choices.push_back(std::move(c));
}

template <int N>
void make_vtable() { option_row::make_vtable(g_choices[N].vtable, &act<N>, &draw<N>); }

}  // namespace

void register_texts(const nfsmw::custom_menus::Host *host) {
    for (Choice &c : g_choices) {  // (made at install: the host keeps the pointers)
        c.title_hash = hash_of(c.key, -1);
        host->register_text(c.title_hash, c.title);
        c.hashes.clear();
        for (size_t i = 0; i < c.texts.size(); ++i) {
            c.hashes.push_back(hash_of(c.key, static_cast<int>(i)));
            host->register_text(c.hashes.back(), c.texts[i].c_str());
        }
    }
    g_texts = !g_choices.empty();
}

void install() {
    add("layout", "Layout", {"horizontal", "vertical"}, {"Horizontal", "Vertical"}, 0, [](int v) { layout::set_vertical(v == 1); }, false);
    add("start", "Start", {"front", "rear", "random"}, {"Players Front", "Players Rear", "Random"}, 0,
        [](int v) { start_grid::set_start(static_cast<start_grid::Start>(v)); }, false);
    std::vector<std::string> words, texts;
    for (int i = 0; i < kTimeValues; ++i) {
        words.push_back(std::to_string(kTimeLowest + i * kTimeStep));
        texts.push_back(words.back() + "%");
    }
    add("tollbooth_time", "Time", std::move(words), std::move(texts), (100 - kTimeLowest) / kTimeStep,
        [](int v) { tollbooth::set_time_percent(kTimeLowest + v * kTimeStep); }, true);
    make_vtable<0>();
    make_vtable<1>();
    make_vtable<2>();
    if (const uintptr_t previous = nfsmw::chain_call(kSetupCallSite, reinterpret_cast<uintptr_t>(&setup_and_rows))) g_setup = previous;
    else g_log.line("race options: the call at %#x is not a call; no Layout, Start and Time rows", static_cast<unsigned>(kSetupCallSite));
}

}  // namespace split_screen_support::race_rows
