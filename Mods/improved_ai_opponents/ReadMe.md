# Improved AI Opponents

This mod for Need for Speed: Most Wanted 2005 (1.3) aims to implement extremely good AI that doesn't cheat. This was thoroughly tested and verified by playing against the AI in split-screen mode, where it's easy to see what the AI is doing.

## Features
This mod is currently WIP, so it can't be used in Career mode yet, only in Split Screen.

- You can race against the Improved AI in Split Screen mode.
- TODO: You can race against the Improved AI in career mode.

## AI
- **Super Steering (TODO)**: The vanilla AI would ignore the handling capabilities of the controlled car, allowing it to take arbitrarily sharp turns at arbitrary speeds. The Improved AI uses the same steering code as the player.
- **Fair Nitrous**: The vanilla AI only uses nitrous if the player also has nitrous available in their car, and it only gets the same quality nitrous. In the Improved AI, this behaviour can be turned off (Fair Nitrous: Off), allowing the AI to use nitrous even if the player's car has none.
- **Fair Performance (WIP)**: The vanilla AI limits its car's performance (top speed and grip) to that of the weakest player's car. In the Improved AI, this behaviour can be turned off (Fair Performance: Off), allowing the AI to use whatever performance its car would normally have. Turning this option off may result in completely unfair races, this was not yet tested in career mode.
- **Catch-Up**: Also known as rubber-banding, this well-known mechanic would cause the vanilla AI to gain additional performance when falling behind the player. This mod allows turning this mechanic off for both the Vanilla and the Improved AI. If this is turned off, it is recommended to also turn off Fair Performance.
- **Pathfinding (WIP)**: The vanilla AI would always pick the shortest path, which is often a sub-optimal path with unnecessary sharp turns in it. The vanilla AI would also make unwise decisions about taking shortcuts that came down to dice rolls involving a skill level value, with the assumption that all shortcuts are good, which is far from the truth in this game. The improved AI calculates a time cost for each road segment in the race based on that road segment's curvature, lane quality, and width, then picks the fastest route. It has no concept of shortcuts, and no dice rolls. It just takes the fastest route.
- **Collision recovery (WIP)**: After a full on collision with a wall or traffic, the vanilla AI will throttle forward for 3 full seconds before deciding to enter its reversing routine to try to right itself. The Improved AI does this after 1.5 seconds instead.

## Debug tools
While developing this mod, I implemented some tools to better understand the game's road and pathfinding systems. These systems are intended to be used in combination with the free fly mode found in [Dev Tools](../dev_tools/ReadMe.md).
- Press F7 to toggle whether your car is controlled by AI or not. (TODO: Which AI? Vanilla or Improved?)
- Press F8 to step through the debug overlays (the **Debug Overlay** option of the AI Driving options page sets the same): **Off**, **AI Logic**, **Roads**, **Cost**. Use them together with the free fly mode of Dev Tools.
  - **AI Logic** visualizes the chosen path and steering target for the current AI, from the car on. The green lines are the Cookie Trail: the road's cross-sections between the car and the nav, which is the corridor the car steers through; they turn red where the car is out of bounds by the game's measure, which makes the game throw the route away. (TODO: This still works when AI control is turned off, what is it showing then?)
  - **Roads** is described below.
  - **Cost** is for checking what a route costs: see below.
- Press F9 to force a recalculation of the planned route.
- **Roads** mode visualizes the Road Nodes. Road Nodes are connected by colored lines which are Road Segments. The color of the line represents how fast that road segment can be taken (green=fast, red=slow). Click on a Node to make it active. Then you can see a detailed preview of Segments connected to this Road Node as you look around the Node. Click on a Segment to make it active. Then you can see its width and curvature, split up into Lanes, which are color-coded by type. Red for lanes that lead into obstacles, orange for rough terrain, green for road, blue for unidentified (TODO: Identify more lane types!). Labels show a Node's ID, and a Segment's ID, length, speed limit and turning radius. A curved Segment also shows how its speed limit is worked out: the amber lines are the road's Bezier handles, the white line is the middle of the road, the grey ticks and cyan lines are the samples and the band across the road that the best line may use, and the best line itself is colored by the speed its curvature allows (red slow, green fast). The magenta diamond marks the tightest point of the best line.
- **Cost** mode draws only the Road Nodes. Click a Node to start a route, then click the Nodes joined to either end of it (they are yellow; hovering one previews the Segment that would be added). The Segments between the picked Nodes are drawn, and the route's total length, time and cost are written beside each end of the route, calculated for your car as the Improved AI's pathfinding would, for a start at 30 m/s and from a standstill. Clicking an end Node takes it off again, clicking empty space does nothing, and right-click starts over.

## Mod Compatibility

- See [General Compatibility](../../ReadMe.md#general-compatibility).
- Required: [Split Screen Support](../split_screen_support/ReadMe.md).
- Conditionally compatible: NFSMWExtraOptions has its own F6 AI: Don't use both on one car.

# TODO
### PATHFINDING COST CALCULATION: NOT GOOD ENOUGH!
- Circuit #1: This was our main testing ground so far, we win by a mile by avoiding the ridiculous pillars.
- Circuit #2: The BMW in Redux flies so hard on the first shortcut that it flips upside down every time, lmao. Not sure how to fix, silly edge case.
- Circuit #3: Seems good now.
- Circuit #4: Improved AI is not taking a shortcut that the vanilla AI does a crazy smooth job at taking towards the end of the lap.

### OBSTACLES
- I wonder if we could do a better job at avoiding cars.
- Collision recovery could trigger instantly, if we detect that our speed has gone from 100 to 0 in a 0.1s window.
    - Collision recovery should preferably not re-trigger if we are successfully accelerating.
- Implement avoiding obstacles like barrels and other destructibles and physics objects, based on their weight. (really wanna avoid water barrels)

### PERFORMANCE
- Disabling of Super Steering is definitely not working, lmao.
- Would be nice to try to nail Perfect Launch every time.
- Would be nice to improve nitro management. Right now it is used quite eagerly and arbitrarily. I'd aim to have a reserve to be used after losing a lot of speed and having a straight section ahead, but other than that, do avoid allowing nitrous to sit on full, since then regeneration potential would be wasted.
- Would be nice if in squiggly roads (like in circuit 3) it would try to find a straight line through when it is possible, instead of following road curvature so religiously.

### UX
- Change the F8 keybind, look for something that doesn't conflict with ExtraOptions hotkeys, and hide them behind an .ini toggle by default.
- Remove options like "Shortcuts", "Follow Race Route", etc, they're likely unnecessary with the new logic.
    - Well, I'm not so sure about this just yet... we may still need to improve our pathfinding logic.
- Allow Split Screen race to be started with only 1 input device, where the 2nd player is AI-controlled.
- Allow using the new AI for regular race opponents in single-player.
- Look into whether the AI settings can become a sub-menu inside the Gameplay options, or, since the Gameplay menu is so empty anyways, maybe we can just add them flat out.
Things only worth bothering if our AI can beat Vanilla AI on every track with various cars:
- Letting the AI control Player 1 should maybe be hidden behind an .ini toggle.

## AI Disclaimer

The code and this page were written by Claude (Sonnet & Opus). Testing and design were hand-made.
