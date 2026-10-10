# Controller button pictures

The pictures `nfsmw_controller_support` shows in the menus' button prompts while a controller is in use: Xbox ones
(`XBOX_*.png`) and PlayStation ones (`PS_*.png`), one pair for each keyboard picture of the game (`KEY_ENTER` replaces
`PC_KEY_ENTER`, `L1` and `R1` are the shoulder buttons, ...). Which set is shown is `icons=` in
`nfsmw_controller_support.ini`.

## Editing a picture

Edit the PNG and run `Mods/set_mods.ps1 -Mode ours`: it builds all of them into `nfsmw_controller_buttons.tpk` (with
`Tools/tpk_build.py`) and copies it to the game's `scripts/mets_mods/` folder.

- Keep the file name: it is the texture's name, and the game finds the picture by it.
- Keep the size (the width and height must be powers of two) and an 8-bit RGBA PNG; the background is transparent.
- `*_LOAD` and `*_ARROW_KEYS_UP_DOWN` are fully transparent: on a controller the game shows nothing there.

## Where they are from

The pictures, and the idea of swapping them, are from [ThirteenAG's WidescreenFixesPack](https://github.com/ThirteenAG/WidescreenFixesPack)
(its `NFSMostWanted.WidescreenFix`, the `ImproveGamepadSupport` option), under the MIT License: see [`LICENSE`](LICENSE).
They were exported from that mod's `.tpk` as they are.
