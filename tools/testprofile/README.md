# The OpenEden test profile

One folder of settings that switches on every finished OpenEden feature at once, so you can test them together. It is
**living**: every task that adds a setting adds it here, so preparing the profile again after a rebuild gives you the
new feature too.

## What it is

- `profile.cfg` lists the settings, one section per `settings.cfg` section, each with a comment naming the task that
  added it and what it shows.
- `variants/*.cfg` hold settings that exclude each other or cost a lot, so they are separate: `msaa` (multisampling
  instead of TAA), `glsl` (the GLSL shaders instead of the Slang ones), `lowtier` (the weakest whole-map tier), `aces`
  (another tonemap curve), `pcss` (soft shadow edges), `lowmemory` (a simulated 4 GiB card for the texture downscaler),
  `stats` (frame counters and the cell-load trace), `nolod` (the world LOD off, to tell a LOD pop from the live terrain), `vramsmall` (a simulated 225 MiB card for the video memory scale-back, the low tier), `rmlui` (the new UI system's test panel over the game).
- `prepare.sh` makes a **profile folder** under `I:\Projects\OpenEden-work\testprofile\<name>` from a **copy** of your
  `openmw.cfg` and `settings.cfg`. Your real folder (`Documents\My Games\OpenMW`) is only read: nothing is written to
  it, and the profile has its own saves, screenshots, logs and baked data.
- `exclude.txt` and `coverage.sh` keep the profile complete (below). `check.sh` tests the scripts.

## Prepare it

Close the game. **On Windows, without any bash window,** open a normal Command Prompt or PowerShell (or double-click the
file) and run the launcher, which finds Git's bash for you and your OpenMW config folder
(`Documents\My Games\OpenMW`, also under OneDrive):

```
cd I:\Projects\OpenEden
tools\testprofile\prepare.bat
tools\testprofile\prepare.bat msaa aces
```

In PowerShell write `.\tools\testprofile\prepare.bat`. It only needs Git for Windows to be installed. To name the config
folder yourself, make it the first argument: `prepare.bat "C:\Users\you\Documents\My Games\OpenMW" msaa`. Everything
below that says `prepare.sh` can be run through `prepare.bat` the same way.

From Git Bash (or any shell that runs `bash`) the script itself:

```
cd /i/Projects/OpenEden
tools/testprofile/prepare.sh "C:/Users/smith/OneDrive/Documents/My Games/OpenMW"
```

With variants (they win over `profile.cfg`, in the order given), or another profile name, or without baking:

```
tools/testprofile/prepare.sh "<your config folder>" msaa aces
tools/testprofile/prepare.sh "<your config folder>" --name weak lowtier
tools/testprofile/prepare.sh "<your config folder>" --no-bake
```

The first run bakes (about two minutes; each bake starts the game in an off-screen window): the terrain tiles and object
clusters for the whole-map view, the merged bodies, the light probes of Caldera's Guild of Mages and Arrille's Tradehouse,
and the sky visibility around Seyda Neen. A second run says "already up to date" for what has not changed.

**The profile uses the build folder's own executable** (`I:\Projects\OpenEden\build-jolt\openmw.exe` unless you pass
`--build`). A rebuild changes the game under the profile, and a build in progress can leave `openmw.exe` missing for a
while. **Prepare again after a rebuild that adds settings:** the script then checks every profile setting against the
build's `defaults.cfg` and warns about the ones the build does not know yet (the game would ignore them).

