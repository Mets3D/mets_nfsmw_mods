"""Read-only sanity check: do the SDK's addresses fit a given speed.exe?

Functions should land on plausible x86 prologue/entry bytes; globals should
land inside a non-executable data/bss section. Never writes to the exe.
"""
import json
import struct
import sys
from collections import Counter

exe_path, addrs_path = sys.argv[1], sys.argv[2]
data = open(exe_path, "rb").read()

pe = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
opt_size = struct.unpack_from("<H", data, pe + 20)[0]
base = struct.unpack_from("<I", data, pe + 0x34)[0]
sec_off = pe + 24 + opt_size
sections = []
for i in range(nsec):
    name, vsize, vaddr, rawsize, rawptr, _, _, _, _, chars = struct.unpack_from(
        "<8sIIIIIIHHI", data, sec_off + i * 40)
    sections.append((name.rstrip(b"\0").decode(), base + vaddr, max(vsize, rawsize), rawptr, rawsize, chars))
    print(f"section {sections[-1][0]:8} VA {base+vaddr:#010x} size {vsize:#x} exec={bool(chars & 0x20000000)}")


def locate(va):
    for name, start, size, rawptr, rawsize, chars in sections:
        if start <= va < start + size:
            off = rawptr + (va - start)
            return name, bool(chars & 0x20000000), (off if va - start < rawsize else None)
    return None, False, None


# first bytes of common MSVC 7.1 function entries (not exhaustive)
PROLOGUE = [b"\x55\x8b\xec", b"\x83\xec", b"\x6a", b"\x56", b"\x57", b"\x53", b"\x51", b"\x52",
            b"\x8b", b"\x33", b"\x31", b"\xb8", b"\xa1", b"\x64\xa1", b"\x81\xec", b"\xc2", b"\xc3",
            b"\x55", b"\x68", b"\x8d", b"\xe9", b"\xff\x25", b"\x0f\xb7", b"\x0f\xbe", b"\xd9", b"\xdd"]

res = Counter()
bad = []
for a in json.load(open(addrs_path)):
    va = int(a["addr"], 16)
    sec, ex, off = locate(va)
    if a["kind"] == "function":
        if sec is None or not ex or off is None:
            res["func: not in exec section"] += 1
            bad.append((a["fullname"], a["addr"], "not in exec section"))
        else:
            b = data[off:off + 4]
            ok = any(b.startswith(p) for p in PROLOGUE)
            res["func: plausible entry" if ok else "func: odd bytes"] += 1
            if not ok:
                bad.append((a["fullname"], a["addr"], b.hex()))
    else:
        if sec is None or ex:
            res["global: outside data"] += 1
            bad.append((a["fullname"], a["addr"], f"section={sec}"))
        else:
            res["global: in data section"] += 1

print()
for k, v in sorted(res.items()):
    print(f"{v:4}  {k}")
for b in bad[:25]:
    print("  ?", *b)
