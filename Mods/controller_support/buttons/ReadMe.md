# Controller button pictures

The pictures `nfsmw_controller_support` shows in the menus' button prompts while a controller is in use, one folder per
style (`xbox/`, `playstation/`). [`prompts.ini`](prompts.ini) says which prompt of the game shows which picture, and how
to rebuild the pack after changing something.

- Use the size of the picture you replace (the menu layout decides how big a prompt is drawn): 128x128 for single
  buttons, 256x128 for the two-in-one ones (`LB_RB`, and `LeftStick` with the picture in the left half), width and height
  powers of two, 8-bit RGBA PNG.
- `Empty` is fully transparent: the game has no controller version of those prompts, and shows nothing.

## Credit

Some of the pictures were taken from [ThirteenAG's WidescreenFixesPack](https://github.com/ThirteenAG/WidescreenFixesPack).
