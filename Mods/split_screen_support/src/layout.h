// Where each player's picture goes on the screen when more than two people play: a grid of cells, each with the screen's own proportions (so a
// picture shrunk into a cell is not distorted, and the HUD, made for the whole screen, fits a cell scaled down). Players go left to right, top to
// bottom. Two players are not a grid here: they have the halves of the screen (split_view.cpp).
#pragma once

#include <d3d9.h>

namespace split_screen_support::layout {

// Cells on a side: 2 for three or four players, 3 for five to nine.
inline int grid_size(int players) {
    int n = 1;
    while (n * n < players) ++n;
    return n;
}

// The cell of `player` (0-based) of `players`, in pixels of a screen `width` x `height`. The cells tile the screen: the last column and row take the
// remainder.
inline RECT cell(int player, int players, long width, long height) {
    const int n = grid_size(players);
    const int column = player % n, row = player / n;
    RECT r;
    r.left = width * column / n;
    r.right = width * (column + 1) / n;
    r.top = height * row / n;
    r.bottom = height * (row + 1) / n;
    return r;
}

}  // namespace split_screen_support::layout
