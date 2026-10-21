# Improved AI Opponents

This mod for Need for Speed: Most Wanted 2005 (1.3) aims to implement extremely good AI that doesn't cheat. This can be best verified by playing against the AI in split-screen mode.

This mod is currently WIP, and the ReadMe below is AI generated, so it's a bunch of bullshit about the current state of this very early WIP state rather than about the final vision. To be updated in coming days.

## How to use

1. Needs the [Split Screen Support](../split_screen_support/ReadMe.md) mod to be installed: it gives this mod its place in every frame and in each player's picture.
2. In a race, open the pause menu and choose **Options > AI Driving** (needs [Custom Menus](../custom_menus/ReadMe.md)). Each player has their own settings: **AI Control** (Off / Vanilla / Improved), **Skill Level** (0-100), **Nitro** (Disabled / Fair / Allowed), **Shortcuts** (Avoid / Allowed / Always), **Catch-Up** and **Debug Overlays**. In a split screen race, player 2 is AI driven from the start.
3. For quick tests: **F7** hands player 1's car to the improved AI and back, **F9** makes the AIs plan their route again from where the car is, **F8** shows the road network around the car.
4. The defaults and every setting are in the [.ini](nfsmw_improved_ai_opponents.ini).

## Features
- Each player's car can be driven by the game's own racing AI ("vanilla") or by the improved AI, to compare them side by side.
- The improved AI uses nitrous (**Fair**: as far as the human players have it, as the game's own opponents do; **Allowed**: whatever the others have), gets out of a stuck position twice as fast, weighs the width of the roads and the turns its car can make when it plans its route, and may plan shortcuts (**Avoid**: never; **Always**: prefers them).
- **Skill Level** and **Catch-Up** (the game's own catch-up: the AI drives better and accelerates harder when it is behind) work for both the vanilla and the improved AI. **Nitro** and **Shortcuts** are for the improved AI only: the vanilla AI races as the game's opponents do (nitrous as far as the human players have it, the game's own shortcut rule).
- With **Debug Overlays** on, the improved AI plans its route beside your car while you drive, so you can see what it would do.
- Lines in each player's picture: the route the AI planned (cyan), the corridor it steers in, its steering target, the roads it did not take, the other roads near the car as orange corridors, and with **F8** every road node and link around the car. They are hidden behind walls. Can be turned off in the [.ini](nfsmw_improved_ai_opponents.ini) (`[overlay]`).

## Mod Compatibility

- See [General Compatibility](../../ReadMe.md#general-compatibility).
- Required: [Split Screen Support](../split_screen_support/ReadMe.md).
- Conditionally compatible: NFSMWExtraOptions has its own F6 AI: Don't use both on one car.

## TODO
- Separate options for the overlays (route, corridor, road network).
- Create tools to modify road data in order to fix various issues:
    - Some road nodes are a clusterfuck, eg. the area of the map with pillars.
    - Some roads lack nodes, like the left-side of a road just before the pillars in the first circuit race.
- Forcing AI to always take shortcuts is not the most optimal - sometimes routes marked as shortcuts force harsh braking or similar sub-optimal strategy. Needs testing on slow vs fast cars though.
- Allow Split Screen race to be started with only 1 input device, where the 2nd player is AI-controlled.
- Allow using the new AI for regular race opponents in single-player.
- Make Split Screen mod not a requirement.

### AI Disclaimer

The code and this page were written by Claude (Sonnet & Opus). Testing and design were hand-made.
