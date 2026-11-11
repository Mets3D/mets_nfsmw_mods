// Where each player's picture goes on the screen. The screen is divided into lines: rows (the Layout row of the race options says Horizontal, the game's way) or
// columns (Vertical). One player has the whole screen, two have a line each (split_view.cpp crops each picture to its cell's shape), three have a line for the
// first player and two cells in the other line, and from four players on the screen is a grid of cells, each with the screen's own proportions (so a picture
// shrunk into a cell is not distorted, and the HUD, made for the whole screen, fits a cell scaled down). The first player is the first cell, top left.
// A player who is out of the race (knocked out, totalled, engine blown) gives their cell to the others after a few seconds (layout.cpp): the cells of their line
// slide over it until the others share the line, and a line with nobody racing in it closes, the other lines growing into it (with two left in one row of four:
// side by side, each the full height). The last player racing gets the whole screen.
#pragma once

#include <d3d9.h>

namespace nfsmw::custom_menus {
struct Host;
}

namespace split_screen_support::layout {

// Cells on a side of the grid of four or more players: 2 for four players, 3 for five to nine.
inline int grid_size(int players) {
    int n = 1;
    while (n * n < players) ++n;
    return n;
}

// The Layout row: the lines are columns (true) or rows. For the races that follow.
bool vertical();
void set_vertical(bool on);

// The cell `player` (0-based) of `players` has when everybody races, in pixels of a screen `width` x `height`. The cells tile the screen: the last column and
// row take the remainder.
RECT home_cell(int player, int players, long width, long height);

// Once per frame of a split screen race, before the cells are asked for: moves the cells of the players who are out.
void update(int players);

// The cell of `player` now: the home cell, or while the cells slide or after they did, its part of its line and the line's part of the screen. It can be empty (a
// player who is out).
RECT cell(int player, int players, long width, long height);

// How big the HUD is in the player's cell now, before the player's own HUD Scale: the HUD is made for the whole screen's width (640 units), and a cell takes it
// scaled by the cell's width as a part of the screen's.
float hud_scale(int player, int players, long width, long height);

// How big the rear view mirror is in the player's cell now, before the player's own HUD Scale: the HUD's scale, but also no bigger than the cell's height as a
// part of the screen's (the picture of the mirror is wide and sits at the top: in the rows of the Horizontal layout, whose cells are as wide as the screen and
// half as tall, it is half the size it has with the whole screen; the cell growing or shrinking when a player is out takes the mirror with it).
float mirror_scale(int player, int players, long width, long height);

// How much of their place on the screen the player still has: 1 while they race (and for a player who is out, until their cell goes), 0 once it is gone. What is
// only for this player (their sounds, the track streamed around them, their picture) fades or stops with it.
float presence(int player);

// Whether the screen is the game's own two halves, top and bottom, of two or three players (where the game's line between them belongs).
bool game_halves(int players);

// The Options > Advanced Video row for allow_split_screen_overtake.
void register_page(const nfsmw::custom_menus::Host *host);

void install();

}  // namespace split_screen_support::layout
