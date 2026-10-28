"""Build the controller button pictures into a texture pack (.tpk) for mets_controller_support.

usage: python button_pack.py <buttons folder> <out.tpk> <PACK_NAME>

The folder has a prompts.ini ([xbox], [playstation], [switch] and [switch_nintendo_layout]: the game's prompt picture = the picture to show instead, a PNG
of that name in the folder xbox/, playstation/ or switch/ (a section's folder is its name up to the first '_')) and the PNGs themselves, named after what they show (A.png, Cross.png).
Each prompt gets its own texture, named <STYLE>_<prompt> (XBOX_PC_KEY_ENTER), made from its picture: a picture several
prompts share (B for Back, its French twin and Quit) is stored once per prompt, so that the plugin can tell the prompts
apart again when it switches back to the keyboard keys. The plugin derives the same names from prompts.ini.
"""
import os
import sys

import tpk_build


def read_prompts(path):
    """{style: [(prompt, picture), ...]} from prompts.ini (';' starts a comment, also after a value)."""
    styles, current = {}, None
    for line in open(path, encoding='utf-8'):
        line = line.split(';', 1)[0].strip()
        if not line:
            continue
        if line.startswith('[') and line.endswith(']'):
            current = line[1:-1].strip().lower()
            styles[current] = []
        elif '=' in line and current is not None:
            prompt, picture = (part.strip() for part in line.split('=', 1))
            styles[current].append((prompt, picture))
    return styles


def main(folder, out, pack_name):
    styles = read_prompts(os.path.join(folder, 'prompts.ini'))
    textures, used = [], set()
    for style, prompts in styles.items():
        for prompt, picture in prompts:
            png = os.path.join(folder, style.split('_')[0], picture + '.png')
            if not os.path.exists(png):
                sys.exit(f'prompts.ini [{style}] {prompt}: there is no {style.split("_")[0]}/{picture}.png')
            used.add(os.path.normpath(png))
            textures.append((f'{style}_{prompt}', png))
    for style in sorted({s.split('_')[0] for s in styles}):
        for name in sorted(os.listdir(os.path.join(folder, style))):
            if name.lower().endswith('.png') and os.path.normpath(os.path.join(folder, style, name)) not in used:
                print(f'note: {style}/{name} is not used by any prompt')
    tpk_build.build(out, pack_name, textures)


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
