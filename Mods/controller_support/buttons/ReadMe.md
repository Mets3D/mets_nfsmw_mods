# Controller button pictures

The pictures `nfsmw_controller_support` shows in the menus' button prompts while a controller is in use, and the list
that says which prompt of the game shows which picture.

```
buttons/
  prompts.ini        which picture each prompt of the game shows, for each style
  xbox/              A.png  B.png  X.png  Y.png  View.png  Menu.png  LB.png  RB.png  LB_RB.png  LeftStick.png  Empty.png
  playstation/       Cross.png  Circle.png  Square.png  Triangle.png  Select.png  Start.png  L1.png  R1.png  L1_R1.png  ...
  alternatives/      pictures that were replaced (a D-pad, a stick click), to copy back if you prefer them
  LICENSE            the licence of the pictures (see below)
```

A picture's file name says what it shows. `prompts.ini` is what connects them to the game:

```ini
[xbox]
PC_KEY_ENTER = A          ; the game's "Enter" prompt shows the A button
PC_KEY_ESC   = B
PC_KEY_Q     = B          ; the main menu's "Quit": B quits there
```

The name on the left is the game's own picture of the prompt (fixed by the game); the one on the right is a file in the
folder of the style. A picture can be used by any number of prompts. The style is chosen with `icons=xbox` or
`icons=playstation` in `nfsmw_controller_support.ini`.

## Changing something

Edit a PNG or `prompts.ini`, then run `python Mods/set_mods.py --mode ours`: it builds the pictures into
`nfsmw_controller_buttons.tpk` (with `Tools/button_pack.py`) and copies it and `prompts.ini` to the game's
`scripts/mets_mods/` folder. The plugin reads `prompts.ini` from there.

- Use the size of the picture you replace (the menu layout decides how big a prompt is drawn): 128x128 for single
  buttons, 256x128 for the two-in-one ones (`LB_RB`, and `LeftStick` with the picture in the left half), width and height
  powers of two, 8-bit RGBA PNG.
- A new prompt: add its line to `prompts.ini` in each style. The name the game uses for a prompt can be found with
  `log_images=1` in the ini (the log lists every image the game draws, by texture hash).
- `Empty` is fully transparent: the game has no controller version of those prompts, and shows nothing.

## Where they are from

The pictures, and the idea of swapping them, are from [ThirteenAG's WidescreenFixesPack](https://github.com/ThirteenAG/WidescreenFixesPack)
(its `NFSMostWanted.WidescreenFix`, the `ImproveGamepadSupport` option), under the MIT License: see [`LICENSE`](LICENSE).
They were exported from that mod's `.tpk` and renamed after what they show. `LeftStick` (a left stick instead of the
D-pad) is ours.
