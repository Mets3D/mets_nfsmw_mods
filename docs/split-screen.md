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
- **Shadows**: PC view ids differ from the console enum above 8: the cube-map views are 18-23 and the **shadow map views are 15 (player 1) and 16 (player 2)** (`eGetShadowMapView`, `eViews[15 + n]`, 0x919C70).
  `___eDisplayFrame` draws the player-1 shadow map each pass through `eRenderShadowPass(view 1)` → `eRenderShadowMap(view, ?, 0, 1)`, which puts view 15's camera around the camera of the view it is given (after
  the swap: the pass's player) and always uses shadow map 0 [tested with `[log] shadows=1`: drawn twice per frame, around each player]. **But the scenery for the shadow map is picked earlier** (`eStartFrameCulling`
  at the top of `___eDisplayFrame`, view 15, flags 0x4100) from view 15's camera as the last shadow map left it: in two passes that is the other player's. Symptom: shadows near a player pop out as the other
  player drives away (distance-based, some remain). Fix: view 15 gets a second camera object and PlatInfo (copies of its static ones; cameras are static 0x2C0 apart, view 1 at 0x986870, view 2 at 0x986B30; PlatInfos are `GimmeMyViewPlatInfo(id)` = 0x984130 + id*0x1A0) swapped in for the second pass, as a pair (camera alone made it worse: shadows behind player 1 vanished too) , so each pass starts with its own player's shadow camera from
  the previous frame [cause read + symptom matches; fix untested]. **View 16 has no camera (null): swapping with it crashes in `Camera::SetCameraMatrix`** [tested]. Gate in front of the shadow pass: `[0x901830] > 0` and `flt_8FAE68 >= flt_8FAE50` (meaning unknown).
- **Scenery draw table** [read; the vanishing shadows fix is untested]: each `___eDisplayFrame` call culls the scenery of every *active* view with an enabled cull job (views 1-4, the env maps, the shadow map view, last)
  into one table of draw entries (`DoCulling` 0x731B80: 0x994368..0x9A2DC8, about 3,700 entries, filled in job order). The game only draws view 1 per call, but in split screen view 2 is active too, so it is culled twice per frame for
  nothing, which can fill the table before the shadow map's job (last in the order): whole areas of shadow casters vanish (seen: shadows just behind the car, with or without the shadow camera fix, so not our regression).
  Switching view 2 off for the two passes (so it is not culled for nothing) **did not fix the vanishing shadows** behind the car and was removed (seen with every fix on and off: not our regression; whether the single player game has it is unchecked).
  Also tried without effect: pre-culler off (`PrecullerMode` 0x903160 = 0), the scenery detail patch below. Not tried: the shadow map's extent / fade (the shadow camera sits at the chase camera + sun direction * `[0x8FAEB8]`, looking at the chase camera position).
- **Scenery detail by view mode** [read; effect untested]: `ScenerySectionHeader::DrawAScenery` (0x729120) turns a pixel size into a model. In view modes >= 3 (`cmp eax,3; jl` at 0x729280) only objects over 22 px are
  drawn, always with the low detail model (`pModel[2]`); in one view over 17 px with the model the size calls for. Shadow casters go through the same test. Patched to the one view rule (`[fixes] scenery_detail`).
- Streaming: `UpdateCameraMovers` 0x47C880 gives the track streamer (0x9AA9D0) a position for each **active** view (`PredictStreamingPosition` 0x7238D0); with view 2 active the world around
  player 2 loads natively. The streamer has two positions (entries of 0x64 at +0xC0) and a split screen flag (+0xB0, from `InitRegion` 0x72C070) under which it keeps both sets of sections.
- Seen/expected: effects updated in `___eDisplayFrame` (rain) run twice per frame; motion blur could
  blend the two pictures (not seen); shadows pop in oddly (maybe an old streaming issue).

## HUD and minimap
- Split screen makes two HUDs (`FEngHud` 0x5A6600): type 3 `HUD_Player1.fng` (player number 0) and type 4 `HUD_Player2.fng` (1) [tested: log]. The files are in `GLOBAL/InGameSplitScreen.bun` (JDLZ chunks, 0x30210).
  Only `HUD_Player1.fng` has the minimap objects (TRACK_MAP, the markers, ...); the `Minimap` is made in `FEngHud`'s constructor for types 1 and 3. Console design: one minimap at (-221, 0) in FEng coordinates
  (origin screen centre, 640 x 480), on the line between the halves, centred on player 1, player 2 as a second marker [tested].
