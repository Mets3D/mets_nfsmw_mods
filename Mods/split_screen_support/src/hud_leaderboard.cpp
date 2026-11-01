/*
 * The leaderboard of a split screen HUD. Every FEngHud makes a LeaderBoard (0x59F0E0, 0x278 bytes, at FEngHud +0x2FC) and the game turns it on in a race when the
 * player's Leaderboard setting is on, but the split screen packages (HUD_Player1.fng ...) have none of its objects (LeaderBoardGroup, LBData_n, LeaderText_n,
 * LBBacking_n, OL_ICON_n): it shows nothing. The single player race HUD (HUD_SingleRace.fng) has them. So that package is pushed once for the race (every object
 * hidden), its LeaderBoardGroup cloned (FEObject::Clone, children and strings included; fe_clone.h finishes the copy) into each split screen HUD's package, and the
 * HUD's LeaderBoard made again on its own package (the old one deleted), which finds the copy by its names; LocalPlayer::UpdateHud fills it as before. The copy keeps
 * the resources (fonts, textures) of the original: an object's Handle and UserParam are what the source package's resource requests gave it, so that package stays
 * until the race's HUDs go (popped right after the copies were made, the game crashed on one of its freed objects in FEngine::ProcessObjectMessage).
 * The copy sits under the race information and is drawn with it (its part of the HUD, hud_views.cpp).
 */
#include "hud_leaderboard.h"

#include <cstdint>

#include "fe_clone.h"
#include "fe_objects.h"
#include "hud_views.h"
#include "max_players.h"
#include "split_screen_support.h"

