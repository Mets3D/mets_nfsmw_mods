"""Find code references to an absolute address in speed.exe and print context."""
import re
import struct
import sys

exe, target = sys.argv[1], int(sys.argv[2], 16)
d = open(exe, "rb").read()
base = 0x400000
pe = struct.unpack_from("<I", d, 0x3C)[0]
nsec = struct.unpack_from("<H", d, pe + 6)[0]
osz = struct.unpack_from("<H", d, pe + 20)[0]
so = pe + 24 + osz
for i in range(nsec):
    n, vs, va, rs, rp = struct.unpack_from("<8sIIII", d, so + i * 40)
    if n.startswith(b".text"):
        tva, tptr, tsz = base + va, rp, rs
pat = re.escape(struct.pack("<I", target))
for m in re.finditer(pat, d[tptr:tptr + tsz]):
    o = tptr + m.start()
    va = tva + m.start()
    print(f"{va:#x}: {d[o-3:o+8].hex(' ')}")
