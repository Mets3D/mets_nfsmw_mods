---
name: nfsmw-reverse-engineering
description: How to read and document the code of speed.exe 1.3 (NFS Most Wanted 2005) in this repo - tools, naming, confidence marks, and how to turn a crash or log into a finding. Use before disassembling anything or when adding to docs/ or Tools/re/names.json.
---

# Reverse engineering speed.exe 1.3

## Start from what exists
- `docs/` pages (by subject) and `Tools/re/names.json` (our function names) before disassembling. The decomp (`Tools/nfsmw-decomp`, `symbols.json`) gives ~46k names and addresses but no readable bodies for most functions.
- The 2005 SDK (`Tools/nfsmw-2005-sdk`) has headers; NFSMWExtraOptions and the widescreen fix sources (`Tools/nfsmw-extra-options`, `Tools/widescreenfixespack`) show how others hooked the game (reference, GPL: do not copy).

## Tools (`Tools/re/re.py`, run with `python -I`; capstone in `Tools/re/pylib`)
- `re.py dis <exe> <addr> <bytes>` listing with names on calls / jumps; `re.py xref <exe> <addr>` who calls an address; `re.py name <addr>` function containing an address; `re.py unnamed` unnamed functions mentioned in docs.
- Read a function by its first ~0x60 bytes or grep the listing for `call`; widen only where needed. Jump tables and vtables: read the dwords from the file (the .data section) with a few lines of Python.
- Find constants: hash strings with `h = h*33 + c` from 0xFFFFFFFF (bStringHash), search the exe for the 4 bytes. Message names like PAD_LTRIGGER are hashes of the plain string.
- A crash: `eip` minus 0x400000 is the offset; `re.py name` gives the function; the surrounding registers say which pointer was bad (0xFDFDFDFD = uninitialised / freed memory).

## Method
1. Find the owner of the data: who reads this field, who writes it (a global's relocation list in `symbols.json`, `xref` of its function).
2. Work out the calling convention from the prologue and epilogue (`ret N` = callee pops; thiscall = ecx; floats return in st0).
3. Prefer a model that explains *all* observations; a log line or a crash that contradicts the model beats the model. Say so and correct the doc line instead of adding a contradicting one.
4. Test by the smallest hook or log line, and let the user run it; never assume a guess about structure layout is right before the game confirmed it.

## Record it (same turn)
- A function: an entry in `Tools/re/names.json` (`address: {name, confidence, note}`). Data layouts, globals, flows, gotchas: the page of the subject in `docs/` with a mark per claim:
  **[read]** seen in the disassembly, **[tested]** confirmed in game or by a crash / log, **[guess]**. Correct lines in place; keep dead ends only if someone could retry them.
- **Before finishing a turn that touched `docs/`, run `python -I Tools/re/re.py unnamed`**: it lists functions the docs mention that are still `sub_XXXXXX` in the decomp. Give each a name in
  `names.json` (confidence honest: a guess stays a guess) and refer to the function by that name in the docs. A function's description lives only in `names.json`, not in the docs.
- No chronological notes about solved problems.

## Hooking rules in this project
MinHook through `common/hooks.h`; thiscall hooks are `__fastcall` with an unused edx; check the 7 prologue bytes; stubs and byte patches through `common/patch.h` (checked). Patching a vtable of the D3D
device gets reverted, hook the d3d9 function bodies. Calls into the game from a mod: declare the exact calling convention and test with a log line first.
