"""Collects the built mods into one release .zip that is unpacked into the game's folder (next to speed.exe).

usage: python package_release.py [--version v1.0.0] [--out dist]     (after building: cmake --build Mods/build)

The zip holds what a player needs, in the game's own layout:
  scripts/nfsmw_<mod>.asi            the plugins (mets_controller_support.asi for the controller support)
  scripts/mets_mods/<mod>.ini        each mod's settings (every option, with its default)
  scripts/mets_mods/*.tpk, *.ini     the texture packs (the Options icons, the controller button pictures)
  scripts/mets_mods/translations/    the texts of each mod in other languages
  ReadMe.txt                         how to install it
dev_tools (for people who make mods) and example_options_page are not part of it.
"""
import argparse
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

import set_mods  # the texture packs and the button pictures: the same ones set_mods.py puts in a game

MODS_DIR = Path(__file__).resolve().parent
TOOLS_DIR = MODS_DIR.parent / 'Tools'
BUILD = MODS_DIR / 'build'
RELEASE_MODS = ['custom_menus', 'camera_settings', 'controller_support', 'split_screen_support']
REPO_URL = 'https://github.com/Mets3D/mets_nfsmw_mods'

README = f"""Mets' NFS Most Wanted (2005) mods {{version}}

Install
  1. Needs NFS Most Wanted 2005, version 1.3, and the Ultimate ASI Loader (dinput8.dll; it comes with most widescreen fixes).
  2. Copy the "scripts" folder of this zip into the game's folder, the one with speed.exe. Say yes to merging folders.
     Installing over an older version replaces the .ini files with the defaults: your settings in them are lost.
  3. To remove the mods: delete the nfsmw_*.asi files and mets_controller_support.asi in scripts, and the scripts/mets_mods folder.

What each mod does, what it needs and how to set it up: {REPO_URL}
"""


def build_pack(command, out):
    subprocess.run([sys.executable, *map(str, command)], cwd=MODS_DIR, check=True, stdout=subprocess.DEVNULL)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--version', default='dev', help='goes in the file name and the ReadMe.txt')
    parser.add_argument('--out', default='dist', help='folder for the zip (and the staging folder it is made from)')
    args = parser.parse_args()

    out = Path(args.out)
    stage = out / 'stage'
    shutil.rmtree(stage, ignore_errors=True)
    scripts = stage / 'scripts'
    data = scripts / 'mets_mods'
    data.mkdir(parents=True)

    for mod in RELEASE_MODS:
        asi = BUILD / set_mods.asi_name(mod)
        if not asi.is_file():
            sys.exit(f'{asi} is missing: build the mods first (cmake --build Mods/build)')
        shutil.copy2(asi, scripts / asi.name)
        for ini in (MODS_DIR / mod).glob('*.ini'):
            shutil.copy2(ini, data / ini.name)
        translations = MODS_DIR / mod / 'translations'
        if translations.is_dir():
            shutil.copytree(translations, data / 'translations' / mod)

    for pack in set_mods.PACKS:
        images = sorted(MODS_DIR.glob(pack['pngs']))
        if pack['mod'] in RELEASE_MODS and images:
            tpk = BUILD / f"{pack['name']}.tpk"
            build_pack([TOOLS_DIR / 'tpk_build.py', tpk, pack['name'].upper(), *images], tpk)
            shutil.copy2(tpk, data / tpk.name)
    buttons = BUILD / f'{set_mods.BUTTONS_PACK}.tpk'
    build_pack([TOOLS_DIR / 'button_pack.py', set_mods.BUTTONS_DIR, buttons, set_mods.BUTTONS_PACK.upper()], buttons)
    shutil.copy2(buttons, data / buttons.name)
    shutil.copy2(set_mods.BUTTONS_DIR / 'prompts.ini', data / f'{set_mods.BUTTONS_PACK}.ini')

    (stage / 'ReadMe.txt').write_text(README.format(version=args.version), encoding='utf-8')

    zip_path = out / f'mets_nfsmw_mods_{args.version}.zip'
    zip_path.unlink(missing_ok=True)
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as z:
        for path in sorted(stage.rglob('*')):
            if path.is_file():
                z.write(path, path.relative_to(stage).as_posix())
    shutil.rmtree(stage)
    print(f'{zip_path}: ' + ', '.join(sorted(n for n in zipfile.ZipFile(zip_path).namelist() if n.endswith('.asi'))))


if __name__ == '__main__':
    main()
