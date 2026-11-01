// Where each player's picture goes on the screen. One player has the whole screen, two have the halves (split_view.cpp crops a band for each), three have
// the top half and the two quarters under it, and from four players on the screen is a grid of cells, each with the screen's own proportions (so a picture
// shrunk into a cell is not distorted, and the HUD, made for the whole screen, fits a cell scaled down). The first player is the first cell, top left.
// A player who is out of the race (knocked out, totalled, engine blown) gives their cell to the others after a few seconds (layout.cpp): the cells of their row
// slide sideways until the others share its width, and a row with nobody racing in it closes, the other rows growing taller (with two left in the bottom row of
// four: side by side, each the full height). The last player racing gets the whole screen.
#pragma once

#include <d3d9.h>

namespace split_screen_support::layout {

// Cells on a side of the grid of four or more players: 2 for four players, 3 for five to nine.
inline int grid_size(int players) {
    int n = 1;
    while (n * n < players) ++n;
    return n;
}

// The cell `player` (0-based) of `players` has when everybody races, in pixels of a screen `width` x `height`. The cells tile the screen: the last column and
// row take the remainder.
inline RECT home_cell(int player, int players, long width, long height) {
    if (players <= 1) return {0, 0, width, height};
    if (players == 2) return {0, height * player / 2, width, height * (player + 1) / 2};
    if (players == 3) {
        if (player == 0) return {0, 0, width, height / 2};
        return {width * (player - 1) / 2, height / 2, width * player / 2, height};
    }
    const int n = grid_size(players);
    const int column = player % n, row = player / n;
    RECT r;
    r.left = width * column / n;
    r.right = width * (column + 1) / n;
    r.top = height * row / n;
    r.bottom = height * (row + 1) / n;
    return r;
}

// How big the HUD is in a home cell before the player's own HUD Scale: the HUD is made for the whole screen (a screen's width of it is 640 units), so a cell that
// is at least half the screen takes it as it is (the width of the screen), a smaller one scales it down by its width as a part of the screen's.
inline float hud_base_scale(const RECT &cell, long width, long height) {
    const long cell_width = cell.right - cell.left, cell_height = cell.bottom - cell.top;
    if (2 * cell_width * cell_height >= width * height) return 1.0f;
    return static_cast<float>(cell_width) / static_cast<float>(width);
}

// Once per frame of a split screen race, before the cells are asked for: moves the cells of the players who are out.
void update(int players);

// The cell of `player` now: the home cell, or while the cells slide or after they did, its part of its row and the row's part of the screen. It can be empty (a
// player who is out).
RECT cell(int player, int players, long width, long height);

// How much of their place on the screen the player still has: 1 while they race (and for a player who is out, until their cell goes), 0 once it is gone. What is
// only for this player (their sounds, the track streamed around them, their picture) fades or stops with it.
float presence(int player);

// Whether the cells are not the home cells any more (the line between the halves of the home layout no longer belongs).
bool changed(int players);

// The HUD's base scale in the cell the player has now: the home cell's, grown with the cell's width (up to the screen's own).
float hud_scale(int player, int players, long width, long height);

}  // namespace split_screen_support::layout
