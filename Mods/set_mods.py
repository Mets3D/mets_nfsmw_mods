"""Switch which .asi plugins are active in a copy of the game (cross-platform port of set_mods.ps1).

usage: python set_mods.py --mode all|ours|none --game <dir> [--only mod ...]
  all  = your original mod set only
  ours = original mod set + our mods: custom_menus (the Options menu framework), camera_settings and
         controller_support, with their texture packs
  none = no plugins at all
Each mod's .ini is copied to scripts/mets_mods/ when the install has none yet (an existing one keeps the settings being tested).
Each mod's translations/ folder is copied to scripts/mets_mods/translations/<mod> every time (as in a release).
--game is the game install to act on (no default: always pass it); a game running from that install is closed first (its .asi
files are loaded from disk while it runs, and a changed one crashes it), unless --keep-running is given. --only says which of our
mods go there
(folder names of Mods/, default the first three; dev_tools, split_screen_support, improved_ai_opponents and example_options_page (the
example for mod makers, in custom_menus/; needs custom_menus) are only added when asked for):
  python set_mods.py --mode ours --game "<path to the game install>" --only controller_support
"""
import argparse
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

MODS_DIR = Path(__file__).resolve().parent
ALL_MODS = ['custom_menus', 'camera_settings', 'controller_support', 'dev_tools', 'split_screen_support', 'improved_ai_opponents',
            'example_options_page']
MOD_DIRS = {'example_options_page': MODS_DIR / 'custom_menus' / 'example_options_page'}  # where a mod's folder is, when it is not Mods/<mod>


def mod_dir(mod):
    return MOD_DIRS.get(mod, MODS_DIR / mod)
# The file name of a mod's plugin, when it is not nfsmw_<mod>.asi. The loader loads a folder's plugins alphabetically, and the
# controller support must load before the widescreen fix ("NFSMostWanted...") to turn off one of its options before it is read.
ASI_NAMES = {'controller_support': 'mets_controller_support.asi'}
OLD_ASIS = ['nfsmw_controller_support.asi', '!nfsmw_controller_support_early.asi', 'nfsmw_ai_driving.asi']  # names it had before: removed from the install


def asi_name(mod):
    return ASI_NAMES.get(mod, f'nfsmw_{mod}.asi')


ALL_ASIS = [asi_name(m) for m in ALL_MODS]
# Texture packs: <name>.tpk in the data folder, built from the PNGs of a folder in Mods/ (the texture's name is the file's)
PACKS = [
    {'mod': 'custom_menus', 'name': 'nfsmw_custom_menus', 'pngs': 'custom_menus/NFSMW_*.png'},  # the live bar under a slider (a flat green)
    {'mod': 'camera_settings', 'name': 'nfsmw_camera_settings', 'pngs': 'camera_settings/OPTIONS_*.png'},  # the Options > Camera icon
    {'mod': 'controller_support', 'name': 'mets_controller_support', 'pngs': 'controller_support/OPTIONS_*.png'},  # the Options > Rumble icon
]
BUTTONS_DIR = MODS_DIR / 'controller_support' / 'buttons'
BUTTONS_PACK = 'nfsmw_controller_buttons'


def run_last_line(cmd):
    result = subprocess.run(cmd, cwd=MODS_DIR, check=True, capture_output=True, text=True)
    lines = [l for l in result.stdout.splitlines() if l.strip()]
    if lines:
        print(lines[-1])


def running_game_pids(game):
    """Process ids of a speed.exe started from this install (Wine under Linux: its command line is Z:\\path\\speed.exe)."""
    exe = (game.resolve() / 'speed.exe')
    if sys.platform == 'win32':
        script = f"Get-CimInstance Win32_Process -Filter \"Name='speed.exe'\" | Where-Object {{ $_.ExecutablePath -eq '{exe}' }} | ForEach-Object {{ $_.ProcessId }}"
        out = subprocess.run(['powershell', '-NoProfile', '-Command', script], capture_output=True, text=True).stdout
        return [int(w) for w in out.split() if w.isdigit()]
    wanted = 'z:' + str(exe).replace('/', '\\').lower()
    pids = []
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit():
            continue
        try:
            argv0 = (entry / 'cmdline').read_bytes().split(b'\0')[0].decode('utf-8', 'replace')
        except OSError:
            continue
        if argv0.lower() == wanted:
            pids.append(int(entry.name))
    return pids


