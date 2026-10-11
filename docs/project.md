# Project: layout, build, test setup

## Game installs (`Game Versions/`, ignored by git)
- Original install `F:\Games\Misc\Need for Speed Most Wanted` is never modified (`speed.exe` 5,926,912 bytes, SHA-256 `0C5675A0...2067E342`).
- `GameTest/` = 11.7 GB copy with the widescreen fix and ~20 other mods (ASI loader `dinput8.dll`, `scripts/`).
- `1.3/` = vanilla 1.3 from the ISO (RLD crack `speed.exe`, 6,029,312 bytes, SHA-256 `80774C2E...`), only the ASI loader and our `.asi` files added.
  Our hooks check the first 7 bytes of each function, and the same addresses install on both exes.
- `Mods/set_mods.ps1 -Mode all|ours|none [-Game <dir>] [-Only <mods>]` picks which `.asi` load (the rest wait in `scripts_disabled/`) and builds the texture packs.

## Our mods (`Mods/`, one folder each, built together)
`custom_menus` (Options menu framework, crash log), `camera_settings` (camera presets, Options > Camera), `controller_support` (controller input, bindings
per controller, camera look, rumble, button icons; namespaces are `pad::`), `split_screen_support`, `dev_tools` (multiple copies, network log), `common/`
(header-only: log file, `hooks.h`, language table merge, XInput, the APIs between mods). Each mod has a ReadMe for players; how to work on them is in `.claude/skills/nfsmw-modding`, what we know about the game in `docs/`.
- Everything our mods write or ship lives in `<game>/scripts/mets_mods/` (`nfsmw::data_file()`): `.ini`, `.log`, `nfsmw_custom_menus_crash.log`, texture packs.
  Uninstall = delete the `.asi` files and that folder.
- `common/hooks.h` `install_hooks`: checks the first 7 bytes, skips a changed function, optional `[hooks]` skip list in the mod's ini, required hooks
  are all-or-nothing; log lines read "hooks installed - <group>: n of m". **Two plugins cannot hook the same function**, so code that builds a screen sits
  next to the hooks that build it (that is why the Options menu is its own mod).
- Icons: `<mod>/OPTIONS_*.png` become `<mod>.tpk` (`Tools/tpk_build.py`).

## Build (MSYS2 MINGW32)
`cd Mods && cmake -S . -B build -G "MinGW Makefiles" && cmake --build build` with `C:\msys64\mingw32\bin` first on PATH (installed 2026-10-06 with
`winget install MSYS2.MSYS2`, then `pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-make`; not on the system PATH; uninstall with
`winget uninstall MSYS2.MSYS2` and delete `C:\msys64`). `build/` is disposable. The runtime must be linked statically (`Mods/CMakeLists.txt`), or the ASI
loader fails with "Error 126" (missing `libgcc_s_dw2-1.dll`).

## Testing the game
- The `.asi` cannot be replaced while the game runs; a hung `speed.exe` that `taskkill` cannot kill dies with
  `Get-CimInstance Win32_Process -Filter "Name='speed.exe'" | Invoke-CimMethod -MethodName Terminate`.
- On GameTest always keep the other mods on: without them the game renders no 3D and garbled text.
- Direct3D 9: patching the device's vtable gets reverted within ~13 frames on this install; hook the `d3d9.dll` function bodies with MinHook. The device
  pointer is `[0x982BDC]`.

## Reference clones (`Tools/`, ignored except the SDK submodule)
`nfsmw-2005-sdk` (submodule: headers the mods build against), `nfsmw-2005-re` (article repo with `tools/nfsmw-tool`), `nfsmw-decomp`
(https://github.com/dbalatoni13/nfsmw: `config/SPEED_EXE_1_3/symbols.json` = ~46k PC function names and addresses), `nfsmw-extra-options` (source of
NFSMWExtraOptions, GPL-3.0; reference only), `widescreenfixespack` (source of the widescreen fix, e.g. `Controller.ixx`), `nfs-xtendedinput` (not used).
Scripts: `re/re.py` (listing with names, cross references, `names.json` overlay), `tpk_list.py` / `tpk_export.py` / `tpk_build.py` (texture packs;
`Exports/fe_textures/` is a gallery from the export), `attrib_hash.py`, `button_pack.py`, older `xref.py` / `xcalls.py` / `compare_names.py` / `check_addrs.py`.
