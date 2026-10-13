# Reverse engineering reference (speed.exe 1.3)

What we know about the game's code, by subject. Each page states **what is true now**, not how we found out; confidence is
marked: **[read]** read in the disassembly, **[tested]** confirmed in the game or by a crash / log, **[guess]** inferred.
Addresses are speed.exe 1.3 (image base 0x400000). Function names are the decomp's (`Tools/nfsmw-decomp`).

| Page | Subject |
|---|---|
| [project.md](project.md) | Repo layout, game installs, build, deploy and test setup, tools |
| [input-system.md](input-system.md) | Devices, binding table, action queues, the Controls screen, the menus' joystick input |
| [controller-support.md](controller-support.md) | Controller state, Steam Input, mouse, Controls menu rows, button prompts, rumble |
| [split-screen.md](split-screen.md) | Quick Race entry, FEDatabase, views and rendering, streaming, Press Start |
| [menus-and-camera.md](menus-and-camera.md) | Options pages, FEng cloning, textures and packs, camera presets, boot flow |
| [dev-tools-lan.md](dev-tools-lan.md) | Starting the game twice, LAN |

How to add to this: put a finding in the page of its subject (create one if there is none), correct a line instead of adding
a contradicting one, and there is no log of past work (git history has it). Tools: `Tools/re/re.py`
(listing with names, cross references, name of an address); the names we gave functions are in `Tools/re/names.json` (address, name, confidence), shown over the decomp's names.