def stop_game(game):
    """Closes the game running from this install (it would crash by itself when its .asi files change)."""
    pids = running_game_pids(game)
    if not pids:
        return
    print(f"closing the running game (pid {', '.join(map(str, pids))}); use --keep-running to leave it")
    for pid in pids:
        if sys.platform == 'win32':
            subprocess.run(['taskkill', '/F', '/PID', str(pid)], capture_output=True)
        else:
            os.kill(pid, signal.SIGTERM)
    deadline = time.time() + 10
    while running_game_pids(game) and time.time() < deadline:
        time.sleep(0.2)
    for pid in running_game_pids(game):
        if sys.platform == 'win32':  # a hung game that taskkill cannot end
            subprocess.run(['powershell', '-NoProfile', '-Command',
                            f"Get-CimInstance Win32_Process -Filter \"ProcessId={pid}\" | Invoke-CimMethod -MethodName Terminate"],
                           capture_output=True)
        else:
            os.kill(pid, signal.SIGKILL)
    time.sleep(1)  # the loader and the files let go


def copy_replace(src, dst):
    """Copies src over dst as a new file: a game that is running keeps the old one (overwriting a mapped .asi in place crashes it under Wine)."""
    dst = Path(dst)
    dst.unlink(missing_ok=True)
    shutil.copy2(src, dst)


def move_force(src, dst_dir):
    dst = dst_dir / src.name
    dst.unlink(missing_ok=True)
    shutil.move(str(src), str(dst))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--mode', required=True, choices=['all', 'ours', 'none'])
    parser.add_argument('--game', required=True, help='path to the game install to act on')
    parser.add_argument('--keep-running', action='store_true', help='do not close a game running from this install')
    parser.add_argument('--only', nargs='+', choices=ALL_MODS, default=['custom_menus', 'camera_settings', 'controller_support'])
    args = parser.parse_args()

    game = Path(args.game)
    if not game.is_dir():
        siblings = sorted(p.name for p in (MODS_DIR.parent / 'Game Versions').glob('*') if p.is_dir()) \
            if (MODS_DIR.parent / 'Game Versions').is_dir() else []
        sys.exit(f"--game not found: {game}\nAvailable under 'Game Versions': {', '.join(siblings) or '(none)'}")

    if not args.keep_running:
        stop_game(game)

    on = game / 'scripts'
    off = game / 'scripts_disabled'
    data = on / 'mets_mods'  # settings, logs and texture packs of our mods
    data.mkdir(parents=True, exist_ok=True)
    shutil.rmtree(game / 'nfsmw_mods', ignore_errors=True)  # where the previous layout kept them

    ours = [asi_name(m) for m in ALL_MODS if m in args.only]

    shutil.rmtree(data / 'translations', ignore_errors=True)  # always re-copied, so an edited text shows up in the next start
    if args.mode == 'ours':
        for mod in args.only:
            translations = mod_dir(mod) / 'translations'
            if translations.is_dir():
                shutil.copytree(translations, data / 'translations' / mod)

    off.mkdir(parents=True, exist_ok=True)
    for n in OLD_ASIS:
        (on / n).unlink(missing_ok=True)
    for asi in on.glob('*.asi'):
        move_force(asi, off)
    if args.mode in ('all', 'ours'):
        for asi in off.glob('*.asi'):
            if asi.name not in ALL_ASIS:
                move_force(asi, on)

    if args.mode == 'ours':
        for n in ours:
            copy_replace(MODS_DIR / 'build' / n, on / n)
        for pack in PACKS:
            name = pack['name']
            images = sorted(MODS_DIR.glob(pack['pngs']))
            if pack['mod'] in args.only and images:
                run_last_line([sys.executable, str(MODS_DIR.parent / 'Tools' / 'tpk_build.py'),
                                str(MODS_DIR / 'build' / f'{name}.tpk'), name.upper(), *map(str, images)])
                copy_replace(MODS_DIR / 'build' / f'{name}.tpk', data / f'{name}.tpk')
            else:
                (data / f'{name}.tpk').unlink(missing_ok=True)
        for mod in args.only:  # a mod's settings file, only when the install has none yet (the ones there hold the settings being tested)
            for ini in mod_dir(mod).glob('*.ini'):
                if not (data / ini.name).exists():
                    shutil.copy2(ini, data / ini.name)
                    print(f'copied {ini.name} to {data}')
        if 'controller_support' in args.only:
            run_last_line([sys.executable, str(MODS_DIR.parent / 'Tools' / 'button_pack.py'),
                            str(BUTTONS_DIR), str(MODS_DIR / 'build' / f'{BUTTONS_PACK}.tpk'), BUTTONS_PACK.upper()])
            copy_replace(MODS_DIR / 'build' / f'{BUTTONS_PACK}.tpk', data / f'{BUTTONS_PACK}.tpk')
            copy_replace(BUTTONS_DIR / 'prompts.ini', data / f'{BUTTONS_PACK}.ini')
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
    for n in ALL_ASIS + OLD_ASIS:
        (off / n).unlink(missing_ok=True)

    active = sorted(p.name for p in on.glob('*.asi'))
    print(f"{args.mode}: active = {', '.join(active)}")


if __name__ == '__main__':
    main()
