# Scenarios

A scenario is a text file the game runs by itself, so a program or an AI agent can test it without anyone at the keyboard. They are run by `nfsmw_dev_tools` (see [`../src/automation.cpp`](../src/automation.cpp)).

## Running one

```
python Tools/run_scenario.py --game "Game Versions/Redux v3 + Our Mods" split_race
```

The script copies `scenarios/<name>.txt` to the install's `scripts/mets_mods/scenarios/`, starts `speed.exe` with `NFSMW_SCENARIO=<name>`, and prints the verdict, the screens the game reached, `state` lines, any crash, and the screenshots' paths. It kills the game if the scenario has not ended it by `--timeout` seconds (default 300).

- The install needs `nfsmw_dev_tools.asi` (deploy with `Mods/set_mods.py`) and, for the split screen scenarios, `nfsmw_split_screen_support.asi`.
- The game runs in a 1280x720 window for the run (the widescreen fix's ini is changed and put back), because in exclusive fullscreen the game loses its Direct3D device when another window takes focus. `--fullscreen` leaves the ini alone. `[background]` in `nfsmw_dev_tools.ini` keeps the game running while unfocused.
- `--verbose` prints the whole `nfsmw_dev_tools.log` and the other mods' log sizes.
- The game allows one copy: a game that is already running from the install is closed first, unless it was started by Steam (then the script stops).
- A running game also re-reads its scenario file when it has grown, so lines can be appended to `scripts/mets_mods/scenarios/<name>.txt` while it runs. A scenario with no `quit` leaves the game open for that (`live.txt` is such a scenario).

## Format

One command per line. `#` starts a comment. Arguments are words, or `key=value`. A command that is not understood, or a wait that times out, ends the game with exit code 2 and says why in the log.

| Command | What it does |
|---|---|
| `wait <seconds>` | Waits. |
| `wait_frames <n>` | Waits for n drawn frames. |
| `wait_until <condition> [timeout=<s>]` | Waits for a condition (default timeout 120 s). Conditions: `frontend` (menus are up), `main_menu` (menus, and the boot sequence is over), `racing`, `state=<n>` (the game flow state: 3 menus, 6 race), `screen=<package>` (the screen that has the controls, e.g. `screen=Track_Select.fng`). |
| `press <button> [times]` | A menu button press, held for 100 ms as a real press is: `accept`, `back`, `up`, `down`, `left`, `right`, `start`. The next command waits until the presses are done. |
| `press_start <0-3\|keyboard>` | For the split screen Press Start screens: tells the one that is showing that this device let go of Start (needs `nfsmw_split_screen_support`). |
| `screenshot <name> [width=960]` | Saves the back buffer as `scripts/mets_mods/screenshots/<date>-<time>_<scenario>/<name>.png`, at most `width` pixels wide. A 960 px picture costs an AI about 700 tokens to look at. |
| `state` | Logs the game flow state, the screen, the view mode, the player count and the game mode. |
| `log <text>` | Writes a line to the log. |
| `quit [code]` | Ends the game (exit code 0 by default; the game leaves its main loop like a closed window). |

Every change of screen is logged (`screen: <package> (frame, ms)`) without a command, which is how a scenario or an agent can tell where the menus are without looking at a picture.

## The bundled scenarios

- `boot.txt` starts the game, which loads the profile named by `auto_load_alias` in `nfsmw_dev_tools.ini` and ends in the main menu, takes two screenshots and quits.
- `split_race.txt` goes through the real menus (Main Menu > Quick Race > Split Screen > Circuit > first track > options > Press Start and car for each player) into a two player split screen race, waits ten seconds, logs the state, takes a screenshot and quits. About 36 seconds.
- `live.txt` boots to the main menu and waits for lines to be appended.

## Writing a scenario for another menu path

Step by step: press a button, `wait_until screen=<next package>`, then `wait 1` (a screen ignores presses for a moment while it animates in; the car select needs about 3 s). To find the package names, run `live.txt` and watch the `screen:` lines in `nfsmw_dev_tools.log` while appending presses.

The strips of icons are moved with `left` / `right`, so a scenario depends on the order of the icons (Main Menu: Resume Career, Challenge Series, Quick Race, ...). The auto load needs a saved profile called `auto_load_alias`.

## Limits

- Input is injected at the menus' button table, not at the keyboard: it drives the menus (and Press Start through `nfsmw_split_screen_support`) but not driving; there is no way yet to steer a car.
- The game must be able to draw: a locked screen or a lost device stops screenshots (the log still works).