Preparing again replaces `config\settings.cfg` with your settings plus the profile (the game rewrites that file when it
exits, so settings you changed in the in-game options are lost; the old one is kept as `settings.cfg.previous`).
Saves, screenshots and baked data in `user\` are kept.

## Start it

In the profile folder (`I:\Projects\OpenEden-work\testprofile\main`):

- `start_game.bat` (or `start_game.sh` in Git Bash): the game, with a visible window. Extra arguments are passed on, for
  example `start_game.bat --skip-menu --start "Seyda Neen"`.
- `start_game_quiet.*`: the same with `OPENEDEN_QUIET_WINDOW=1` (a window far off the desktop; for scripted runs).
- `start_editor.*` and `start_editor_quiet.*`: Eden Studio with the profile.

Screenshots (F12) go to `user\screenshots`; the log is `config\openmw.log`; at exit it also holds the GPU time of each
render pass (`gpu timing`).
- *Pipeline cache* (2026-11-09): `pipeline cache = true` keeps the driver's compiled pipelines in `<user data>/openeden/shadercache/pipelinecache-*.bin`. The first run on a new driver still stalls for seconds while the driver compiles; the second run should show no `compile.vsg` scope over 100 ms on the main thread.

## Console commands that help

Open the console with the key under Escape (`~` or `` ` ``).

| Command | Does |
| --- | --- |
| `coc "Seyda Neen"`, `coc "Balmora"`, `coc "Vivec"` | Teleport to a cell. Interiors: `coc "Caldera, Guild of Mages"`, `coc "Seyda Neen, Arrille's Tradehouse"` |
| `set gamehour to 18.5` | Time of day (18.5 is the sunset colours; 12 noon; 23 night) |
| `set timescale to 0` | Stop the clock, so the light stays |
| `ChangeWeather "Bitter Coast Region" 4` | Weather for a region: 0 clear, 1 cloudy, 2 foggy, 3 overcast, 4 rain, 5 thunder, 6 ash, 7 blight, 8 snow, 9 blizzard. Changing the region you stand in starts a transition of a minute or more; `coc` somewhere afterwards to see it at once |
| `ChangeWeather "Isinfier Plains Region" 8` | Snow (Solstheim, Bloodmoon); `"Ashlands Region" 6` or `"Red Mountain Region" 6` for ash |
| `tgm`, `tcl` | God mode, collision off (to fly: `player->setpos z 25000`) |
| `tcb` | Toggle the Jolt physics debug draw. `togglenavmesh` the navigation mesh |
| `cmStyle`, `cmStyle off`, `cmStyle cm_heavy_test` | Show / change the CreativeMotion style of the target (the player if none) |
| `cmRagdoll`, `cmRagdollOff` | Drop a limp CreativeMotion ragdoll into the physics world |
| `Player->BecomeWerewolf` | A werewolf body (merged body and equipment checks keep the stock parts) |

## What to look at, feature by feature

For each: where to go, what you should see, what "wrong" looks like. The log (`config\openmw.log`) says when a setting
was refused ("`[Rendering] taa needs ...`"); `prepare.sh` and `check.sh` look for that too. Debug views replace the
picture: switch one on by hand in `config\settings.cfg` with the game closed, and remove it afterwards.

**Rendering**
- *HDR, bloom* (0025): torches and the sun glow softly. Wrong: a flat grey or overexposed picture. `variants/aces.cfg`
  changes the curve.
- *G-buffer* (0026): nothing visible. `gbuffer debug view = normals`, `depth`, `velocity` (moving things coloured) show it.
- *PBR* (0027, `pbr look = physical`): wet rock, metal armour and water edges are shinier than vanilla. Wrong: everything
  glossy like plastic. `pbr material maps` (0044) and `parallax` (0045) show only with a texture pack that has `_rmaos` /
  `_p` files next to its textures; with plain Morrowind they change nothing.
- *Light probes, interiors* (0028): in Caldera's Guild of Mages or Arrille's Tradehouse the light is warmer around the
  torches and darker in corners than in a plain interior. Other interiors keep the flat ambient colour until baked
  (`openmw --bake-gi "<cell>"`). `gi debug = true` draws a small octahedron at every probe.
- *Sky visibility, exteriors* (0036): in the cells around Seyda Neen the ambient light is darker under the bridge, trees
  and eaves. Cells you have not baked (`--bake-gi-exterior "x,y"`) are unchanged.
- *Ambient occlusion* (0029, terrain 0048, temporal 0076): creases, wall bases, rocks on the ground and the bottoms of
  cliffs are darker. Wrong: a fixed grain that does not change (temporal is not on), or a dark halo that trails behind a
  moving thing. `ao temporal debug = raw` or `weight` shows the accumulation.
