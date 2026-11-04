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
  `stats` (frame counters and the cell-load trace).
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
  move; rain fainter than vanilla (known). `taa debug = velocity`, `reject` or `weight`. `variants/msaa.cfg` compares.
- *Atmosphere* (0034): at `set gamehour to 18.5` the sky and fog are at their sunset colours; ash storms, blight and the
  night keep the weather's own colours. *Procedural sun* (0047): the sun is a disc with a glow, not a flat texture; look
  toward it at 15:00. *Sun glow* (0040): a warm halo in foggy or hazy weather, none in clear weather.
- *Clouds and cloud shadows* (0038): in cloudy weather (`ChangeWeather ... 1`) the undersides are darker and the edges glow,
  and moving shadows cross the ground. *Volumetric clouds* (0042): in fair weather the sky layer is a real volume; ash, blight,
  blizzard, fog and snow keep their own sky. Wrong: flicker or blocks when the camera turns. Heavy on the GPU.
  *Cirrus* (0079): in clear weather, thin white streaks high above the other clouds (look up with the camera at the
  sky); at sunset they stay lit and turn pink after the ground is dark; they thin out as clouds come in and are gone in
  overcast and rain. Wrong: dashes or flicker near the horizon, a second sun round the real one.
- *Volumetric fog and light shafts* (0041): in the Bitter Coast fog or at a low sun between trees, shafts of light through
  the fog. Wrong: banding or a bright haze everywhere.
- *Wetness* (0037): rain makes the ground darker and glossier and flat ground collects puddles that mirror the sky; it
  dries about a quarter of an hour (game time) after the rain stops. *Precipitation lighting* (0040): rain and snow are lit
  by the scene and torches. *Lightning* (0040): `ChangeWeather ... 5`; a cold flash, and the shadows tilt for a moment.
- *Snow and ash cover* (0039): `ChangeWeather "Isinfier Plains Region" 8` (snow) or an ash storm: upward-facing surfaces
  gather white or dark grey over twenty game minutes; walls stay clear.
- *Slang shaders* (0072): the look should match `variants/glsl.cfg`. A difference between the two is a bug. The first
  compile on a cold cache stalls the game for a few seconds.

**Shadows**
- *Sun shadows* (0052): objects, terrain, actors and the player cast shadows outdoors. Wrong: a crash when you switch
  `object shadows` on (fixed).
- *Point-light shadows* (0077): in Arrille's Tradehouse a lantern, the plant and the stool cast shadows on the walls;
  NPCs and the player cast moving shadows past a torch. Known: a lantern's own bars cast spoke shadows; water glints are
  not shadowed. Only the nearest `point light shadow count` (4) lights cast.

**World**
- *Whole-map view* (0055, 0059, 0066): `tcl`, `tgm`, then `player->setpos z 25000` over Balmora: land, towers and trees out
  to the horizon with a blue haze, no draw edge. Wrong: holes in the land, a pop when you rise, or clusters that
  stay when you land. `view distance = auto` takes the reach of the loaded land. `variants/lowtier.cfg` shows the weakest
  tier.
- *Incremental cell build* (0060): walk (or `tcl` and fly low) across a cell border; objects appear over a few frames
  instead of one long frame. Wrong: objects that never appear.
- *Merged body* (0069): NPCs and the player have the same body as before, built as one mesh. Wrong: gaps or cracks at
  the wrist, knee, ankle or neck; the skin through clothing. The log has a line "Merged bodies: ... draws the merged
  body" per actor, and one message box says to re-bake if the cache is stale (a load order change; run `prepare.sh`).

**Animation (the `[Game]` features)**
- *Compound hitboxes* (0011): `tcb` shows a multi-segment shape round each actor.
- *Foot planting, look at, smoother turns* (M9): feet stay on slopes and stairs, NPCs turn their heads to you, turns are
  not snapped.
- *Inertial locomotion* (high, also for NPCs) and *procedural lean*: the player accelerates and stops with momentum and
  leans into turns.
- *CreativeMotion `natural`* (0067, with the style layer 0053): changes of animation blend (the stop from a run is soft,
  idle has a little breathing and sway). `cmStyle` names the style; `cmStyle cm_heavy_test` exaggerates it.
- *Weapon and shield sheathing*: needs modded assets; with plain Morrowind nothing changes.
- *Ragdoll* (0035): `cmRagdoll` on the target.

## Known limits

- **TAA makes rain streaks fainter**, and fast-moving things may leave a faint trail. `variants/msaa.cfg` swaps it out.
- **The first Slang compile stalls the game for a few seconds** on a cold shader cache (`user\openeden\shadercache`, one per
  profile). A stall over 5 s is taken for a freeze: a quiet game ends with a `openmw-freeze.dmp` and exit code 0xDEAD, a visible
  one shows the Fatal Error box. If a fresh profile does that in its first minutes, start it again; `prepare.sh` retries a
  bake once for this reason. The cache stays, so the second start is quick.
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
