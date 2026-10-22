# Improved AI Opponents

This mod for Need for Speed: Most Wanted 2005 (1.3) aims to implement extremely good AI that doesn't cheat. This was thoroughly tested and verified by playing against the AI in split-screen mode, where it's easy to see what the AI is doing.

## Features
This mod is currently WIP, so it can't be used in Career mode yet, only in Split Screen.

- You can race against the Improved AI in Split Screen mode.
- TODO: You can race against the Improved AI in career mode.

## AI
- **Super Steering (TODO)**: The vanilla AI would ignore the handling capabilities of the controlled car, allowing it to take arbitrarily sharp turns at arbitrary speeds. The Improved AI uses the same steering code as the player.
- **Friendly Nitrous (TODO)**: The vanilla AI would only use nitrous if the player also had nitrous available in their car, and it would only use the same quality nitrous. In the Improved AI, this behaviour can be toggled off, allowing the AI to use nitrous even if the player's car has none.
- **Friendly Performance (TODO)**: The vanilla AI would limit its car's performance to not exceed the player's car's performance. In the Improved AI, this behaviour can be toggled off, allowing the AI to use whatever performance its car would normally have. Turning this option off may result in completely unfair races, this was not yet tested in career mode.
- **Catch-Up**: Also known as rubber-banding, this well-known mechanic would cause the vanilla AI to gain additional performance when falling behind the player. This mod allows turning this mechanic off for both the Vanilla and the Improved AI. If this is turned off, it is recommended to also turn off Friendly Performance.
- **Pathfinding (WIP)**: The vanilla AI would always pick the shortest path, which is often a sub-optimal path with unnecessary sharp turns in it. The vanilla AI would also make unwise decisions about taking shortcuts that came down to dice rolls involving a skill level value, with the assumption that all shortcuts are good, which is far from the truth in this game. The improved AI calculates a time cost for each road segment in the race based on that road segment's curvature, lane quality, and width, then picks the fastest route. It has no concept of shortcuts, and no dice rolls. It just takes the fastest route.
- **Collision recovery (WIP)**: After a full on collision with a wall or traffic, the vanilla AI will throttle forward for 3 full seconds before deciding to enter its reversing routine to try to right itself. The Improved AI does this after 1.5 seconds instead.

## Debug tools
While developing this mod, I implemented some tools to better understand the game's road and pathfinding systems. These systems are intended to be used in combination with the free fly mode found in [Dev Tools](../dev_tools/ReadMe.md).
- Press F7 to toggle whether your car is controlled by AI or not. (TODO: Which AI? Vanilla or Improved?)
- Press F8 to visualize the chosen path and steering target for the current AI. (TODO: This still works when AI control is turned off, what is it showing then?)
- Press F9 to force a recalculation of the planned route.
- Press F11 to visualize the Road Nodes. Road Nodes are connected by colored lines which are Road Segments. The color of the line represents how fast that road segment can be taken (green=fast, red=slow). Click on a Node to make it active. Then you can see a detailed preview of Segments connected to this Road Node as you look around the Node. Click on a Segment to make it active. Then you can see its width and curvature, split up into Lanes, which are color-coded by type. Red for lanes that lead into obstacles, orange for rough terrain, green for road, blue for unidentified (TODO: Identify more lane types!). Labels show a Node's ID, and a Segment's ID, length, speed limit and turning radius.

## Mod Compatibility

- See [General Compatibility](../../ReadMe.md#general-compatibility).
- Required: [Split Screen Support](../split_screen_support/ReadMe.md).
- Conditionally compatible: NFSMWExtraOptions has its own F6 AI: Don't use both on one car.

# TODO
### PATHFINDING COST CALCULATION: NOT GOOD ENOUGH!
To improve this situation, we need a tool to run our time cost calculation on a whole segment. I think it should work by only allowing us to select nodes which are connected to the previously selected node, and then displaying a live time cost, possibly with a breakdown of the contributing segments.
- Circuit #1: This was our main testing ground so far, we win by a mile by avoiding the ridiculous pillars.
- Circuit #2: Improved AI is not taking the obvious shortcut reliably. Should do a cost check to see if something's wrong with our algos, if not, maybe hard-code some segments as preferred, but that would require going through the whole game world hunting for shortcuts, which would suck.
- Circuit #3: Improved AI smashes itself into a wall sometimes trying to follow a path which is not a part of this race (is behind an arrow barrier).
- Circuit #4: Improved AI is not taking a shortcut that the vanilla AI does a crazy smooth job at taking towards the end of the lap.

### OBSTACLES
- I wonder if we could do a better job at avoiding cars.
- Collision recovery could trigger instantly, if we detect that our speed has gone from 100 to 0 in a 0.1s window.
    - Collision recovery should preferably not re-trigger if we are successfully accelerating.
- Implement avoiding obstacles like barrels and other destructibles and physics objects, based on their weight. (really wanna avoid water barrels)

### PERFORMANCE
- Disabling of Super Steering is definitely not working, lmao.
- AI cars seem to limit their own performance to that of their opponent, maybe the weakest car in the race or something. This is probably wise, but adding an option would be useful to allow players to race a better car using a weaker car, for what should be an impossible challenge.
    - Make this an option called "Fair Performance: On/Off".
- Rename Nitrous: "Allow/Fair/Disabled" to "Fair Nitrous: On/Off"
- Would be nice to try to nail Perfect Launch every time.
- Would be nice to improve nitro management. Right now it is used quite eagerly and arbitrarily. I'd aim to have a reserve to be used after losing a lot of speed and having a straight section ahead, but other than that, do avoid allowing nitrous to sit on full, since then regeneration potential would be wasted.
- Would be nice if in squiggly roads (like in circuit 3) it would try to find a straight line through when it is possible, instead of following road curvature so religiously.

### UX
- Change F8/F11 keybinds, look for something that doesn't conflict with ExtraOptions hotkeys, and hide them behind an .ini toggle by default.
- Remove options like "Shortcuts", "Follow Race Route", etc, they're likely unnecessary with the new logic.
    - Well, I'm not so sure about this just yet... we may still need to improve our pathfinding logic.
- Allow Split Screen race to be started with only 1 input device, where the 2nd player is AI-controlled.
- Allow using the new AI for regular race opponents in single-player.
- Look into whether the AI settings can become a sub-menu inside the Gameplay options, or, since the Gameplay menu is so empty anyways, maybe we can just add them flat out.
Things only worth bothering if our AI can beat Vanilla AI on every track with various cars:
- Letting the AI control Player 1 should maybe be hidden behind an .ini toggle.

## AI Disclaimer

The code and this page were written by Claude (Sonnet & Opus). Testing and design were hand-made.