- *Subsurface scattering* (0033): close to a face (third person, stand near an NPC) the skin is slightly soft and warm at
  edges. `gbuffer debug view = subsurface` shows what is marked as skin (eyes are not).
- *TAA* (0075): edges are smooth and thin wires do not shimmer when you turn. Wrong: ghost trails behind things that
  move; rain or snow fainter than with `taa` off (fixed: blended draws come after the resolve). `taa debug = velocity`, `reject` or `weight`. `variants/msaa.cfg` compares.
- *Atmosphere* (0034): at `set gamehour to 18.5` the sky and fog are at their sunset colours; ash storms, blight and the
  night keep the weather's own colours. *Procedural sun* (0047): the sun is a disc with a glow, not a flat texture; look
  toward it at 15:00. *Sun glow* (0040): a warm halo in foggy or hazy weather, none in clear weather.
- *Clouds and cloud shadows* (0038): in cloudy weather (`ChangeWeather ... 1`) the undersides are darker and the edges glow,
  and moving shadows cross the ground. *Volumetric clouds* (0042): in fair weather the sky layer is a real volume; ash, blight,
  blizzard, fog and snow keep their own sky. Wrong: flicker or blocks when the camera turns. Heavy on the GPU.
  The three horizon settings (0042 follow-up: `horizon`, `curved layer`, `variety`) take the clouds down to the horizon, where the vanilla dome left a clear band, and make the pattern differ from place to place; look at the horizon over open water. Wrong: a hard edge or a bright line where the clouds end, or the same cloud shapes in a regular grid.
  *Cirrus* (0079): in clear weather, thin white streaks high above the other clouds (look up with the camera at the
  sky); at sunset they stay lit and turn pink after the ground is dark; they thin out as clouds come in and are gone in
  overcast and rain. Wrong: dashes or flicker near the horizon, a second sun round the real one.
- *Volumetric fog and light shafts* (0041): in the Bitter Coast fog or at a low sun between trees, shafts of light through
  the fog. Wrong: banding or a bright haze everywhere.
- *Wetness* (0037): rain makes the ground darker and glossier and flat ground collects puddles that mirror the sky; it
  dries about a quarter of an hour (game time) after the rain stops. *Puddle map* (0095): on the TERRAIN, puddles sit in
  the hollows the bake found (few and small: about 0.3 percent of the land), grow from their lowest point as the rain
  wets the ground and shrink back the same way as it dries, follow the ground's contours at the edge, are darker where
  deep and show ripples only where deep; objects and anything the map does not cover keep the noise puddles (with the
  `puddlestatics` variant, which also bakes roads, paving and decks: they take the baked puddles too, so a road is dry
  where it slopes and wet only in its own dips; the capture walks the player through every cell and takes long). Try
  `ChangeWeather "Bitter Coast Region" 4` or Seyda Neen, and `wetness puddle debug = capacity` (or `level`, `cell`) in
  `settings.cfg` to see the map. Wrong: puddles on a slope or a hill top, a puddle that jumps when the camera crosses a
  cell border, a mirrored placement (a puddle next to its hollow). *Precipitation lighting* (0040): rain and snow are lit
  by the scene and torches. *Lightning* (0040): `ChangeWeather ... 5`; a cold flash, and the shadows tilt for a moment.
- *Snow and ash cover* (0039): `ChangeWeather "Isinfier Plains Region" 8` (snow) or an ash storm: upward-facing surfaces
  gather white or dark grey over twenty game minutes; walls stay clear.
- *Slang shaders* (0072): the look should match `variants/glsl.cfg`. A difference between the two is a bug. On a cold
  cache a shader variant the build did not list is compiled on a worker thread while the GLSL draws it (no stall);
  objects loaded in that second or two keep the GLSL until they are loaded again, and so does the terrain until the next
  start. A stall longer than a frame on loading is a bug: find its `Slang shader compiled` line in `openmw.log`
  (`[Cells] load trace = true`).

