"""Switch which .asi plugins are active in a copy of the game (cross-platform port of set_mods.ps1).

usage: python set_mods.py --mode all|ours|none --game <dir> [--only mod ...]
  all  = your original mod set only
  ours = original mod set + our mods: custom_menus (the Options menu framework), camera_settings and
         controller_support, with their texture packs
  none = no plugins at all
--game is the game install to act on (no default: always pass it), and --only which of our mods go there
(folder names of Mods/, default the first three; dev_tools and split_screen_support are only added when asked for):
  python set_mods.py --mode ours --game "../Game Versions/SomeMods" --only controller_support
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

MODS_DIR = Path(__file__).resolve().parent
ALL_MODS = ['custom_menus', 'camera_settings', 'controller_support', 'dev_tools', 'split_screen_support']
ALL_ASIS = [f'nfsmw_{m}.asi' for m in ALL_MODS]
# Texture packs: <name>.tpk in the data folder, built from the PNGs of a folder in Mods/ (the texture's name is the file's)
PACKS = [
    {'mod': 'camera_settings', 'name': 'nfsmw_camera_settings', 'pngs': 'camera_settings/OPTIONS_*.png'},  # the Options > Camera icon
    {'mod': 'controller_support', 'name': 'nfsmw_controller_support', 'pngs': 'controller_support/OPTIONS_*.png'},  # the Options > Rumble icon
]
BUTTONS_DIR = MODS_DIR / 'controller_support' / 'buttons'
BUTTONS_PACK = 'nfsmw_controller_buttons'


def run_last_line(cmd):
    result = subprocess.run(cmd, cwd=MODS_DIR, check=True, capture_output=True, text=True)
    lines = [l for l in result.stdout.splitlines() if l.strip()]
    if lines:
        print(lines[-1])


def move_force(src, dst_dir):
    dst = dst_dir / src.name
    dst.unlink(missing_ok=True)
    shutil.move(str(src), str(dst))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--mode', required=True, choices=['all', 'ours', 'none'])
    parser.add_argument('--game', required=True, help='path to the game install to act on')
    parser.add_argument('--only', nargs='+', choices=ALL_MODS, default=['custom_menus', 'camera_settings', 'controller_support'])
    args = parser.parse_args()

    game = Path(args.game)
    if not game.is_dir():
        siblings = sorted(p.name for p in (MODS_DIR.parent / 'Game Versions').glob('*') if p.is_dir()) \
            if (MODS_DIR.parent / 'Game Versions').is_dir() else []
        sys.exit(f"--game not found: {game}\nAvailable under 'Game Versions': {', '.join(siblings) or '(none)'}")

    on = game / 'scripts'
    off = game / 'scripts_disabled'
    data = on / 'mets_mods'  # settings, logs and texture packs of our mods
    data.mkdir(parents=True, exist_ok=True)
    shutil.rmtree(game / 'nfsmw_mods', ignore_errors=True)  # where the previous layout kept them

    ours = [n for n in ALL_ASIS if n[len('nfsmw_'):-len('.asi')] in args.only]

    off.mkdir(parents=True, exist_ok=True)
    for asi in on.glob('*.asi'):
        move_force(asi, off)
    if args.mode in ('all', 'ours'):
        for asi in off.glob('*.asi'):
            if asi.name not in ALL_ASIS:
                move_force(asi, on)

    if args.mode == 'ours':
        for n in ours:
            shutil.copy2(MODS_DIR / 'build' / n, on / n)
        for pack in PACKS:
            name = pack['name']
            images = sorted(MODS_DIR.glob(pack['pngs']))
            if pack['mod'] in args.only and images:
                run_last_line([sys.executable, str(MODS_DIR.parent / 'Tools' / 'tpk_build.py'),
                                str(MODS_DIR / 'build' / f'{name}.tpk'), name.upper(), *map(str, images)])
                shutil.copy2(MODS_DIR / 'build' / f'{name}.tpk', data / f'{name}.tpk')
            else:
                (data / f'{name}.tpk').unlink(missing_ok=True)
        if 'controller_support' in args.only:
            run_last_line([sys.executable, str(MODS_DIR.parent / 'Tools' / 'button_pack.py'),
                            str(BUTTONS_DIR), str(MODS_DIR / 'build' / f'{BUTTONS_PACK}.tpk'), BUTTONS_PACK.upper()])
            shutil.copy2(MODS_DIR / 'build' / f'{BUTTONS_PACK}.tpk', data / f'{BUTTONS_PACK}.tpk')
            shutil.copy2(BUTTONS_DIR / 'prompts.ini', data / f'{BUTTONS_PACK}.ini')
        else:
            (data / f'{BUTTONS_PACK}.tpk').unlink(missing_ok=True)
            (data / f'{BUTTONS_PACK}.ini').unlink(missing_ok=True)
    else:
        for pack in PACKS:
            (data / f"{pack['name']}.tpk").unlink(missing_ok=True)
        (data / f'{BUTTONS_PACK}.tpk').unlink(missing_ok=True)
        (data / f'{BUTTONS_PACK}.ini').unlink(missing_ok=True)

    for pack in PACKS:
        (on / f"{pack['name']}.tpk").unlink(missing_ok=True)  # where older versions put them
    for n in ALL_ASIS:
        (off / n).unlink(missing_ok=True)

    active = sorted(p.name for p in on.glob('*.asi'))
    print(f"{args.mode}: active = {', '.join(active)}")


if __name__ == '__main__':
    main()