- **The decomp's struct layouts do not match the PC exe** for these classes: `HudElement` is 0x30 bytes (object list +8, package name +0x10, mask +0x18, features +0x20), so every `Minimap` member is the decomp's offset + 8
  (map layout +0x38, own marker +0x90, other player's marker +0x94, default position +0x9C, map centre +0xB8), size 0x338, made with `malloc`; `FEngHud`: type +0x20, minimap +0x320; `FEPackage` object list head +0x4C;
  `FEGroup` first child +0x64 (matches); `FEObject` up to +0x5C matches. Virtual tables of the FEObject family: entry 0 deleting destructor, entry 1 `Clone` [read, tested].
- `Minimap::Update(player)` centres on the player it is given, north up in split screen. `UpdatePlayer2Element` draws the *last entry of the local player list* (0x92D87C, count 0x92D884) as the second marker.
  The map picture comes from `ChoppedMiniMapManager::UncompressMaps` (9 tile slots): it unloads every tile not in the list it is given, so two minimaps need one list with both sets.
  The Minimap places markers and icons itself, in absolute screen coordinates around its map centre (`UpdateElementArt`); only the map picture group's position is derived (default - displacement, every frame).
- `FEObject::Clone` (vtable entry 1) copies the scripts and responses but **not the object's data** (`pData`: colour, position, size - zeroed, so the clone is invisible) nor a multi image's texture fields, and it gives new GUIDs
  [read: the copy constructor 0x5C31A0 zero-fills; tested: clones with zero data were invisible]. Only images, groups and multi images (no pointers in their data) are copied by hand.
- **A clone plays its scripts from the start, which sets the position the object was made with**: a shift applied to a clone's data is lost on the next package update [tested: the clone of the ring / backdrop group stayed at (0, 0) while
  the original, whose scripts had finished, kept its shift]. The map picture group is exempt (set every frame); the others are put back every frame.
- The minimap's circular backdrop is a black 128 x 128 image (hash 7af9160b) in the ring group (`TRACKMAPTARGETRING`, 382d2fc9) under the map picture. `HUD_Player2.fng` has an unrelated group with the same hash 157c5b37 at (-226, 7).
- FEng colours are 0xAARRGGBB (ints b, g, r, a in `FEObjData`); an object with alpha 0 is not drawn.
- **Built in `minimap.cpp`** [tested]: when player 2's HUD is made, the top level objects of `HUD_Player1.fng` that contain what the Minimap registered are cloned into `HUD_Player2.fng`; everything but the group holding the markers is moved
  +-120 (`[hud] minimap_offset`; the map picture, ring and backdrop); a second `Minimap` is built on the clones and shown / updated from player 2's `FEngHud::Update`; each minimap's second marker is the other player (hooked
  `UpdatePlayer2Element`); the markers are orange (player 1) and blue (player 2); the tile list passed to `UncompressMaps` is the union of both minimaps'; `AdjustForWidescreen` is repeated on the second. Not done: the rest of player 2's HUD
  is as the console has it (speedometer bottom right).
- **Minimap visibility on pause** [tested]: `FEngHud::DetermineHudFeatures` 0x57CA60 makes all HUD hide on pause (checks `FEManager::IsPaused()` and whether `Pause_Main.fng` is pushed, no per-player case: decomp
  `FEPkg_Hud.cpp`), called from `FEngHud::Update` 0x58CA30, which then `HudElement::Toggle`s (0x5793F0) whatever that hud owns. Player 2's clone minimap (`g_b`) isn't owned by `FEngHud`, so `minimap.cpp` calls the
  same two functions itself to mirror the native path - but from `events::on_frame` (the `eDisplayFrame` hook), not `FEngHud::Update`: that does not run at all while paused, so a hook on it never sees the
  pause/unpause edge.

## Press Start and players
`FEngMapJoyParamToJoyport` 0x572FF0 (bit 0..3 to port 0..3, else -1): every FE press on PC is port 0, so the Press Start screen cannot tell devices apart. `uiQRPressStart` screen:
START_RELEASED 0xEBFCDA65, player number at screen `+0x2C`, `NotificationMessage` 0x7A3F20, `CreateQRPressStart` 0x570C60. The keyboard's Start is Space (Enter is Accept, ignored).
`players.cpp` ignores the game's own Start messages (which device ends in one depends on the bindings: Start on a pad is FE_ACTION_START = button 9 by default) and tells the screen itself: it reads
the XInput slots and the keyboard every frame and, when a controller lets go of Start or Enter / numpad Enter / Space is let go, sends START_RELEASED to the screen and gives that device to the player. Per race: player 1's vehicle takes its input from player 1's
device alone (`vehicle_source`, see input-system.md), player 2 reads the extra device; all devices drive every menu throughout (pause, Controls, car select); after the race (view mode leaves 3) every
device drives the vehicle again.
