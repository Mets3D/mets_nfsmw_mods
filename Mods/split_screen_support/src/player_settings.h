// The game's per-player settings (PlayerSettings: camera view, the HUD toggles of Options > Player, ...) for players 3 and 4 (see player_settings.cpp).
#pragma once

namespace split_screen_support::player_settings {

// While alive, the settings array entry `slot` (0 or 1: the one the game reads for a HUD or an options screen) holds the settings of player `who` (0 to 3).
class Use {
public:
    Use(int who, int slot);
    ~Use();
    Use(const Use &) = delete;
    Use &operator=(const Use &) = delete;

private:
    unsigned char *a_ = nullptr, *b_ = nullptr;
};

void install();

}  // namespace split_screen_support::player_settings
