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
- The game allows one copy: a game that is already running from the install is closed first, whoever started it.
- A running game also re-reads its scenario file when it has grown, so lines can be appended to `scripts/mets_mods/scenarios/<name>.txt` while it runs. A scenario with no `quit` leaves the game open for that (`live.txt` is such a scenario).

## Format

One command per line. `#` starts a comment. Arguments are words, or `key=value`. A command that is not understood, or a wait that times out, ends the game with exit code 2 and says why in the log.

| Command | What it does |
|---|---|
| `wait <seconds>` | Waits. |
| `wait_frames <n>` | Waits for n drawn frames. |
| `wait_until <condition> [timeout=<s>]` | Waits for a condition (default timeout 120 s). Conditions: `frontend` (menus are up), `main_menu` (menus, and the boot sequence is over), `racing`, `state=<n>` (the game flow state: 3 menus, 6 race), `screen=<package>` (the screen that has the controls, e.g. `screen=Track_Select.fng`). |
| `press <button> [times]` | A menu button press, held for 100 ms as a real press is: `accept`, `back`, `up`, `down`, `left`, `right`, `start`. The next command waits until the presses are done. |
| `measure <seconds>` | Waits that long and logs the frame rate over it (`measure: N frames in M ms`). |
| `profile <seconds>` | Like `measure`, and a thread samples the game thread's instruction pointer every millisecond: the log gets the time by module (the game, d3d9, d3dx9, the driver) and by 256-byte block of the game's code (`Tools/re/re.py name <addr>` says which function), and for time spent outside the game the block of the game's code that called in. The game was 28% d3d9, 23% d3dx9 and 24% its own code in a four player race: the cost is draw calls. |
| `drive <player 2-4> <gas 0..1> [steer -1..1]` | That player's own game input device says that (gas and steering) until `drive <player> off` (needs `mets_controller_support`; player 1 reads every device and cannot be driven this way). |
| `entry <player 2-4> <binding entry> <value or off>` | That player's device says that value for a binding entry (15 camera change, 16 look back, 25-28 look left / right / up / down) until `off`. |
| `pause_request <player 2-4> <1 or 0>` | That player's device holds (1) or lets go of (0) the pause button: the pause menu is theirs. |
| `log_cars` | Logs the cars of the profile's garage (the Quick Race's car list): index, handle (the first word), and the folder in CARS that has the car's name with its size. The `log_cars` scenario does just that. |
| `log_players` | Logs every local player of the split screen mod: car, position, render port (needs `nfsmw_split_screen_support`). |
| `press_start <0-3\|keyboard>` | For the split screen Press Start screens: tells the one that is showing that this device let go of Start (needs `nfsmw_split_screen_support`). |
| `screenshot <name> [width=960]` | Saves the back buffer as `scripts/mets_mods/screenshots/<date>-<time>_<scenario>/<name>.png`, at most `width` pixels wide. A 960 px picture costs an AI about 700 tokens to look at. |
| `state` | Logs the game flow state, the screen, the view mode, the player count and the game mode. |
| `log <text>` | Writes a line to the log. |
| `quit [code]` | Ends the game (exit code 0 by default; the game leaves its main loop like a closed window). |

`wait_until main_menu` also presses Continue (accept) every 3 seconds while the boot flow is not over after the auto load: the load ends in a "Load complete" dialog that waits for it (it is not always there).

Every change of screen is logged (`screen: <package> (frame, ms)`) without a command, which is how a scenario or an agent can tell where the menus are without looking at a picture.

## The bundled scenarios

- `boot.txt` starts the game, which loads the profile named by `auto_load_alias` in `nfsmw_dev_tools.ini` and ends in the main menu, takes two screenshots and quits.
- `single_race.txt` goes through the real menus (Main Menu > Quick Race > the race the game picks) into a race with one player and takes a screenshot: for the HUD of a game that is not split screen. The game picks the race (and its car) at random, and a few of them crash in `CreateCars`: run it with `--retries 1`.
- `split_race.txt` goes through the real menus (Main Menu > Quick Race > Split Screen > Circuit > first track > options > Press Start and car for each player) into a two player split screen race, waits ten seconds, logs the state, takes a screenshot and quits. About 36 seconds.
- `split_race_4.txt` is the same with the **Players** row set to 4 (`more_players=1` in the split screen mod's ini): Press Start and Car Select for four players with four devices, then a race with four cars, the first two screenshots of the countdown, `log_players` twice, a `profile` of ten seconds (the frame rate and where the time goes) and a screenshot. About 80 seconds.
- `split_race_4_drive.txt` is the same, and then players 2, 3 and 4 drive (`drive`), steering different ways: the positions in the log before and after show that each of them has input, and the screenshots the four HUDs while driving. The launcher runs a scenario again once when it fails (`--retries`): a menu is sometimes not where it should be.
- `split_race_3.txt` is the same with the **Players** row set to 3: the second round of Press Start and Car Select is for player 3 alone, then a race with three views (player 1 has the top half); players 2 and 3 drive.
- `split_race_4_look.txt` is the same, and then players 3 and 4 use the camera buttons of their own devices (look, look back, Change Camera View): screenshots before and during.
- `split_race_4_quit.txt` is the same, and then player 3 pauses the race and quits to the menu: the log must have no exception and the screens end at the main menu.
- `split_race_4_options.txt` is the same, and then player 3 pauses, opens Options > Player and changes the camera, Gauges, Free Look Reset, Minimap and HUD Scale: the screenshots before and after show only player 3's picture and HUD changed, the log (`log_players`) each player's camera view, and the .ini files (`[player_3]`, `[look_player_3]`) what was kept (it writes them: remove those sections after a run).
- `live.txt` boots to the main menu and waits for lines to be appended.

## Writing a scenario for another menu path

Step by step: press a button, `wait_until screen=<next package>`, then `wait 1` (a screen ignores presses for a moment while it animates in; the car select needs about 3 s). To find the package names, run `live.txt` and watch the `screen:` lines in `nfsmw_dev_tools.log` while appending presses.

The strips of icons are moved with `left` / `right`, so a scenario depends on the order of the icons (Main Menu: Resume Career, Challenge Series, Quick Race, ...). The auto load needs a saved profile called `auto_load_alias`.

## Limits

- Input is injected at the menus' button table, not at the keyboard: it drives the menus (and Press Start through `nfsmw_split_screen_support`) but not driving; there is no way yet to steer a car.
- The game must be able to draw: a locked screen or a lost device stops screenshots (the log still works).