**Shadows**
- *Sun shadows* (0052): objects, terrain, actors and the player cast shadows outdoors. Wrong: a crash when you switch
  `object shadows` on (fixed).
- *Point-light shadows* (0077): in Arrille's Tradehouse a lantern, the plant and the stool cast shadows on the walls;
  NPCs and the player cast moving shadows past a torch. Known: a lantern's own bars cast spoke shadows; water glints are
  not shadowed. Only the nearest `point light shadow count` (4) lights cast.
- *Point-light shadow cache* (0077, stage e): the same scene with `point light shadow cache` on (the profile): a walking
  NPC's or a swinging door's shadow follows within a few frames, a lantern left alone costs almost nothing. Wrong: a
  shadow that stays where a moved actor or door was for more than about half a second, or a shadow that flickers with the
  torch. Set it to false to compare.

**UI**
- *New UI system, first slice* (0104, decision 0015): `prepare.bat rmlui` (the variant switches `[GUI] rmlui` on). A dark panel
  at the top left with "OpenEden UI", a live "FPS: n" line (updated twice a second) and three list lines, drawn in the
  game's own Morrowind font over the game and its HUD. Wrong: no panel, garbled or missing letters, a panel that moves or
  flickers, a black or white box, or any change to the game's own windows and HUD. Off (the default) must look exactly as
  before. (Input, scaling and resize came with task 0111: see the next lines.)
- *New UI system, input* (0111 and its follow-ups): with the `rmlui` variant, open a game menu (Escape), then press the Back button of a
  gamepad (or F9) with no mouse touched: the first button of the panel lights up; the d-pad moves between the buttons and the text
  field, A presses, B or Back/F9 again gives the controller back to the game. In the text field Ctrl+C, Ctrl+X, Ctrl+V and
  Shift+Insert work with the system clipboard. Wrong: Back/F9 does something in the game's menu while the panel is shown, the
  focus does not enter, or paste puts old text.

**World**
- *Whole-map view* (0055, 0059, 0066): `tcl`, `tgm`, then `player->setpos z 25000` over Balmora: land, towers and trees out
  to the horizon with a blue haze, no draw edge. Wrong: holes in the land, a pop when you rise, or clusters that
  stay when you land. `view distance = auto` takes the reach of the loaded land. `variants/lowtier.cfg` shows the weakest
  tier.
- *Far lava, waterfalls and the Ghostgate fence move* (0096 stage b): from a hill over Vivec or at the Ghostgate, look at
  the far lava planes, Vivec's waterfalls and the barrier: the texture scrolls as it does when you stand next to it
  (`object cluster effects` and `object cluster scenery`, both in the profile; switch `effects` off to see them still).
  Wrong: a scroll that jumps, runs at another speed than near, or stops; a hole where a plane was; an activator you can
  no longer use near the player (clusters never draw in the active grid).
- *Far lava and waterfalls keep moving in the object paging chunks* (0096 stage d): `animated objects` (in the profile) keeps
  the texture scroll of activators the chunks draw (lava planes, waterfalls, the Ghostgate fence) running between the
  live range and `animated object distance` (24,576 units), every frame near, every 4th and every 16th further out. It
  matters where the baked clusters do not cover (with `object clusters` off, or the cells next to the active grid).
  `OPENEDEN_FARANIM_TRACE=1` logs a line a second with the chunk, object and controller counts. Wrong: a scroll that
  jumps when you walk closer, one that runs at another speed than the near copy, or a frame-time rise.
