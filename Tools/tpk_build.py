"""Build a standalone NFS Most Wanted texture pack (.tpk) from PNG files.

usage: python tpk_build.py <out.tpk> <PACK_NAME> <image.png> [image.png ...]

Each PNG becomes one texture named after its file name in upper case (OPTIONS_CAMERA.png -> OPTIONS_CAMERA), stored
as uncompressed 32-bit BGRA with one mip level, like the game's own menu icons. The texture's hash is the game's
bHash of that name (h = 0xFFFFFFFF; h = h * 33 + c), which is what the game and the mods look it up by. A pack stores
at most 23 characters of a name; a longer name is cut there, but the hash stays the one of the full name.

The chunk layout copies the game's own packs (and the standalone ones other mods ship): padding chunks keep the data
chunk and every texture at 0x80-byte boundaries, because the game reads texture data from the first 0x80-aligned
address inside the data chunk.
"""
import os
import struct
import sys
import zlib


def bhash(s):
    h = 0xFFFFFFFF
    for c in s.encode('latin1'):
        h = (h * 33 + c) & 0xFFFFFFFF
    return h


def read_png(path):
    """Returns (width, height, RGBA bytes). 8-bit grey, grey+alpha, RGB and RGBA, non-interlaced."""
    d = open(path, 'rb').read()
    if d[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path}: not a PNG')
    pos, idat = 8, b''
    while pos < len(d):
        n, kind = struct.unpack('>I4s', d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b'IHDR':
            w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', body)
        elif kind == b'IDAT':
            idat += body
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(ctype)
    if depth != 8 or channels is None or interlace:
        raise ValueError(f'{path}: needs an 8-bit, non-interlaced PNG (got depth {depth}, colour type {ctype})')
    raw = zlib.decompress(idat)
    stride = w * channels
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    rgba = bytearray(w * h * 4)
    for i in range(w * h):
        px = out[i * channels:(i + 1) * channels]
        if channels == 1:
            r = g = b = px[0]; a = 255
        elif channels == 2:
            r = g = b = px[0]; a = px[1]
        elif channels == 3:
            r, g, b = px; a = 255
        else:
            r, g, b, a = px
        rgba[i * 4:i * 4 + 4] = bytes((r, g, b, a))
    return w, h, bytes(rgba)


def chunk(cid, body):
    return struct.pack('<II', cid, len(body)) + body


def pad_to(offset, align):
    """A padding chunk (id 0) that moves the next chunk from `offset` to the next `align` boundary."""
    n = (-(offset + 8)) % align
    return struct.pack('<II', 0, n) + bytes(n)


def build(out_path, pack_name, pngs):
    """pngs: paths, or (texture name, path) pairs."""
    filename = pack_name + '.tpk'
    file_hash = bhash(filename)
    textures = []
    for item in pngs:
        # a PNG named after its file, or (texture name, PNG) when the texture is to be named otherwise
        path = item[1] if isinstance(item, tuple) else item
        full_name = (item[0] if isinstance(item, tuple) else os.path.splitext(os.path.basename(path))[0]).upper()
        name = full_name[:23]  # the pack stores 23 characters; the hash (what the game looks it up by) is of the full name
        w, h, rgba = read_png(path)
        if w & (w - 1) or h & (h - 1):
            raise ValueError(f'{path}: width and height must be powers of two (got {w}x{h})')
        bgra = bytearray(rgba)
        bgra[0::4], bgra[2::4] = rgba[2::4], rgba[0::4]
        textures.append({'name': name, 'hash': bhash(full_name), 'w': w, 'h': h, 'data': bytes(bgra)})
    textures.sort(key=lambda t: t['hash'])

    data = bytearray()
    for t in textures:
        t['placement'] = len(data)
        data += t['data'] + bytes((-len(t['data'])) % 0x80)

    header = struct.pack('<I28s64sI', 5, pack_name.encode('latin1'), filename.encode('latin1'), file_hash) + bytes(0x18)
    keys = b''.join(struct.pack('<II', t['hash'], 0) for t in textures)
    infos = comps = b''
    for t in textures:
        infos += (bytes(0xC) + t['name'].encode('latin1').ljust(24, b'\0') +
                  struct.pack('<IIIiiiii', t['hash'], 0x001A93CF, 0, t['placement'], -1, len(t['data']), 0, len(t['data'])) +
                  struct.pack('<hhbbBBhbbbbbbbbbb', t['w'], t['h'], t['w'].bit_length() - 1, t['h'].bit_length() - 1,
                              0x20, 0, 0, 1, 0, 0, 5, 0, 0, 0, 2, 1, 0) +  # 32-bit, 1 mip, modulated alpha, blend
                  struct.pack('<hhhhhhh', 0x100, 0, 0, 0, 0, 0x100, 0x100) + bytes(0x16))
        comps += struct.pack('<IIIIIIII', 0, 0, 1, 5, 6, 21, 0, 0)  # 21 = D3DFMT_A8R8G8B8
    info = (chunk(0x33310001, header) + chunk(0x33310002, keys) + chunk(0x33310004, infos) + chunk(0x33310005, comps))
    assert len(infos) == 0x7C * len(textures)

    out = bytearray()
    body = bytearray(pad_to(8, 0x40))  # the info chunk starts at file offset 0x40
    body += chunk(0xB3310000, info)
    body += pad_to(8 + len(body), 0x80)  # the data chunk starts on a 0x80 boundary
    vram = bytearray(chunk(0x33320001, struct.pack('<IIIIII', 0, 0, 1, file_hash, 0, 0)))
    vram += pad_to(8 + len(body) + 8 + len(vram), 0x80)
    vram += chunk(0x33320002, bytes(0x78) + data)  # texture data begins 0x80 after this chunk's header
    body += chunk(0xB3320000, bytes(vram))
    out += chunk(0xB3300000, bytes(body))
    open(out_path, 'wb').write(out)
    for t in textures:
        print(f'{out_path}: {t["name"]} {t["w"]}x{t["h"]} hash {t["hash"]:08X}')


if __name__ == '__main__':
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    build(sys.argv[1], sys.argv[2], sys.argv[3:])
