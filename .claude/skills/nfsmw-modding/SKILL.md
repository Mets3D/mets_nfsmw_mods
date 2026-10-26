---
name: nfsmw-modding
description: How to work on the NFS Most Wanted (2005) ASI mods in this repo - where code goes, building and deploying, testing with the user, logging, and token economy. Use for any change under Mods/ or when the user reports an in-game result.
---

# Working on the NFS-MW mods

## Orient cheaply
1. Read `agent_docs/README.md` (index), then only the page of the subject. Read the mod's own `ReadMe.md` for what it does. Do not read git history or large files whole; grep.
2. The code under `Mods/` is the source of truth for behaviour; `agent_docs/` is the source of truth for what we know about the game. No log file of past work exists on purpose.

## Where code goes
- A mod owns what it is about (`controller_support` = devices, bindings, rumble, look; `custom_menus` = Options framework; `split_screen_support` = the split screen; `dev_tools` = tools for making mods).
  Shared helpers are header-only in `Mods/common/`. **Put code where it belongs**; never defend a hack by its placement.
- Dependencies: `controller_support` depends on no other mod; `split_screen_support` may use `controller_support` through `common/controller_api.h` (looked up by export name, so it degrades when absent).
- Two plugins cannot hook one function: code that changes a screen sits with the hooks that build it. Hook tables go through `common/hooks.h` (7-byte prologue check, `[hooks]` skip list).
- When something works, look for what it makes obsolete and **delete it** (a refactor that only adds code is unfinished). Prefer fixing the cause over working around it; say so when something is a bridge.
- Each mod has a file per part, a header, a namespace, and an ini switch per part. Keep comments short and about why.

## Build and deploy
- Build: Windows, in a shell with `C:\msys64\mingw32\bin` first on PATH: `cd Mods && cmake -S . -B build -G "MinGW Makefiles" && cmake --build build`.
  Linux, cross-compiling: `cd Mods && cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=../Tools/nfsmw-2005-sdk/cmake/nfsmw-toolchain-mingw-i686.cmake && cmake --build build`.
  Re-run the configure after adding files. Details: `agent_docs/project.md`.
- Deploy by copying `Mods/build/nfsmw_<mod>.asi` into each game install's `scripts/` (or `python Mods/set_mods.py --mode ours --game <dir>`; no install path is hard-coded, always pass `--game`).
  **Do not launch the game by hand; the user tests.** The exception is the scripted test run (`python Tools/run_scenario.py --game <install> <scenario>`, see `agent_docs/project.md` "Automated test runs"), which the user allowed on the Redux v3 install: use it for what a script can check (boot, hooks installed, a screenshot of a menu or race, no crash), and say what it cannot judge. Never close a game the user started (check `Get-CimInstance Win32_Process` for steam.exe as the parent); if one is running, ask. `set_mods.py` closes a game running from that install before it changes anything (`--keep-running`
  to leave it), because a changed `.asi` crashes a running game (Wine maps the file; seen 2026-10-16: access violation in ntdll's TLS
  callbacks reading garbage, dump written 3 ms after a `cp`). So deploy with `set_mods.py`, not `cp`; if you must copy by hand, check
  `pgrep -af 'speed\.exe'` first and use `cp --remove-destination`. Windows: kill a running `speed.exe` before deploying (the file is locked).
- Our files at run time: `<game>/scripts/mets_mods/` (ini, logs, packs).

## Adding a mod
1. `Mods/<name>/src/main.cpp` with `NFSMW_PLUGIN_DECLARE` / `NFSMW_PLUGIN_MAIN` (`camera_settings/src/main.cpp` is a small one), a line in `Mods/CMakeLists.txt` (`nfsmw_add_plugin(...)`, and the `foreach` list below it).
2. Hooks through `common/hooks.h` (address + first 7 bytes; a changed function is skipped), log / strings / language table through `common/nfsmw_util.h`.
3. An Options menu page: export `nfsmw_custom_menus_provide` (copy from `Mods/custom_menus/example_options_page/src/main.cpp`, which documents every callback).
4. Add it to `Mods/set_mods.py` (`ALL_MODS`, the `--only` choices, texture packs), write the mod's own `ReadMe.md` (what it does for a player; no build or developer notes there), and put findings in `agent_docs/`.
5. Uninstall = delete the mod's `.asi` (and `.tpk`) from `scripts/`; everything else of ours is in `scripts/mets_mods/`.

## After the user tests
- Read the logs yourself (`scripts/mets_mods/nfsmw_*.log`, `nfsmw_dev_tools.log` (`[diagnostics] enabled=1` in its ini) for access violations: eip is `speed.exe+offset`, add 0x400000 and use `Tools/re/re.py name`). Do not ask the user to send logs.
- Say plainly what is confirmed, what is a guess, and what you did not run. Do not claim a fix works before the user has seen it.

## Logging
`log_line` only for what a bug report needs (setup, devices, hooks, what the user did, errors). Traces for development are temporary `log_line`s, taken out again once the question is answered.
A diagnosis that needs one fact gets one targeted line, not a dump; a flooded log costs the user's budget. But when the unknown is a *structure* (a tree of objects, a table), dump all of it once, in one compact line per item,
behind a one-shot trigger: a test run costs the user more than a few hundred log lines, and guessing which part to log in successive runs is the most expensive way to find out.

## Token economy (the user's session budget is small)
- Short answers: what changed, what to test, what is uncertain. No recaps of the whole task. But while working (especially anything that launches the game, which the user sees on their desktop) say in one line what you are about to do and why before each step, and report what each result showed as it comes in; the user follows along live and must never have to ask what is going on.
- Targeted reads and greps; disassemble only the bytes you need (a list of relocations or a whole function dump is almost never needed).
- Record a finding once: `agent_docs/<subject>.md` for data / flow / gotchas, `Tools/re/names.json` for a function. Do not write chronological notes about solved problems.
- Avoid questions whose answer the code or the user's earlier words give. Ask only for decisions that are the user's.

## Pitfalls
- Bash heredocs containing apostrophes break the tool: write scripts and files with the Write tool.
- Windows line endings: editing with sed/python keeps them; git warns about LF/CRLF, which is harmless.
- Commit only when asked.