- *Far trees sway* (0096 stage c): from a hill in the Bitter Coast or the Ascadian Isles look at the far trees (they are
  far clusters beyond the live objects): they lean and swing slowly with the wind, more on a stormy day, the trunk's foot
  fixed to the ground (`object cluster sway`, in the profile; it needs the objects re-baked, `prepare.sh` does it).
  `object cluster sway distance boost` (also in the profile) makes the far trees move up to 6 times more; set it to
  false to see the real, nearly invisible size. Wrong: a tree that detaches from the ground, shimmering or jumping, the whole forest moving exactly in step, leaves
  that turn transparent or opaque black (the model's vertex alpha is replaced), or trees that swing with no wind.
- *Terrain tile fade* (0087): fly (`tcl`, `tgm`) up and down over Balmora, or along the ground toward Vivec: far tiles
  dither in and out over 0.4 s (a fine stipple, softened by `taa`) instead of popping, with no hole in the land. Set
  `[WorldLod] terrain fade time = 0` for the old instant swap to compare. Wrong: a flash, a hole, or a tile that stays
  half faded.
- *Incremental cell build* (0060): walk (or `tcl` and fly low) across a cell border; objects appear over a few frames
  instead of one long frame. Wrong: objects that never appear.
- *Merged body* (0069): NPCs and the player have the same body as before, built as one mesh. Wrong: gaps or cracks at
  the wrist, knee, ankle or neck; the skin through clothing. The log has a line "Merged bodies: ... draws the merged
  body" per actor, and one message box says to re-bake if the cache is stale (a load order change; run `prepare.sh`).

- *Facial speech curves* (0071): an NPC with a `.facecurves` file beside its voice line moves its mouth and blinks by
  the curves, in sync with the voice; no such file exists unless you generate one (`components/face` has the Rhubarb and
  CSV importers), so by default nothing changes. Wrong: a mouth that lags or leads the voice, stays shut, or an NPC whose
  lines with no curves file stop moving their mouth. `OPENEDEN_FACE_TRACE=1` logs the sampled values per frame.

**Animation (the `[Game]` features)**
- *Compound hitboxes* (0011): `tcb` shows a multi-segment shape round each actor.
- *Foot planting, look at, smoother turns* (M9): feet stay on slopes and stairs, NPCs turn their heads to you, turns are
  not snapped.
- *Stride sync* (0108, `inertial stride sync`): the walk and run keep the body's speed in step with the clip's own root speed,
  so a planted foot should not skate in steady walking, arcs and strafing.
- *Foot lock* (0108): stop walking or running and look at the feet: the foot on the ground stays where it was and settles
  slowly instead of gliding back to the idle pose. `tools/footslip_check` measures it.
- *Inertial locomotion* (high, also for NPCs) and *procedural lean*: the player accelerates and stops with momentum and
  leans into turns.
- *CreativeMotion `natural`* (0067, with the style layer 0053): changes of animation blend (the stop from a run is soft,
  idle has a little breathing and sway). `cmStyle` names the style; `cmStyle cm_heavy_test` exaggerates it.
- *Weapon and shield sheathing*: needs modded assets; with plain Morrowind nothing changes.
- *Extension debug draw and profiler* (task 0017): `extension debug draw` draws what enabled extensions submit as lines in the world (the example extension draws a line over the player and a box, sphere, point, label and line ahead of him; labels show as small crosses), `extension profiler` fills the Extensions tab of the debug window (F10) with each extension's timings per phase. Nothing shows until an extension is enabled (`extension=example` in `openmw.cfg`).
- *Neutral world* (0103, N3a-1): `neutral world` builds the core's neutral world store for game packs; nothing shows with Morrowind. With `game=skyrimse` the Skyrim SE pack registers its (still empty) world source and no longer refuses the session. Nothing reads the store yet.
- *Ragdoll* (0035): `cmRagdoll` on the target. *Death ragdolls* (0106): kill an NPC (e.g. `"name"->SetHealth 0` in the console, or hit it) and watch it fall and settle.

## Video memory scale-back (task 0080)

- `[VideoMemory] enabled` is on in the profile. With the driver's own budget a large card never reaches it. Add the
  variant `vramsmall` and watch openmw.log for `VRAM scale-back: reduced far objects to step N (used X MiB of budget Y
  MiB)`: the far field's live memory budgets are halved one step at a time, in the order of `order`, and the farthest
  objects thin out (a frame viewed at 25,000 units up: sparser distant vegetation, no holes). Restoring needs the load
  to fall well below the budget (see the task's stage log). `report = true` prints one summary line at exit.
  `texture mips` is last in the profile's `order`: with a small budget it drops the top mip levels of textures of 2048
  and up after the far field (`VRAM scale-back: reduced texture mips to step N`), and the console command `texturemips
  status|0|1|2|off` calls it by hand (a texture mod such as Landscape Remastered shows it; the full profile with that mod is
  ended by the freeze monitor at start, a known separate problem).

## Known limits

- Blended draws (rain, snow, glass, particles) are drawn after the TAA resolve, so their edges are not anti-aliased; fast-moving things may leave a faint trail. `variants/msaa.cfg` swaps it out.
- **A cold shader cache no longer stalls the game** (`user\openeden\shadercache`, one per profile): a Slang variant the
  build did not list is compiled on a worker thread (80 to 110 ms each, 400 to 430 ms for the first, which loads the library)
  while the GLSL draws, so for the first minutes some objects, and the terrain until the next start, are drawn by the GLSL
  (the look differs only by rounding). The cache stays, so the second start reads everything at once. After a minute with
  nothing to compile the compiler gives back its about 265 MiB. With `[Cells] load trace = true` every compile is a
  line in `openmw.log` and a `slang.compile` record in `load-trace.jsonl`.
- **Terrain pops and the log (task 0087):** every terrain tile change of the world LOD is a `World LOD tile swap: frame N, ...: new|refined|coarsened|to live|removed` line in `openmw.log`, and with the `stats` variant (`[Cells] load trace = true`) also an event `lod.tile.<kind>` in `load-trace.jsonl`; match a pop in a video to the nearest swap in time.
- **`map view` costs memory:** about +100 MiB resident with terrain tiles alone and +200 to +450 MiB with object clusters
  (the tier decides; `variants/lowtier.cfg` and `far memory budget` limit it).
- **Volumetric fog and volumetric clouds are the heaviest GPU effects**; with everything on, expect a much lower frame
  rate than the default game.
- **Bakes are for Seyda Neen only** (light probes for two interiors, sky visibility for nine cells). Others are unchanged
  until baked. A change of the load order makes the LOD packs and the merged bodies stale: run `prepare.sh` again.
- **The profile has no texture pack:** `pbr material maps` and `parallax` need `_rmaos` / `_p` files that Morrowind does
  not have.
- The merged body, weapon sheathing and some animation features keep the stock look when their data is missing; the log
  says so once.

## Keeping it alive

A task that adds a user-visible setting adds it to `profile.cfg` (or a variant) with a `# task NNNN: ...` comment and a line
in the list above in the same change. `coverage.sh` fails when a setting of `files/settings-default.cfg` under
`[Rendering]`, `[WorldLod]`, `[Bodies]`, `[Shadows]`, `[Cells]` (incremental), the CreativeMotion, inertial and sheathing keys of
`[Game]`, and `[Physics]` (ragdolls) is neither in the profile nor in `exclude.txt`, where debug views, tuning values and
bake parameters are listed with a reason. It also fails on a profile line naming a setting that does not exist.
`check.sh` runs it, tests `prepare.sh` against a made-up config, and with `--game <your config folder>` prepares the real
profile and starts the game once outdoors and once indoors, off-screen, to read the log for refused settings.

## The Landscape Remastered texture mod (a test aid)

`texturemod.sh <profile folder> add|remove [--pbr] [--lava]` adds the mod's data folders (extracted to
`I:/Projects/OpenEden-work/mods/landscape-remastered`: `00 core` land textures, `01 PBR` `_spec` maps, `02 glowing lava
rocks` meshes) to a profile's `config/openmw.cfg`, after the last `data=` line so it wins. Run it after `prepare.sh`,
which writes a new `openmw.cfg`. Its terrain textures are 2048 (diffuse) and 4096 (`_nh`), and its grass 8192; the texture
mip swaps (task 0080) do not cover terrain layer arrays yet, so only the lava rocks' two 2048 textures can be swapped.
