# Split screen (nfsmw_split_screen_support)

Input for player 2: see [input-system.md](input-system.md) (extra device). Marks: [read] [tested].

## What the PC exe still has
- The console split screen: `UIQRMainMenu::Setup` 0x7AA730 adds only Quick Play (icon 0xE6313967) and Custom Race (0x2A49B5E2), but `NotificationMessage` 0x7A2E20 still handles
  `QRMode` (0x9B9E68) == 2 (player count byte = 2, flag 0x400 in `[FEDatabase+0x12C]`, then the mode select screen). The Split Screen icon (0xF365B5F5, FrontB.lzc) and name (0x841D518A)
  exist in the data. The entry is added by hooking `IconScrollerMenu::AddOption` 0x573960 and, when it returns to 0x7AA821, adding an `IconOption` (0x4C bytes, `operator new` 0x652AD0,
  ctor 0x586FA0 (texture, name, description)) whose vtable is Custom Race's (0x8B7590: deleting dtor, React) with React replaced (message 0x0C407210 pressed: QRMode = 2) [tested].
- **FEDatabase** (pointer 0x91CF90): byte 0 player count, `+0x0C/+0x0D` joystick port of players 1/2, `+0x12C` bit 2 = Quick Race. `IsSplitScreenMode` 0x469120 = bit 2 and count 2.
  `Sim::Init` 0x6FC3A0 (from `BeginWorldLoad` 0x662950) gets user mode 1 for split screen (`Game_IsSplitScreen` 0x605630 reads `[0x988E04]`).
- The three fixes (the same ones NFSMWExtraOptions' `ExperimentalSplitScreenFix` makes; GPL source in `Tools/nfsmw-extra-options`, nothing copied): `0x7A3F68` 0x0C to 0x0D (the Press Start
  screen compares the pressed port with `[db+0x0C]`; player 2 may press Start with player 1's device), `0x666EB3` 75 to EB (`TrackLoader::LoadHandler` takes the normal loading screen: the split
  one crashes), `0x6CFC03` NOP (`MaybeChangeViewMode` 0x6CFAD0 only knows modes 0-2; mode 0 switches every view off: the black screen). Ours: byte patches plus a stub for the view mode
  (`common/patch.h`), each with an ini switch, backing off when ExtraOptions' fix is on.

## Rendering
- **eViews** at 0x9195E0, 0x70 bytes each: `+0` PlatInfo, `+4` id, `+8` Active, `+0x40` camera, `+0x44` mover list, `+0x5C` lighting context (from `SetupWorldLightContext` 0x5029F0, needs only the
  camera), `+0x60` render target. EVIEWMODE (`[0x982B40]` current, `[0x982B44]` requested, `[0x982B3C]` wanted): 3 = two views. View 2 is never Active on PC (its render target is never set); we
  give it view 1's render target and set Active at world load of a split screen game, and off afterwards [tested]: the game then does the per-view setup (0x6CF400: projection from the camera
  and the target) and the streaming for it.
- `eDisplayFrame` 0x6E7220 per frame: per-view setup, BeginScene, lights for views 1-3, **`___eDisplayFrame` 0x6DE300 (the whole world + effects, hard-wired to view 1, 0x919650)**,
  `FEManager::Render` (menus, HUD), EndScene, Present. The game clears colour and depth once per frame, after Present. `split_view.cpp` runs `___eDisplayFrame` twice in mode 3: view 1, copy a
  half-height band of the back buffer into a canvas (top), clear screen and depth, swap view 1 and 2's PlatInfo / camera / lighting (+0 / +0x40 / +0x5C), run again, copy the band (bottom), swap
  back, StretchRect the canvas to the back buffer. The canvas is released in an `IDirect3DDevice9::Reset` hook (vtable slot 16).
- **What a view is, for the second pass** (swapped between view 1 and 2 around the second `___eDisplayFrame`): PlatInfo `+0`, camera `+0x40`, lighting `+0x5C`, and the **camera mover list** (`+0x44`, the head node of
  an intrusive circular list: every node points back to it, so the lists are swapped node by node). `eView::GetCameraMover` is the list's first node. Car rendering asks it: `CarRenderConn::OnRender` skips the car
  the mover follows when `RenderCarPOV` is false (bumper / hood views) and, for the cube-map views, skips the car of view 1's mover. Without the swap player 1's bumper view hid player 1's car in player 2's picture [guess,
  untested fix].
- **Reflections**: the car reflection is a cube map drawn from six extra views (18-23 on PC), pointed at view 1's followed car once per frame by `UpdateEnvironmentMapCameras` (0x756060, only when `[0x9017B8]`) and set up by
  `SetupPerView` in `eDisplayFrame`'s loop; `___eDisplayFrame` draws them before the world. For the second pass both are done again after the swap [guess, untested].
- **Shadows (open)**: not understood yet. `GrandSceneryCullInfo::WhatSectionsShouldWeDraw` picks the scenery sections of the shadow-map views from the camera of view `id - 12` (GC numbering). Unknown on PC which views
  are the shadow maps (the PC enum is shifted by 2 at the env maps) and whether view 2's shadow map ever runs.
- Streaming: `UpdateCameraMovers` 0x47C880 gives the track streamer (0x9AA9D0) a position for each **active** view (`PredictStreamingPosition` 0x7238D0); with view 2 active the world around
  player 2 loads natively. The streamer has two positions (entries of 0x64 at +0xC0) and a split screen flag (+0xB0, from `InitRegion` 0x72C070) under which it keeps both sets of sections.
- Seen/expected: the HUD is player 1's and drawn over the whole screen; reflections (view 4) follow player 1; effects updated in `___eDisplayFrame` (rain) run twice per frame; motion blur could
  blend the two pictures (not seen); shadows pop in oddly (maybe an old streaming issue).

## Press Start and players
`FEngMapJoyParamToJoyport` 0x572FF0 (bit 0..3 to port 0..3, else -1): every FE press on PC is port 0, so the Press Start screen cannot tell devices apart. `uiQRPressStart` screen:
START_RELEASED 0xEBFCDA65, player number at screen `+0x2C`, `NotificationMessage` 0x7A3F20, `CreateQRPressStart` 0x570C60. The keyboard's Start is Space (Enter is Accept, ignored).
`players.cpp` ignores the game's own Start messages (which device ends in one depends on the bindings: Start on a pad is FE_ACTION_START = button 9 by default) and tells the screen itself: it reads
the XInput slots and the keyboard every frame and, when a controller lets go of Start or Enter / numpad Enter / Space is let go, sends START_RELEASED to the screen and gives that device to the player. Per race: player 1's vehicle takes its input from player 1's
device alone (`vehicle_source`, see input-system.md), player 2 reads the extra device; all devices drive every menu throughout (pause, Controls, car select); after the race (view mode leaves 3) every
device drives the vehicle again.
