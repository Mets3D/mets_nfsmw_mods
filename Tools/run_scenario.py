"""Runs a game scenario unattended and reports what happened.

usage: python Tools/run_scenario.py --game <install dir> <scenario> [--timeout 300] [--verbose] [--fullscreen]

The scenario is Mods/dev_tools/scenarios/<scenario>.txt (commands: see Mods/dev_tools/src/automation.cpp). It is copied to the
install's scripts/mets_mods/scenarios/, the game is started with NFSMW_SCENARIO=<scenario>, and killed when the scenario has not
ended it by --timeout seconds. Needs nfsmw_dev_tools.asi in the install (deploy with Mods/set_mods.py first). A speed.exe of
the same install that is already running is closed first (the user allowed it, even their own game); one of another install makes this stop (the game allows one copy).
The game runs in a 1280x720 window (the widescreen fix's WindowedMode / ResX / ResY are set for the run and put back after it): in
exclusive fullscreen the game loses its Direct3D device whenever another window takes focus, and screenshots then fail. --fullscreen keeps the ini.
Prints the exit code, the screens reached, `state` lines and crashes from nfsmw_dev_tools.log (all of it with --verbose), and the
screenshots' full paths.
"""
import argparse
import re
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

SCENARIOS = Path(__file__).resolve().parent.parent / 'Mods' / 'dev_tools' / 'scenarios'


def running_games():
    """[(pid, exe path, name of the parent process)] of every speed.exe."""
    out = subprocess.run(['powershell', '-NoProfile', '-Command',
                          "Get-CimInstance Win32_Process -Filter \"Name='speed.exe'\" | ForEach-Object { "
                          "$p = Get-CimInstance Win32_Process -Filter \"ProcessId=$($_.ParentProcessId)\"; "
                          "\"$($_.ProcessId)|$($_.ExecutablePath)|$($p.Name)\" }"],
                         capture_output=True, text=True).stdout
    return [line.strip().split('|', 2) for line in out.splitlines() if line.count('|') == 2]


def running_game_paths():
    return [(pid, path) for pid, path, _ in running_games()]


def kill(pid):
    """taskkill, then WMI's Terminate, which also ends a game stuck in ExitProcess (the crash handler leaves one behind)."""
    subprocess.run(['taskkill', '/F', '/T', '/PID', str(pid)], capture_output=True)
    subprocess.run(['powershell', '-NoProfile', '-Command',
                    f"Get-CimInstance Win32_Process -Filter \"ProcessId={pid}\" | Invoke-CimMethod -MethodName Terminate"], capture_output=True)
    for _ in range(20):
        if not any(int(p) == int(pid) for p, _ in running_game_paths()):
            return True
        time.sleep(0.5)
    return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('scenario')
    parser.add_argument('--game', required=True)
    parser.add_argument('--timeout', type=float, default=300)
    parser.add_argument('--fullscreen', action='store_true', help='leave the display settings alone')
    parser.add_argument('--verbose', action='store_true', help='the whole log and the other logs sizes')
    args = parser.parse_args()

    game = Path(args.game).resolve()
    exe = game / 'speed.exe'
    source = SCENARIOS / f'{args.scenario}.txt'
    if not exe.exists() or not source.exists():
        sys.exit(f'missing {exe if not exe.exists() else source}')
    if not (game / 'scripts' / 'nfsmw_dev_tools.asi').exists():
        sys.exit('nfsmw_dev_tools.asi is not in the install: deploy it with Mods/set_mods.py first')

    for pid, path, parent in running_games():
        if Path(path).resolve() != exe:
            sys.exit(f'speed.exe {pid} from {path} is running; the game allows one copy')
        if not kill(pid):
            sys.exit(f'speed.exe {pid} of this install is running and could not be closed')

    data = game / 'scripts' / 'mets_mods'
    (data / 'scenarios').mkdir(parents=True, exist_ok=True)
    shutil.copy(source, data / 'scenarios' / source.name)
    shots = data / 'screenshots'
    log = data / 'nfsmw_dev_tools.log'

    env = dict(os.environ, NFSMW_SCENARIO=args.scenario)
    ws_ini = game / 'scripts' / 'NFSMostWanted.WidescreenFix.ini'
    ws_original = ws_ini.read_bytes() if ws_ini.exists() and not args.fullscreen else None
    if ws_original is not None:
        text = ws_original.decode('latin-1')
        for key, value in (('WindowedMode', 2), ('ResX', 1280), ('ResY', 720)):
            text = re.sub(rf'(?m)^({key}\s*=\s*)\d+', rf'\g<1>{value}', text)
        ws_ini.write_bytes(text.encode('latin-1'))
    started = time.time()
    process = subprocess.Popen([str(exe)], cwd=game, env=env)
    try:
        code = process.wait(timeout=args.timeout)
        verdict = f'the game ended by itself with exit code {code}'
    except subprocess.TimeoutExpired:
        kill(process.pid)
        code = None
        verdict = f'TIMEOUT after {args.timeout:.0f} s: the game was killed'
    finally:
        if ws_original is not None:
            ws_ini.write_bytes(ws_original)
    print(f'{verdict} ({time.time() - started:.0f} s)')

    keep = ('screen:', 'state:', 'EXCEPTION', 'ACCESS VIOLATION', 'automation: quit', 'automation: the game did not', 'automation: no frame', 'NOT ')
    lines = log.read_text(errors='replace').splitlines() if log.exists() else ['(no log)']
    print(f'--- {log.name}')
    print(chr(10).join(l for l in lines if args.verbose or l.startswith(keep) or 'timed out' in l))
    if args.verbose:
        print('--- other logs')
        for other in sorted(data.glob('*.log')):
            if other.name != log.name and '.prev.' not in other.name and other.stat().st_mtime >= started:
                print(f'{other.name}: {other.stat().st_size} bytes')
    print('--- screenshots')
    for png in sorted(shots.glob('*/*.png')) if shots.exists() else []:
        if png.stat().st_mtime >= started:
            print(png)
    sys.exit(0 if code == 0 else 1)


if __name__ == '__main__':
    main()
