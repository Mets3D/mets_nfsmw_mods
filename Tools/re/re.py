"""Small reverse engineering helpers for speed.exe 1.3 (no disassembler install needed beyond capstone).

  python -I Tools/re/re.py dis  <speed.exe> <addr hex> <bytes>   x86 listing; calls and jumps get the decomp's names
  python -I Tools/re/re.py xref <speed.exe> <addr hex>           every direct call / jmp to that address in .text
  python -I Tools/re/re.py name <addr hex>                       function containing the address (decomp symbols)
  python -I Tools/re/re.py unnamed                               functions the docs/ pages mention that are still sub_XXXXXX (add them to names.json)

capstone: pip install capstone --target Tools/re/pylib   (the folder is ignored by git)
Names come from Tools/nfsmw-decomp/config/SPEED_EXE_1_3/symbols.json, overlaid with Tools/re/names.json (ours). Always run with -I (no implicit imports).
"""
import bisect
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SYMBOLS = os.path.join(HERE, '..', 'nfsmw-decomp', 'config', 'SPEED_EXE_1_3', 'symbols.json')
BASE = 0x400000


def sections(exe):
    pe = struct.unpack_from('<I', exe, 0x3C)[0]
    count = struct.unpack_from('<H', exe, pe + 6)[0]
    first = pe + 24 + struct.unpack_from('<H', exe, pe + 20)[0]
    out = []
    for i in range(count):
        name, vsize, va, rsize, raw = struct.unpack_from('<8sIIII', exe, first + 40 * i)
        out.append((name, BASE + va, max(vsize, rsize), raw, rsize))
    return out


def read(exe, addr, n):
    for _, va, size, raw, rsize in sections(exe):
        if va <= addr < va + size:
            return exe[raw + addr - va:raw + addr - va + n]
    return b''


def functions():
    """The decomp's functions, with our own names (names.json) over them."""
    data = json.load(open(SYMBOLS))['functions']
    names = {int(s['start'], 16): s['name'] for s in data}
    ours = json.load(open(os.path.join(HERE, 'names.json')))
    for address, entry in ours.items():
        if address.startswith('0x'):
            names[int(address, 16)] = entry['name']
    table = sorted(names.items())
    return table, [t[0] for t in table]


def name_of(addr, table, starts):
    i = bisect.bisect_right(starts, addr) - 1
    if i < 0:
        return '?'
    offset = addr - table[i][0]
    return table[i][1][:90] + (f'+{offset:#x}' if offset else '')


def dis(exe, addr, n):
    sys.path.insert(0, os.path.join(HERE, 'pylib'))
    import capstone
    table, starts = functions()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for ins in md.disasm(read(exe, addr, n), addr):
        text = f'{ins.address:#x}: {ins.mnemonic} {ins.op_str}'
        m = re.match(r'(call|jmp) (0x[0-9a-f]+)$', f'{ins.mnemonic} {ins.op_str}')
        print(text + (f'   ; {name_of(int(m.group(2), 16), table, starts)}' if m else ''))


def xref(exe, target):
    for name, va, size, raw, rsize in sections(exe):
        if not name.startswith(b'.text'):
            continue
        data = exe[raw:raw + rsize]
        for i in range(len(data) - 5):
            if data[i] in (0xE8, 0xE9):
                rel = struct.unpack_from('<i', data, i + 1)[0]
                if (va + i + 5 + rel) & 0xFFFFFFFF == target:
                    print(hex(va + i), 'call' if data[i] == 0xE8 else 'jmp')


def unnamed():
    """Addresses in docs/*.md that start a function the decomp still calls sub_XXXXXX / nullsub and names.json does not name."""
    import glob
    data = json.load(open(SYMBOLS))['functions']
    decomp = {int(s['start'], 16): s['name'] for s in data}
    ours = {int(k, 16) for k in json.load(open(os.path.join(HERE, 'names.json'))) if k.startswith('0x')}
    found = {}
    for path in sorted(glob.glob(os.path.join(HERE, '..', '..', 'docs', '*.md'))):
        text = open(path, encoding='utf-8').read()
        for m in re.finditer(r'0x([0-9A-Fa-f]{6})\b', text):
            a = int(m.group(1), 16)
            if a in decomp and a not in ours and re.match(r'(sub_|nullsub|unknown_libname)', decomp[a]):
                found.setdefault(a, (text[max(0, m.start() - 50):m.start()].replace(chr(10), ' '), os.path.basename(path)))
    for a, (context, page) in sorted(found.items()):
        print(f'{a:#x} {decomp[a]}  | ...{context} | {page}')
    if not found:
        print('every function the docs mention has a name')


if __name__ == '__main__':
    mode = sys.argv[1]
    if mode == 'unnamed':
        unnamed()
    elif mode == 'name':
        table, starts = functions()
        print(name_of(int(sys.argv[2], 16), table, starts))
    else:
        exe = open(sys.argv[2], 'rb').read()
        if mode == 'dis':
            dis(exe, int(sys.argv[3], 16), int(sys.argv[4], 0))
        elif mode == 'xref':
            xref(exe, int(sys.argv[3], 16))
