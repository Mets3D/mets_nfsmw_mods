---
name: nfsmw-modding
description: How to work on the NFS Most Wanted (2005) ASI mods in this repo - where code goes, building and deploying, testing with the user, logging, and token economy. Use for any change under Mods/ or when the user reports an in-game result.
---

# Working on the NFS-MW mods

## Orient cheaply
1. Read `docs/README.md` (index), then only the page of the subject. Read the mod's own `ReadMe.md` for what it does. Do not read git history or large files whole; grep.
2. The code under `Mods/` is the source of truth for behaviour; `docs/` is the source of truth for what we know about the game. No log file of past work exists on purpose.

## Where code goes
- A mod owns what it is about (`controller_support` = devices, bindings, rumble, look; `custom_menus` = Options framework; `split_screen_support` = the split screen; `dev_tools` = tools for making mods).
  Shared helpers are header-only in `Mods/common/`. **Put code where it belongs**; never defend a hack by its placement.
- Dependencies: `controller_support` depends on no other mod; `split_screen_support` may use `controller_support` through `common/controller_api.h` (looked up by export name, so it degrades when absent).
- Two plugins cannot hook one function: code that changes a screen sits with the hooks that build it. Hook tables go through `common/hooks.h` (7-byte prologue check, `[hooks]` skip list).
- When something works, look for what it makes obsolete and **delete it** (a refactor that only adds code is unfinished). Prefer fixing the cause over working around it; say so when something is a bridge.
- Each mod has a file per part, a header, a namespace, and an ini switch per part. Keep comments short and about why.

## Build and deploy
- Build in a shell with `C:\msys64\mingw32\bin` first on PATH: `cd Mods && cmake -S . -B build -G "MinGW Makefiles" && cmake --build build` (re-run the configure after adding files).
- Deploy by copying `Mods/build/nfsmw_<mod>.asi` into `Game Versions/1.3/scripts/` and `Game Versions/GameTest/scripts/` (or `Mods/set_mods.ps1`). **Never launch the game.** The user tests; kill a running
  `speed.exe` before deploying (the file is locked).
- Our files at run time: `<game>/scripts/mets_mods/` (ini, logs, crash log, packs).

## After the user tests
- Read the logs yourself (`scripts/mets_mods/nfsmw_*.log`, `nfsmw_custom_menus_crash.log` for access violations: eip is `speed.exe+offset`, add 0x400000 and use `Tools/re/re.py name`). Do not ask the user to send logs.
- Say plainly what is confirmed, what is a guess, and what you did not run. Do not claim a fix works before the user has seen it.

## Logging
`log_line` only for what a bug report needs (setup, devices, hooks, what the user did, errors). Traces for development go through `log_debug` (`[log] verbose=1`) or a targeted switch (`[log] inputs=1`).
A diagnosis that needs one fact gets one targeted line, not a dump. A flooded log costs the user's budget.

## Token economy (the user's session budget is small)
- Short answers: what changed, what to test, what is uncertain. No narrated tool calls, no recaps of the whole task.
- Targeted reads and greps; disassemble only the bytes you need (a list of relocations or a whole function dump is almost never needed).
- Record a finding once: `docs/<subject>.md` for data / flow / gotchas, `Tools/re/names.json` for a function. Do not write chronological notes about solved problems.
- Avoid questions whose answer the code or the user's earlier words give. Ask only for decisions that are the user's.

## Pitfalls
- Bash heredocs containing apostrophes break the tool: write scripts and files with the Write tool.
- Windows line endings: editing with sed/python keeps them; git warns about LF/CRLF, which is harmless.
- Commit only when asked.
