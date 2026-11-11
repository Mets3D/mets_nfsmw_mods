---
name: nfsmw-reverse-engineering
description: How to read and document the code of speed.exe 1.3 (NFS Most Wanted 2005) in this repo - tools, naming, confidence marks, and how to turn a crash or log into a finding. Use before disassembling anything or when adding to agent_docs/ or Tools/re/names.json.
---

# Reverse engineering speed.exe 1.3

## Start from what exists
- `agent_docs/` pages (by subject) and `Tools/re/names.json` (our function names) before disassembling. The decomp (`Tools/nfsmw-decomp`, `symbols.json`) gives ~46k names and addresses but no readable bodies for most functions.
- The 2005 SDK (`Tools/nfsmw-2005-sdk`) has headers; NFSMWExtraOptions and the widescreen fix sources (`Tools/OtherMods/nfsmw-extra-options`, `Tools/OtherMods/widescreenfixespack`) show how others hooked the game (reference, GPL: do not copy).

## Tools (`Tools/re/re.py`, run with `python -I`; capstone in `Tools/re/pylib`)
- `re.py dis <exe> <addr> <bytes>` listing with names on calls / jumps; `re.py xref <exe> <addr>` who calls an address; `re.py name <addr>` function containing an address; `re.py unnamed` unnamed functions mentioned in docs.
- Read a function by its first ~0x60 bytes or grep the listing for `call`; widen only where needed. Jump tables and vtables: read the dwords from the file (the .data section) with a few lines of Python.
- Find constants: hash strings with `h = h*33 + c` from 0xFFFFFFFF (bStringHash), search the exe for the 4 bytes. Message names like PAD_LTRIGGER are hashes of the plain string.
- A crash: `eip` minus 0x400000 is the offset; `re.py name` gives the function; the surrounding registers say which pointer was bad (0xFDFDFDFD = uninitialised / freed memory).

## Ghidra: readable C instead of a listing (`Tools/re/ghidra_decomp.py`)
Use it when the question is *what does this function do* (control flow, which fields it reads, what it calls with what); keep `re.py dis` for exact bytes (prologues for hooks, patch sites) and `xref`. Ghidra's project is `D:\Tools\ghidra_projects`
(program "speed.exe", the Redux v3 install's exe; our function names from `names.json` and the decomp's 46k names are already in it, so the output reads `SummonChyron(...)`, `this->...` and shows the vtable calls).
Run from the repo root, with `python -I`; Ghidra and Java are found by the script's defaults (`D:\Tools\ghidra_12.1.3_PUBLIC`, `D:\Tools\jdk-21.0.12.1+1`), nothing to set up:
- `python -I Tools/re/ghidra_decomp.py 0x4F4FC0 0x595ED0` decompiles the functions that contain these addresses (give all of them in **one** call: starting Ghidra costs ~35 s each time, the functions themselves ~1 s; at most 20 per call). [tested]
- `--name SummonChyron` finds functions whose name contains the text (the way in when you only know a name). `--range 0x4A0000 0x500000 out.c` decompiles a whole range into a file, to `grep` instead of starting Ghidra again.
  Put the file in the scratchpad.
- `--import-names` only after `names.json` gained entries (it writes the project, so do not run it at the same time as another run; two plain decompiles at once were fine [tested]). New names then show in the next output.
- It is the *same decomp that is wrong sometimes*: struct offsets come out as `*(int *)(this + 0x150)`, which is the exe's truth (good, take them), but argument types, `thiscall` vs `fastcall` and the ends of switch tables
  can be off. A member offset or a calling convention that matters for a hook: confirm in `re.py dis`. Ignore the `_DAT_xxxxxx` ones: they are globals, read the address.
- Give the shell call a timeout of ~2 minutes; a handful of functions is done well within it.
- `.mcp.json` also lists a Ghidra MCP server (ReVa); it only exists in sessions that have its tools. If you have none, this script is the way.

## Method
1. Find the owner of the data: who reads this field, who writes it (a global's relocation list in `symbols.json`, `xref` of its function).
2. Work out the calling convention from the prologue and epilogue (`ret N` = callee pops; thiscall = ecx; floats return in st0).
3. **The decomp's struct layouts can be wrong for the PC exe** (the `Minimap` / `HudElement` members are 8 bytes higher, `FEngHud` 0x14 lower): take member offsets from the constructor's disassembly (stores to `[esi+X]` after each call), never from the headers alone. A header's class tells you what the fields mean; the exe tells you where they are.
4. Prefer a model that explains *all* observations; a log line or a crash that contradicts the model beats the model. Say so and correct the doc line instead of adding a contradicting one.
5. Test by the smallest hook or log line, and let the user run it; never assume a guess about structure layout is right before the game confirmed it.

## Record it (same turn)
- A function: an entry in `Tools/re/names.json` (`address: {name, confidence, note}`). Data layouts, globals, flows, gotchas: the page of the subject in `agent_docs/` with a mark per claim:
  **[read]** seen in the disassembly, **[tested]** confirmed in game or by a crash / log, **[guess]**. Correct lines in place; keep dead ends only if someone could retry them.
- **Before finishing a turn that touched `agent_docs/`, run `python -I Tools/re/re.py unnamed`**: it lists functions the docs mention that are still `sub_XXXXXX` in the decomp. Give each a name in
  `names.json` (confidence honest: a guess stays a guess) and refer to the function by that name in the docs. A function's description lives only in `names.json`, not in the docs.
- No chronological notes about solved problems.

## Hooking rules in this project
MinHook through `common/hooks.h`; thiscall hooks are `__fastcall` with an unused edx; check the 7 prologue bytes; stubs and byte patches through `common/patch.h` (checked). Patching a vtable of the D3D
device gets reverted, hook the d3d9 function bodies. Calls into the game from a mod: declare the exact calling convention and test with a log line first.