namespace split_screen_support::hud_leaderboard {

namespace {

using namespace fe;

constexpr uintptr_t kFEng = 0x91CADC, kPushPackage = 0x516990, kPopPackage = 0x5169B0, kSetAllVisibility = 0x5248C0, kFindObject = 0x524850, kNew = 0x652AD0,
                    kLeaderBoardCtor = 0x59F0E0, kPackageAddObject = 0x50DB90;
constexpr unsigned kHudLeaderBoard = 0x2FC, kHudOuter = 8, kLeaderBoardSize = 0x278;  // FEngHud: the LeaderBoard, its UTL::COM::Object (the "outer" of its elements)
constexpr unsigned kObjectHandle = 0x24, kObjectUserParam = 0x28;                       // FEObject: the resource
constexpr unsigned kGroupHash = 0x5ED609A1, kRaceInformation = 0xB1C5ED0D;            // LeaderBoardGroup; RaceInformationGroup
constexpr float kOffsetX = 149.0f, kOffsetY = 36.0f;  // from the race information group to the copy: right edges aligned, the top just under it (the board is drawn at 3/4 of its place)
constexpr int kPriority = 0x66;                                                        // as the HUD pushes its own package
constexpr const char *kSource = "HUD_SingleRace.fng";

void *g_huds[nfsmw::kMaxPlayers];  // the HUD whose package got the copy, for each player
bool g_source_pushed = false;      // HUD_SingleRace.fng was pushed for the copies: popped when the last of those HUDs goes

void *fe_engine() { return *reinterpret_cast<void **>(kFEng); }

void *find_object(const char *package, unsigned hash) { return reinterpret_cast<void *(__cdecl *)(const char *, unsigned)>(kFindObject)(package, hash); }

void delete_leaderboard(void *hud) {
    void *&board = *reinterpret_cast<void **>(static_cast<char *>(hud) + kHudLeaderBoard);
    if (board) reinterpret_cast<void(__fastcall *)(void *, void *, int)>((*reinterpret_cast<void ***>(board))[0])(board, nullptr, 1);
    board = nullptr;
}

// What finish_clone leaves out for objects of another package: the resource the original was connected to.
void copy_resources(void *original, void *copy, int depth = 0) {
    field<unsigned>(copy, kObjectHandle) = field<unsigned>(original, kObjectHandle);
    field<unsigned>(copy, kObjectUserParam) = field<unsigned>(original, kObjectUserParam);
    if (!is_group(original) || depth > 8) return;
    void *o = first_child(original), *c = first_child(copy);
    for (; o && c; o = next_of(o), c = next_of(c)) copy_resources(o, c, depth + 1);
}

// Where an object is in its package: its own position plus its groups'.
void absolute_position(void *package, void *object, float &x, float &y) {
    x = position_x(object);
    y = position_y(object);
    void *child = object;
    for (int depth = 0; depth < 8; ++depth) {
        void *parent = nullptr;
        for (void *top = first_object(package); top && !parent; top = next_of(top))
            if (top != child && is_group(top)) parent = parent_of(top, child);
        if (!parent) break;
        x += position_x(parent);
        y += position_y(parent);
        child = parent;
    }
}

void dump(void *o, int depth) {
    void *data = field<void *>(o, 0x2C);
    if (!data || depth > 4) return;
    g_log.line("dump: %*s%08x type %d pos (%.0f, %.0f) size (%.2f, %.2f) flags %08x", depth * 2, "", hash_of(o), type_of(o), position_x(o), position_y(o), field<float>(data, 0x38),
               field<float>(data, 0x3C), field<unsigned>(o, 0x1C));
    if (is_group(o))
        for (void *c = first_child(o); c; c = next_of(c)) dump(c, depth + 1);
}

// The leaderboard's group of the single player HUD, copied into `package` (top level), or null.
void *copy_group(const char *package) {
    void *target = find_package(package);
    if (!target) return nullptr;
    void *engine = fe_engine();
    if (!find_package(kSource)) {
        g_source_pushed = true;
        reinterpret_cast<void(__fastcall *)(void *, void *, const char *, int)>(kPushPackage)(engine, nullptr, kSource, kPriority);
        reinterpret_cast<void(__cdecl *)(const char *, bool)>(kSetAllVisibility)(kSource, false);
    }
    void *source = find_package(kSource);
    void *group = source ? find_object(kSource, kGroupHash) : nullptr;
    void *copy = nullptr;
    if (group) {
        float x, y;
        absolute_position(source, group, x, y);
        copy = reinterpret_cast<void *(__fastcall *)(void *, void *, bool)>((*reinterpret_cast<void ***>(group))[1])(group, nullptr, false);  // FEObject::Clone
        if (copy) {
            finish_clone(group, copy);
            copy_resources(group, copy);
            position_x(copy) = x;
            position_y(copy) = y;
            reinterpret_cast<void(__fastcall *)(void *, void *, void *)>(kPackageAddObject)(target, nullptr, copy);
            if (ini_flag("diagnostics", "dump_hud", false)) {
                g_log.line("dump: the leaderboard copied into %s (at %.0f, %.0f in %s)", package, x, y, kSource);
                dump(copy, 0);
            }
        }
    }
    if (!copy) g_log.line("hud: no leaderboard for %s (%s)", package, !source ? "the single player HUD could not be pushed" : !group ? "it has no leaderboard" : "the copy failed");
    return copy;
}

// Puts the copy under the race information: its right edge at the race information's, its top a little under that group's place, and draws it with it.
void place(const char *package, void *copy) {
    void *target = find_package(package);
    void *info = find_object(package, kRaceInformation);
    if (!target || !info) {
        g_log.line("hud: the leaderboard of %s stays where the single player HUD has it (%s)", package, !target ? "no package" : "no race information found");
        return;
    }
    float x, y;
    absolute_position(target, info, x, y);
    position_x(copy) = x + kOffsetX;
    position_y(copy) = y + kOffsetY;
    field<unsigned>(copy, 0x1C) |= kFlagDirty;
    hud_views::classify_tree_like(copy, info);
    g_log.line("hud: the leaderboard of %s at (%.0f, %.0f), the race information at (%.0f, %.0f); HUD part %d (the race information's first object: %d)", package,
               position_x(copy), position_y(copy), x, y, hud_views::part_of_object(first_child(copy)), hud_views::part_of_object(first_child(info)));
}

}  // namespace

void attach(void *hud, const char *package, int player, int number) {
    if (!hud || !package || player < 0 || player >= nfsmw::kMaxPlayers || !fe_engine()) return;
    if (!find_object(package, kGroupHash)) {
        void *copy = copy_group(package);
        if (!copy) return;
        place(package, copy);
    }
    delete_leaderboard(hud);
    void *memory = reinterpret_cast<void *(__cdecl *)(size_t)>(kNew)(kLeaderBoardSize);
    void *board = memory ? reinterpret_cast<void *(__fastcall *)(void *, void *, void *, const char *, int)>(kLeaderBoardCtor)(memory, nullptr, static_cast<char *>(hud) + kHudOuter,
                                                                                                                         package, number)
                         : nullptr;
    *reinterpret_cast<void **>(static_cast<char *>(hud) + kHudLeaderBoard) = board;
    g_huds[player] = hud;
    g_log.line("hud: player %d's leaderboard is in %s (%p)", player + 1, package, board);
}

void detach(void *hud) {
    for (int i = 0; i < nfsmw::kMaxPlayers; ++i)
        if (g_huds[i] == hud) {
            delete_leaderboard(hud);  // (before the package's objects go)
            g_huds[i] = nullptr;
        }
    for (void *other : g_huds)
        if (other) return;
    if (g_source_pushed && fe_engine()) reinterpret_cast<void(__fastcall *)(void *, void *, const char *)>(kPopPackage)(fe_engine(), nullptr, kSource);
    g_source_pushed = false;
}

}  // namespace split_screen_support::hud_leaderboard
