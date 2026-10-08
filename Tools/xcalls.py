"""List call/jmp sites (E8/E9 rel32) targeting an address in speed.exe's .text, with the containing symbol."""
import json
import struct
import sys

exe, target = sys.argv[1], int(sys.argv[2], 16)
d = open(exe, "rb").read()
base = 0x400000
pe = struct.unpack_from("<I", d, 0x3C)[0]
so = pe + 24 + struct.unpack_from("<H", d, pe + 20)[0]
n, vs, va, rs, rp = struct.unpack_from("<8sIIII", d, so)
tva = base + va
funcs = sorted((int(s["start"], 16), int(s["size"], 16), s["name"])
               for s in json.load(open("nfsmw-decomp/config/SPEED_EXE_1_3/symbols.json"))["functions"])
import bisect
starts = [f[0] for f in funcs]
for i in range(rs - 4):
    if d[rp + i] in (0xE8, 0xE9):
        rel = struct.unpack_from("<i", d, rp + i + 1)[0]
        if (tva + i + 5 + rel) & 0xFFFFFFFF == target:
            site = tva + i
            j = bisect.bisect_right(starts, site) - 1
            f = funcs[j]
            print(f"{site:#x}: {'call' if d[rp+i]==0xE8 else 'jmp '}  in {f[2]} ({f[0]:#x}+{site-f[0]:#x})")
