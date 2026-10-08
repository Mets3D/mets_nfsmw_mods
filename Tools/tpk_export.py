"""Export the textures of NFS Most Wanted texture packs (TPK chunks) as PNG files, plus an index.html gallery.

usage: python tpk_export.py <out_dir> <file.bun|.lzc>[:PACKNAME,PACKNAME...] ...
       python tpk_export.py ../Exports/fe_textures ../GameTest/GLOBAL/InGameA.bun:INGAMETEXTURES

Writes <out_dir>/<PACKNAME>/<TEXTURE_NAME>_<hash>.png and <out_dir>/index.html. Needs numpy.
Handles DXT1/3/5 (0x22/0x24/0x26), 32-bit BGRA (0x20) and 8-bit paletted (0x08/0x81) textures, top mip only.
"""
import html
import os
import struct
import sys
import zlib

import numpy as np

import tpk_list

TPK_DATA = 0xB3320000
TPK_DATA_ARRAY = 0x33320002


def write_png(path, rgba):
    h, w, _ = rgba.shape
    raw = b''.join(b'\0' + rgba[y].tobytes() for y in range(h))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def rgb565(c):
    c = c.astype(np.uint32)
    r = ((c >> 11) & 31) * 255 // 31
    g = ((c >> 5) & 63) * 255 // 63
    b = (c & 31) * 255 // 31
    return np.stack([r, g, b], -1)


