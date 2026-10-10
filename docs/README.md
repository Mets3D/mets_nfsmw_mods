# Reverse engineering reference (speed.exe 1.3)

What we know about the game's code, by subject. Each page states **what is true now**, not how we found out; confidence is
marked: **[read]** read in the disassembly, **[tested]** confirmed in the game or by a crash / log, **[guess]** inferred.
Addresses are speed.exe 1.3 (image base 0x400000). Function names are the decomp's (`Tools/nfsmw-decomp`).

| Page | Subject |
|---|---|
| [input-system.md](input-system.md) | Devices, binding table, action queues, the Controls screen, the menus' joystick input |

How to add to this: put a finding in the page of its subject (create one if there is none), correct a line instead of adding
a contradicting one, and keep `notes.md` for the project log (what was done, when, decisions). Tools: `Tools/re/re.py`
(listing with names, cross references, name of an address); the names we gave functions are in `Tools/re/names.json` (address, name, confidence), shown over the decomp's names.
