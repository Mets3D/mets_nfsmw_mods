# Project: layout, build, test setup

## Game installs (`Game Versions/`, ignored by git)
Folder names are whatever the user picks; nothing in the scripts or mods hard-codes them, always pass the install directory as an argument.
Two copies exist on this machine (both speed.exe 1.3, two different cracks, so two different exe byte sizes/hashes — our hooks check the
first 7 bytes of each function, so the same addresses install on both):
- `AllMods/` (12 GB): the widescreen fix and ~20 other mods grabbed from the internet, played before this modding project started
  (ASI loader `dinput8.dll`, `scripts/`). `speed.exe` 5,926,912 bytes, SHA-256 `0C5675A0...2067E342`.
- `SomeMods/` (2.9 GB): vanilla 1.3 from the ISO (RLD crack), where mods (including ours) are added one at a time and checked for
  compatibility. `speed.exe` 6,029,312 bytes, SHA-256 `80774C2E...`.
- `python Mods/set_mods.py --mode all|ours|none --game <dir> [--only <mods>]` picks which `.asi` load (the rest wait in `scripts_disabled/`) and builds the texture packs.

## Our mods (`Mods/`, one folder each, built together)
`custom_menus` (Options menu framework, crash log), `camera_settings` (camera presets, Options > Camera), `controller_support` (controller input, bindings
per controller, camera look, rumble, button icons; namespaces are `pad::`), `split_screen_support`, `dev_tools` (start-up and diagnostic tools), `common/`
(header-only: log file, `hooks.h`, language table merge, XInput, the APIs between mods). Each mod has a ReadMe for players; how to work on them is in `.claude/skills/nfsmw-modding`, what we know about the game in `docs/`.
- Everything our mods write or ship lives in `<game>/scripts/mets_mods/` (`nfsmw::data_file()`): `.ini`, `.log`, `nfsmw_custom_menus_crash.log`, texture packs.
  Uninstall = delete the `.asi` files and that folder.
- `common/hooks.h` `install_hooks`: checks the first 7 bytes (a function that starts with a jump, i.e. another mod's hook, is hooked on top of it; any other difference skips the hook), optional `[hooks]` skip list in the mod's ini, required hooks
  are all-or-nothing; log lines read "hooks installed - <group>: n of m". **Two plugins cannot hook the same function**, so code that builds a screen sits
  next to the hooks that build it (that is why the Options menu is its own mod).
- Icons: `<mod>/OPTIONS_*.png` become `<mod>.tpk` (`Tools/tpk_build.py`).

## Build
Windows (MSYS2 MINGW32): `cd Mods && cmake -S . -B build -G "MinGW Makefiles" && cmake --build build` with `C:\msys64\mingw32\bin` first on PATH
(installed 2026-10-06 with `winget install MSYS2.MSYS2`, then `pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-make`; not on the
system PATH; uninstall with `winget uninstall MSYS2.MSYS2` and delete `C:\msys64`).

Linux (cross-compile, installed 2026-10-13 with `dnf install mingw32-gcc-c++ mingw32-binutils mingw32-crt mingw32-headers mingw32-filesystem
mingw32-winpthreads`): `cd Mods && cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=../Tools/nfsmw-2005-sdk/cmake/nfsmw-toolchain-mingw-i686.cmake &&
cmake --build build`. The game itself runs through Steam Proton Experimental; only the build is cross-compiled, nothing in `Game Versions/` runs
under Linux natively.

Either way `build/` is disposable. The runtime must be linked statically (`Mods/CMakeLists.txt`), or the ASI
loader fails with "Error 126" (missing `libgcc_s_dw2-1.dll`).

## Testing the game
- The `.asi` cannot be replaced while the game runs. Windows: a hung `speed.exe` that `taskkill` cannot kill dies with
  `Get-CimInstance Win32_Process -Filter "Name='speed.exe'" | Invoke-CimMethod -MethodName Terminate`. Linux, running under Steam Proton Experimental:
  kill the game's own process by PID (`pgrep -f '(^Z:.*|^\./)speed\.exe$'`); `pkill -f speed.exe` also matches and kills the shell running it.
- Linux: launching without Steam, and backtraces. `cd <install> && STEAM_COMPAT_DATA_PATH=~/.local/share/Steam/steamapps/compatdata/<id> STEAM_COMPAT_CLIENT_INSTALL_PATH=~/.local/share/Steam
  "<Proton - Experimental>/proton" run ./speed.exe` (`<id>`: the non-Steam shortcut's compatdata folder). Backtraces of a running game: `WINEPREFIX=<compatdata>/pfx WINEDLLPATH="<proton>/files/lib/wine:<proton>/files/lib64/wine"
  "<proton>/files/bin/wine" winedbg --command "bt all"` (works on a Steam-launched game too); map `.asi` offsets with `i686-w64-mingw32-nm -n` on `Mods/build/*.dll` (offset + ImageBase).
- **Black screen at start-up, no frame drawn** [tested]: the main thread hangs in DirectSound's start-up (`EAXSND8Wrapper::Initialize` -> dsound -> Wine `mmdevapi`) when PipeWire has no real output - cards' profiles "off", only
  "Dummy Output", no default sink (`pactl info`, `wpctl status`). Not the mods: it happens with none loaded. `systemctl --user restart wireplumber pipewire pipewire-pulse` brought the outputs back and the game booted every time.
  How to tell: the boot flow has no current screen (`*0x91CF48` null or its screen unset) and `eFrameCounter` (0x982B78) / `RealTimeFrames` (0x8F82F4) do not advance. The other start-up stalls are the save-directory check
  waiting on a message (MemoryCard +0x14, see `menus-and-camera.md`) or never finishing.
- On the copy with the widescreen fix and other mods (`AllMods/`), always keep those other mods on: without them the game renders no 3D and garbled text.
- Direct3D 9: patching the device's vtable gets reverted within ~13 frames on this install; hook the `d3d9.dll` function bodies with MinHook. The device
  pointer is `[0x982BDC]`.

## Reference clones (`Tools/`, ignored except the SDK submodule)
`nfsmw-2005-sdk` (submodule: headers the mods build against), `nfsmw-2005-re` (article repo with `tools/nfsmw-tool`), `nfsmw-decomp`
(https://github.com/dbalatoni13/nfsmw: `config/SPEED_EXE_1_3/symbols.json` = ~46k PC function names and addresses), `nfsmw-extra-options` (source of
NFSMWExtraOptions, GPL-3.0; reference only), `widescreenfixespack` (source of the widescreen fix, e.g. `Controller.ixx`), `nfs-xtendedinput` (not used).
Scripts: `re/re.py` (listing with names, cross references, `names.json` overlay), `tpk_list.py` / `tpk_export.py` / `tpk_build.py` (texture packs;
`Exports/fe_textures/` is a gallery from the export), `attrib_hash.py`, `button_pack.py`, older `xref.py` / `xcalls.py` / `compare_names.py` / `check_addrs.py`.