def colour_blocks(blocks, dxt1):
    """blocks: (n, 8) uint8 colour part -> (n, 16, 4) RGBA."""
    c0 = blocks[:, 0].astype(np.uint16) | (blocks[:, 1].astype(np.uint16) << 8)
    c1 = blocks[:, 2].astype(np.uint16) | (blocks[:, 3].astype(np.uint16) << 8)
    p0, p1 = rgb565(c0), rgb565(c1)
    four = (c0 > c1) | (not dxt1)
    p2 = np.where(four[:, None], (2 * p0 + p1) // 3, (p0 + p1) // 2)
    p3 = np.where(four[:, None], (p0 + 2 * p1) // 3, 0)
    pal = np.stack([p0, p1, p2, p3], 1)  # (n, 4, 3)
    alpha = np.full((len(blocks), 4), 255, np.uint32)
    if dxt1:
        alpha[:, 3] = np.where(four, 255, 0)
    bits = blocks[:, 4:8].astype(np.uint32)
    idx = bits[:, 0] | (bits[:, 1] << 8) | (bits[:, 2] << 16) | (bits[:, 3] << 24)
    sel = (idx[:, None] >> (2 * np.arange(16))) & 3  # (n, 16)
    rgb = np.take_along_axis(pal, sel[:, :, None].repeat(3, 2), 1)
    a = np.take_along_axis(alpha, sel, 1)
    return np.concatenate([rgb, a[:, :, None]], 2)


def decode_dxt(buf, w, h, kind):
    bw, bh = max(1, (w + 3) // 4), max(1, (h + 3) // 4)
    bsize = 8 if kind == 1 else 16
    blocks = np.frombuffer(buf, np.uint8, bw * bh * bsize).reshape(-1, bsize)
    if kind == 1:
        px = colour_blocks(blocks, True)
    else:
        px = colour_blocks(blocks[:, 8:], False)
        if kind == 3:
            a = blocks[:, :8].astype(np.uint32)
            nib = np.stack([(a >> 0) & 15, (a >> 4) & 15], 2).reshape(-1, 16)
            px[:, :, 3] = nib * 17
        else:
            a0 = blocks[:, 0].astype(np.int32)
            a1 = blocks[:, 1].astype(np.int32)
            k = np.arange(8)
            eight = (a0[:, None] * (7 - k[1:7]) + a1[:, None] * k[1:7]) // 7
            six = (a0[:, None] * (5 - k[1:5]) + a1[:, None] * k[1:5]) // 5
            pal8 = np.concatenate([a0[:, None], a1[:, None], eight], 1)
            pal6 = np.concatenate([a0[:, None], a1[:, None], six, np.zeros((len(a0), 1), int),
                                   np.full((len(a0), 1), 255)], 1)
            pal = np.where((a0 > a1)[:, None], pal8, pal6)
            bits = np.zeros(len(blocks), np.uint64)
            for i in range(6):
                bits |= blocks[:, 2 + i].astype(np.uint64) << np.uint64(8 * i)
            sel = ((bits[:, None] >> (np.uint64(3) * np.arange(16, dtype=np.uint64))) & np.uint64(7)).astype(int)
            px[:, :, 3] = np.take_along_axis(pal, sel, 1)
    img = px.reshape(bh, bw, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 4)
    return img[:h, :w].astype(np.uint8)


def decode(data, t, base):
    w, h, comp = t['w'], t['h'], t['comp']
    p = base + t['placement']
    if comp in (0x22, 0x24, 0x26):
        return decode_dxt(data[p:p + t['size']], w, h, {0x22: 1, 0x24: 3, 0x26: 5}[comp])
    if comp == 0x20:
        bgra = np.frombuffer(data, np.uint8, w * h * 4, p).reshape(h, w, 4)
        return bgra[:, :, [2, 1, 0, 3]].copy()
    if comp in (0x08, 0x81):
        idx = np.frombuffer(data, np.uint8, w * h, p).reshape(h, w)
        pp = base + t['pal_placement']
        n = max(1, t['pal_entries'])
        pal = np.frombuffer(data, np.uint8, n * 4, pp).reshape(n, 4)[:, [2, 1, 0, 3]]
        return pal[np.minimum(idx, n - 1)]
    return None


def data_base(data, s, e):
    for cid, p, size in tpk_list.chunks(data, s, e):
        if cid != TPK_DATA:
            continue
        for sid, sp, ssize in tpk_list.chunks(data, p, p + size):
            if sid == TPK_DATA_ARRAY:
                return (sp + 0x7F) & ~0x7F
    return None


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    gallery = []
    for arg in sys.argv[2:]:
        path, _, names = arg.partition(':')
        only = set(names.split(',')) if names else None
        data = open(path, 'rb').read()
        for s, e in tpk_list.find_packs(data):
            pack, texs = tpk_list.read_pack(data, s, e)
            if only and pack not in only:
                continue
            base = data_base(data, s, e)
            # palette fields are not in tpk_list's summary; read them here
            for t in texs:
                t.setdefault('pal_placement', 0)
                t.setdefault('pal_entries', 0)
            info = [c for c in tpk_list.chunks(data, s, e) if c[0] == tpk_list.TPK_INFO]
            if info:
                for sid, sp, ssize in tpk_list.chunks(data, info[0][1], info[0][1] + info[0][2]):
                    if sid == tpk_list.TPK_TEXTURES:
                        for i, t in enumerate(texs):
                            q = sp + i * 0x7C
                            t['pal_placement'] = struct.unpack_from('<i', data, q + 0x34)[0]
                            t['pal_entries'] = struct.unpack_from('<h', data, q + 0x4C)[0]
            os.makedirs(os.path.join(out, pack), exist_ok=True)
            items = []
            for t in texs:
                img = decode(data, t, base) if base is not None else None
                if img is None:
                    print(f'  skipped {pack}/{t["name"]} (format {t["comp"]:#x})')
                    continue
                rel = f'{pack}/{t["name"]}_{t["hash"]:08X}.png'
                write_png(os.path.join(out, rel), img)
                items.append((rel, t))
            print(f'{path}: {pack}: {len(items)}/{len(texs)} textures')
            gallery.append((path, pack, items))

    with open(os.path.join(out, 'index.html'), 'w', encoding='utf8') as f:
        f.write('<!doctype html><meta charset="utf-8"><title>NFSMW textures</title><style>'
                'body{background:#222;color:#ddd;font:12px sans-serif;margin:16px}'
                '.g{display:flex;flex-wrap:wrap;gap:10px}'
                '.t{width:170px;text-align:center;word-break:break-all}'
                '.t img{max-width:160px;max-height:160px;background:repeating-conic-gradient(#555 0 25%,#444 0 50%) 0 0/16px 16px}'
                'input{width:300px;padding:4px;margin-bottom:12px}</style>'
                '<input id=q placeholder="filter by name or hash" oninput="for(const e of document.querySelectorAll(\'.t\'))'
                'e.style.display=e.dataset.k.includes(this.value.toUpperCase())?\'\':\'none\'">')
        for path, pack, items in gallery:
            f.write(f'<h2>{html.escape(pack)} <small>({html.escape(os.path.basename(path))}, {len(items)})</small></h2><div class=g>')
            for rel, t in items:
                key = f'{t["name"]} {t["hash"]:08X}'.upper()
                f.write(f'<div class=t data-k="{html.escape(key)}"><img loading=lazy src="{html.escape(rel)}"><br>'
                        f'{html.escape(t["name"])}<br>{t["hash"]:08X} &middot; {t["w"]}x{t["h"]}</div>')
            f.write('</div>')


if __name__ == '__main__':
    main()
