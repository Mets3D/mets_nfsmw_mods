"""List the texture packs (TPK chunks) in an NFS Most Wanted .BUN/.LZC file.

usage: python tpk_list.py <file> [hash ...]
Prints every texture pack with its name and texture count. With hashes (hex), prints which pack holds each one.
"""
import struct
import sys

TPK = 0xB3300000
TPK_INFO = 0xB3310000
TPK_HEADER = 0x33310001
TPK_TEXTURES = 0x33310004


def chunks(d, start, end):
    p = start
    while p + 8 <= end:
        cid, size = struct.unpack_from('<II', d, p)
        yield cid, p + 8, size
        p += 8 + size


def read_pack(d, start, end):
    """Returns (pack name, [TextureInfo dicts])."""
    name, texs = '?', []
    for cid, p, size in chunks(d, start, end):
        if cid != TPK_INFO:
            continue
        for sid, sp, ssize in chunks(d, p, p + size):
            if sid == TPK_HEADER:
                name = d[sp + 4:sp + 0x20].split(b'\0')[0].decode('latin1')
            elif sid == TPK_TEXTURES:
                for i in range(ssize // 0x7C):
                    q = sp + i * 0x7C
                    texs.append({
                        'name': d[q + 0xC:q + 0x24].split(b'\0')[0].decode('latin1'),
                        'hash': struct.unpack_from('<I', d, q + 0x24)[0],
                        'placement': struct.unpack_from('<i', d, q + 0x30)[0],
                        'size': struct.unpack_from('<i', d, q + 0x38)[0],
                        'w': struct.unpack_from('<h', d, q + 0x44)[0],
                        'h': struct.unpack_from('<h', d, q + 0x46)[0],
                        'comp': d[q + 0x4A],
                        'mips': d[q + 0x4E],
                    })
    return name, texs


def find_packs(d):
    """Yields (offset, end) of every top-level TPK chunk, also inside other containers."""
    stack = [(0, len(d))]
    while stack:
        s, e = stack.pop()
        for cid, p, size in chunks(d, s, e):
            if cid == TPK:
                yield p, p + size
            elif cid & 0x80000000 and size > 8 and p + size <= len(d):
                stack.append((p, p + size))


if __name__ == '__main__':
    data = open(sys.argv[1], 'rb').read()
    want = {int(h, 16) for h in sys.argv[2:]}
    for s, e in find_packs(data):
        name, texs = read_pack(data, s, e)
        print(f'{s:#010x} {name:32} {len(texs)} textures')
        for t in texs:
            if t['hash'] in want:
                print(f'    {t["hash"]:08x} {t["name"]} {t["w"]}x{t["h"]} comp={t["comp"]:#x}')
