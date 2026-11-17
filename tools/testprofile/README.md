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
  `ChangeWeather "Bitter Coast Region" 4` or Seyda Neen, and `wetness puddle debug = capacity` (or `level`, `cell`, `relief`) in
  `settings.cfg` to see the map. *Topographic puddles* (0095, stage W-e): roads, plateaus and fields where the map found no pit get small, shallow
  puddles in the dips of the ground (`wetness puddle topographic`); they should sit in slight hollows, not in random spots,
  appear when the rain has soaked the ground, spread with more rain and shrink as it dries. Wrong: puddles on a slope or a hill top, a puddle that jumps when the camera crosses a
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
- *New UI system, Lua library* (0162): the same panel written with `openeden.ui` (`tools/ui_check/mod`, `tools/ui_check/check.sh`;
  needs `rmlui debug panel = false` and the content file `uipanel.omwscripts`): a panel with two buttons that count clicks, a text field
  that echoes, a 12 row list, the frame rate line and a frame counter each in its own small document. Wrong: a missing or doubled
  panel, a counter that stops or flickers, clicks that do nothing, a field that does not echo, any difference from the C++ panel other
  than the title.
- *New UI system, input* (0111 and its follow-ups): with the `rmlui` variant, open a game menu (Escape), then press the Back button of a
  gamepad (or F9) with no mouse touched: the first button of the panel lights up; the d-pad moves between the buttons and the text
  field, A presses, B or Back/F9 again gives the controller back to the game. In the text field Ctrl+C, Ctrl+X, Ctrl+V and
  Shift+Insert work with the system clipboard. Wrong: Back/F9 does something in the game's menu while the panel is shown, the
  focus does not enter, or paste puts old text.

- *New UI system, themes* (0163): with the `rmlui` variant set `rmlui theme = high-contrast` (opaque black, white text, yellow
  focus and 2 px outlines) or `rmlui theme variants = large-text` (or `small-text`) in the copied settings. The panel changes
  look only; the layout stays readable at every scale. While the game runs, saving a change to a `ui/*.rcss` file in a data
  folder shows within a second (the variant sets the reload interval to 1). Wrong: unstyled (white or inline) text, a
  theme that does not apply, text that is cut off, or a crash on a bad style sheet (a bad sheet must only log an error).

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
- *Banners and flags wave* (`animate placed scenery keyframes`, task 0096 stage f): the keyframe files of placed scenery play, near and far (the far field needs `[WorldLod] animated objects`).
- *Extension debug draw and profiler* (task 0017): `extension debug draw` draws what enabled extensions submit as lines in the world (the example extension draws a line over the player and a box, sphere, point, label and line ahead of him; labels show as small crosses), `extension profiler` fills the Extensions tab of the debug window (F10) with each extension's timings per phase. Nothing shows until an extension is enabled (`extension=example` in `openmw.cfg`).
- *Neutral world* (0103, N3a-1): `neutral world` builds the core's neutral world store for game packs; nothing shows with Morrowind. With `game=skyrimse` the Skyrim SE pack registers its (still empty) world source and no longer refuses the session. Nothing reads the store yet.
- *Neutral actors* (0121, N5-1 and N5-2): `neutral actors` draws a game pack's NPCs standing in their rest pose (Skyrim SE: the Bannered Mare's people, dressed, no animation). Needs `neutral world` and `game=skyrimse`; nothing shows with Morrowind.
- *Neutral actor idles* (0132, 0145): `neutral actor idles` makes those NPCs loop the calm standing idle (`mt_idle` of their sex) decoded on a worker from the game's own `.hkx` files (root motion ignored). Needs `neutral actors`.
- *Actor packages* (0135, S5): `[extension.skyrimse] actor packages` makes the Skyrim NPCs run their PACK packages by game hour and conditions: sandbox NPCs wander round their spot and pause, travel packages walk to a place. Needs `neutral actors`; nothing shows with Morrowind.
- *Harvesting* (0221): `[extension.skyrimse] harvest = true` (with `inventory` and `[Game] neutral inventory`) makes the real activate key harvest a plant or mushroom (a FLOR or TREE base with an ingredient): the ingredient goes to the inventory, the plant disappears and grows back after 10 game days (also across saves); the tooltip says "Harvest <name>". A pack setting, so not set in the profile. Nothing shows with Morrowind.
- *Dragons* (0253): `[extension.skyrimse] dragons = true` (with `creatures`, `combat ai`, `shouts` and `[Game] neutral combat`, `neutral actors`, `neutral actor idles`, `neutral actor locomotion`) makes a Skyrim dragon a dragon: it walks, bites and tail-whips with its own clips, breathes fire (its shout's first spell) in a 25 degree cone from its mouth, takes off after 14 s of fighting (`dragons flight`, default true), circles you at a height breathing, lands, and when it dies its soul comes to you ("Dragon soul absorbed", `souls` of the shouts). A pack setting, so not set in the profile; `dragons test place = EncDragon01Fire` with `dragons test replace = EncCow@31505,-11573` puts one where a cow stands. Nothing shows with Morrowind.
- *Diseases* (0240): `[extension.skyrimse] diseases = true` (with `[Game] neutral combat`, `creatures = true` and the neutral actors) lets a landed, unblocked blow of a creature whose race attack carries a disease (wolf and fox: Rockjoint, skeever: Ataxia, bear: Bone Break Fever, chaurus: Rattles, sabre cat: Witbane, hagraven: Brain Rot, ash hopper: Droops) infect you by a seeded roll (10 percent, less with disease resistance: Argonian and Wood Elf half): the message "You have contracted ...", a status line "Diseased: ..." and the penalty (stamina or magicka maximum, stamina or magicka regeneration, or 25 percent less one-handed and two-handed damage). A Cure Disease potion or a temple shrine cures it; it is saved. A pack setting, so not set in the profile; `diseases test chance = 1.0` makes every carrier blow infect. Nothing shows with Morrowind.
- *Vampirism* (0260): `[extension.skyrimse] vampirism = true` (with `papyrus`, `diseases`, `rest`, `[Game] neutral combat` and `[Game] neutral inventory`) runs the game's own `PlayerVampireQuestScript`: Sanguinare Vampiris (`diseases test infect = b8780`; a creature carrying it is not built) turns you three game days later (message, stage 1); each 1, 2 and 3 game days without feeding raises the stage (frost resistance 20 to 50, fire weakness 20 to 50, sunlight damage that grows, Vampire's Servant, Seduction and Embrace of Shadows as powers on the `P` key); activating a sleeping NPC feeds (back to stage 1). `vampirism test stage = 1` to `4` starts you as a vampire at that stage. A pack setting, so not set in the profile. Nothing shows with Morrowind.
- *Sneaking* (0227): `[extension.skyrimse] sneak = true` (with `[Game] neutral combat`, `[GUI] rmlui` and the neutral actors) makes the sneak key (Ctrl by default) matter: nearby NPCs judge every half second whether they have noticed you (distance, a clear line, which way they face, your Sneak skill, speed and armour weight), the sneak eye above the middle of the screen shows closed (hidden), half (caution) or open (detected), a melee hit or shot on someone who has not noticed you does 2x to 3x damage with the message "Sneak attack for 3x damage!", Sneak trains from staying hidden and from sneak attacks, and crimes and pickpocketing are only noticed by those who noticed you. A pack setting, so not set in the profile. Nothing shows with Morrowind.
- *Skyrim-style combat HUD* (0244): `[extension.skyrimse] hud style = true` (with `[Game] neutral combat`, `[GUI] rmlui` and the neutral actors) replaces the plain combat bars with the player's framed health, magicka and stamina bars at the bottom left (the lost part of a bar stays pale for half a second and then drains), a wide bar at the top centre for a boss (an actor whose editor id holds `boss` or starts with `dragon`) or else the enemy fought last, and small bars with name and level over the heads of up to eight engaged actors (allies green); with `sneak` and `map` also on, Morrowind's sneak icon and minimap are hidden. A pack setting, so not set in the profile. Nothing shows with Morrowind.
- *Furniture use* (0135, S6b): with *Actor packages* on, sleepers walk to a bed and lie in it at night, eaters and sandbox NPCs (with the package's "allow sitting" input on) sit on free chairs, benches and stools for a while and get up again; `[extension.skyrimse] actor sleep snap` (default true) puts a sleeper on the bed when the walk fails. Needs `neutral actor idles` for the pose. Nothing shows with Morrowind.
- *Neutral actor idles* (0132): `neutral actor idles` makes those NPCs loop the idle clip decoded from the game's own `.hkx` files (root motion ignored). Needs `neutral actors`.
- *Neutral combat* (0170): `neutral combat` calls the pack's combat hooks once a frame and plays the clips it asks for; visible only with `[extension.skyrimse] combat test swing = <editor id>` (for example `Hulda`), which makes that NPC swing bare-handed every few seconds. Needs `neutral actor idles`. Task 0171 adds the melee rules (a HitFrame asks for a sweep, damage by the weapon, skill and armour, a hit reaction, death at 0 health) with the pack test settings `combat test hostile = <editor id>` (that NPC attacks the player when in reach), `combat test player swing = <seconds>`, `combat test weapon = <WEAP editor id, for example IronSword>` and `combat trace = true`; the player takes damage from the engine health (the HUD bar). None of them is in the profile: they are proofs of the contract. Task 0172 (S5-4) adds block, power attack, bash and stagger for the one-handed sword with the pack test settings `combat test power every = N`, `combat test bash every = M` (every Nth or Mth attack of a hostile actor) and `combat test block = <editor id>` (that actor blocks while free), also not in the profile; and the player's real attack key (a tap attacks, a hold of half a second is a power attack) when `neutral combat` is on in a Skyrim session; there is no block key (OpenMW has no block action). Task 0192 adds `combat test crowd = N`, `combat test duels = M` and `combat test crowd delay = <seconds>` (the N nearest actors attack the player, the next 2M fight in pairs): proofs as well, not in the profile.
- *Held gear* (0220): `[extension.skyrimse] held gear` draws the melee weapon and shield a Skyrim NPC fights with (the choice of `combat real gear`): the weapon waits at the skeleton's sheath node while the actor is at peace and is in its hand while it fights (8 s after its last combat clip), the shield stays on its arm. Also, independent of the setting, an outfit that is one use-all leveled list (a guard's armour) is worn in full. Needs `neutral actors`; nothing shows with Morrowind.
- *Neutral actor locomotion* (0148): `neutral actor locomotion` makes a Skyrim NPC that moves play the walk or run cycle of its sex (`mt_walkforward`, `mt_runforward`) blended with the idle by its measured ground speed; the cycle rate follows the speed through the clips' own root motion. Needs `neutral actor idles`. Only visible when the pack moves an NPC (`actor test walk` or `actor test goto`).
- *Neutral player body* (0222): `neutral player body` (with the pack setting `[extension.skyrimse] player body`) draws the player in third person as a Skyrim actor: race Nord, the sex of `player female`, the clothes and armour he has equipped, the weapon and shield he holds, the idle, walk and run clips by his speed and the clip of each swing. First person shows no body. Needs `neutral actors`.
- *First-person arms* (0245): `first person body` (pack setting, needs `player body`) shows the player's arms in first person: the race's first-person arm models and worn sleeves and gauntlets on the first-person skeleton, the weapon in the hand once drawn, the arms' own idle, swing and cast clips; the camera sits on the rig's camera node and the arms turn with the view. Shows nothing without `neutral player body` and `neutral actors`.
- *Character creation* (0225): the pack setting `[extension.skyrimse] character creation` (needs `menus`, `player body` and `[GUI] rmlui`) opens an RmlUi window at the start of a Skyrim game over the paused world: the ten playable races with their text and starting skills, the sex, hair, facial hair and eyes of the race, a name; the player's body in front of the camera follows each choice. Done keeps the choice in the save (chunk `character`), gives the race's skill bonuses to the progression and its abilities to the combat rules.
- *Neutral actor process levels* (0154, S7): `neutral actor process levels` makes a Skyrim NPC far from the player step its body less often (within 4096 units every frame, within 8192 every 1/20 s, beyond every 1/5 s) with the skipped time handed over, so the path is the same in bigger strides. Needs `neutral actors`. Only visible in a crowd (Whiterun's streets); an interior is all near. `[extension.skyrimse] player level` (default 1) is the stand-in player level for NPCs that scale with the player; the numbers and relations of each NPC appear in the log as `NACTVAL` lines with `OPENEDEN_NEUTRAL_ACTOR_TRACE=1`.
- *Neutral saves* (0177, N6): `neutral saves` lets a Skyrim session be saved and loaded: the save keeps only what differs from the game pack (moved, scaled, disabled, enabled and deleted placements, opened locks, held keys), the player's cell and position, and the pack's own chunks (today the load order stamp, which refuses a load over another order). Save with F5 or the menu and load it again; actors are placed again by their schedule (a fight in progress or a corpse is not saved). `tools/neutral_session_check/saveload.sh` runs a round trip.
- *Spell delivery* (0242): `[extension.skyrimse] combat spell delivery = true` makes a target actor spell (Heal Other) cast at the actor in the sights, a rune (target location) stay where its projectile lands until an enemy comes within 150 units, and an aimed spell with an area (the Fireball, 15 feet) hurt everyone around where it ends. A feature, off by default, not in the profile (the rune glyph stays drawn after it goes off): turn it on with the `combat test player spell = FireballRightHand` test setting, or use a scroll of Heal Other or Fire Rune from the inventory (`inventory test items`).
- *Summons and raise dead* (0250): `[extension.skyrimse] summons = true` (with `combat spell delivery = true`, `creatures = true`, `combat ai`, `combat factions` and `actor packages` for the ally behaviour) makes a conjure spell or scroll (Conjure Familiar, the Atronachs, Dremora Lord, the thralls) place its creature 150 units ahead of you, an ally that follows you and fights your enemies for the spell's duration (60 s for the familiar) and then goes; Raise Zombie and the other reanimate spells raise the nearest corpse in front of you (its level no higher than the magnitude) as an ally for the duration, and it falls dead again where it stands. One conjured or raised thing at a time per caster (`summons max`). A pack setting, so not set in the profile; the test aids are `summons test magicka` (the stand-in player has 100 magicka, Conjure Familiar costs 107), `summons test kill = <editor id>` (kills an actor 3 seconds after the combat data is read, so there is a corpse). Summons are not saved. Nothing shows with Morrowind.
- *Visible spells and beams* (0191): `neutral spell effects` draws the glow cards and cones of a Skyrim spell model (the Firebolt in flight, a ward's body; additive, the palette lookup and animation are not played, particles are not drawn). With the pack settings `[extension.skyrimse] combat spell beams = true` a held aimed spell (Flames, Frostbite, Sparks) and a touch spell work as a ray each frame, and with `combat spell visuals = true` a held beam streams drawn-only copies of its projectile model and a ward shows its body model while it is up. Both pack settings are features, not test aids, but they are off by default and not in the profile (a stand-in look): set them with the `combat test player spell = Flames` (and `combat test player cast every`, `combat test player cast hold`) or `combat test cast = Hulda:WardLesser` test settings to see them.
- *Inventory and containers* (0182, M-S6): `neutral inventory` (with `[extension.skyrimse] inventory`, and `[GUI] rmlui` from the `rmlui` variant) turns activating a Skyrim container into two RmlUi windows, your inventory on the left and the container on the right: click a name to move one, "All"/"Put"/"Take" for the whole stack, "Take all", "Close"; the I key opens the inventory alone. The pack keeps the state (it is saved with `neutral saves`). Task 0184 adds: a dead NPC (killed by the combat test settings) is searched with the activate key instead of talked to, rows of weapons and armour get Equip and Unequip buttons (the equipped melee weapon and the armour change the player's hits and the damage taken; needs `neutral combat`), potions get Use (instant health only), a key item in the inventory opens its lock, and the speech and bribe checks read the gold in the inventory. The pack test aid `[extension.skyrimse] inventory test items = <hex ids with optional :counts>` gives the player items at the start; it is not in the profile. Nothing shows with Morrowind.
- *Quest journal* (0185, M-S6): `[extension.skyrimse] journal` (with `[GUI] rmlui` from the `rmlui` variant) makes J open a journal window in a Skyrim session: the running quests by name, the selected quest's current log entry, displayed objectives and earlier entries, and a `Completed` list. Quests only move with `[extension.skyrimse] papyrus` on. Nothing shows with Morrowind.
- *Compass and world map* (0201, M-S7): `[extension.skyrimse] map` (with `[GUI] rmlui`) shows a compass strip at the top of a Skyrim session (the cardinal points, the map markers near the player as `O` when found and `?` when only hinted, quest targets as `!`, and the name of the marker in front); M opens the world map (the land of the worldspace by cell, the discovered markers, quest targets and the player; Escape or Close leaves). A marker is discovered by walking within 4096 units of it. Nothing shows with Morrowind.
- *Crime* (0204, M-S6): `[extension.skyrimse] crime` makes the player's crimes count in a Skyrim session: taking from an owned container or hitting or killing a calm NPC in sight of a witness costs bounty in the witness's crime faction (the hold), stolen goods are marked and merchants refuse them, and a guard of the hold walks up and offers pay, jail or resist (a small window with `[GUI] rmlui`; resist needs `combat ai`). Bounty and stolen marks survive save and load. Nothing shows with Morrowind, which keeps its own crime system.
- *Night Eye* (0257): `[extension.skyrimse] night eye = true` with `[Game] neutral image space effects = true` (needs `hdr`, `neutral world` and `neutral combat`) draws Skyrim's Night Eye view while the effect runs: the game's own image space modifier fades in over about 4 s (blue tint, desaturated, brighter), is held, and fades out. Cast the Khajiit power (or a potion). Nothing shows with Morrowind.
- *Barks* (0231, M-S7): `[extension.skyrimse] barks = true` makes Skyrim NPCs speak without being talked to: a hello when the player comes within about 400 units, idle chatter every 10 to 20 seconds within about 800, and in a fight (needs `combat ai`) a cry when it starts, blow cries and taunts on the game's own timers, a hurt line, a cry when fleeing, a death cry and a line when the fight ends. Needs `fallback-archive=Skyrim - Voices_en0.bsa`: a line with no voice file is not said. `barks subtitles` (default true) shows the text as a message box. Nothing shows with Morrowind.
- *Music* (0209, M-S7): `[extension.skyrimse] music = true` (and `music seed`, default 1) makes a Skyrim session play Skyrim's own music instead of whatever the Morrowind playlist script finds in the `music/` folders: the music type of the cell (its XCMO), else its location, else its region, else its worldspace; the battle type while an AI fighter fights the player (needs `combat ai`); a track of the type whose conditions pass at the game hour, picked by the seed; the type's fade when it changes. Needs `fallback-archive=Skyrim - Sounds.bsa`. The log lines start with `Neutral music:`. Ducking, finales and cue points are read but not played. Nothing changes with Morrowind or with the setting off.
- *Menus and loading tips* (0207, plan 4.24): `[extension.skyrimse] menus` (with `[GUI] rmlui`) draws the main menu (Continue, New Game, Load Game, Settings, Quit) and the pause menu (Escape: Resume, Save Game, Load Game, Settings, Quit) of a Skyrim session with RmlUi instead of the engine's own, and shows a loading screen tip (an LSCR text chosen by its conditions, seeded) on a black loading screen. Settings opens the engine's own settings window. Needs `neutral world`; saving needs `neutral saves`. Morrowind keeps its menus.
- *Real weapons and shields* (0214): `[extension.skyrimse] combat real gear` makes a fighter use the best melee weapon and the shield of its inventory and outfit (highest base damage, then lowest form id), play the clip set of that weapon (one-handed, two-handed sword, two-handed axe or hammer) and, with a shield, block and bash with the shield clips and a block bonus; the player's equipped weapon of any melee type does the same. Needs `neutral combat`; visible in a fight (`combat test hostile`, `combat ai`). `combat test weapon` still overrides every NPC's weapon. Dual wielding is not built.
- *Neutral lip sync* (0146): `neutral lip sync` makes a speaking Skyrim NPC move her jaw (the head's `Aah` and `BigAah` morphs from the race `.tri`) with the loudness of her voice line. Needs `neutral actors`.
- *Actor hooks* (0135 S3): not in the profile. `[extension.skyrimse] actor test walk = <editor id>` (default empty) makes that NPC pace; it only proves the pack-to-engine movement contract. Try it with `tools/neutral_session_check/check.sh ... n5walk` and `OPENEDEN_NEUTRAL_ACTOR_TRACE=1`.
- *Locomotion request* (0135 S4): not in the profile. `[extension.skyrimse] actor test goto = <editor id>,<x>,<y>,<z>` (default empty) sends that NPC along a navmesh path to the point and the pack is told Arrived, NoPath or Blocked; it only proves the pack-to-engine go-to contract. Try it with `tools/neutral_session_check/check.sh ... n5walk` and `OPENEDEN_NEUTRAL_ACTOR_TRACE=1` (`NACTGOTO` lines).
- *Neutral weather* (0127, W-S1): `neutral weather` runs a game pack's weathers in the weather manager (Skyrim SE: the sky, fog, ambient and sun colours of its 100+ weathers by time of day, one cloud picture, wind, regional weather lists and transitions; no rain, snow, thunder or sound yet). Needs `neutral world` and `game=skyrimse`; nothing shows with Morrowind.
- *Neutral pathfind half extents* (0134): `neutral pathfind half extents` is the size of the agent the navmesh of a game pack's neutral cell (Skyrim SE) is built for and the default agent of `nearby.findPath` there; 18 x 18 x 66.5 passes the Bannered Mare's stairs, Morrowind's 29 does not. Nothing shows with Morrowind.
- *Neutral weather, more sky* (0127, W-S3): `neutral weather cloud layers` draws up to that many more cloud pictures over the first (each is another sky dome; 0 is the single picture), the climate's sun and sun glare pictures replace Morrowind's and a climate without a moon hides it; `neutral weather image space` applies the weather's image space brightness as exposure (needs `hdr`). Nothing shows with Morrowind.
- *Lit moons* (0167): `[Rendering] lit moons` draws Morrowind's moons (Masser red, Secunda grey) on the night sky; the moon shader multiplied their picture by an emissive colour nothing set, so they were invisible. A game pack's weather always draws them. Off: invisible as before.
- *Neutral world materials* (0130): `neutral world materials` makes Skyrim SE static meshes use their normal map (with tangents), glow map, specular and emissive values, and with `pbr` on turns glossiness and specular colour into roughness and reflectance. Needs `neutral world` and `game=skyrimse`; nothing shows with Morrowind.
- *Neutral havok collision* (0144): `neutral havok collision` builds Skyrim SE collision from the `bhk` shapes in each NIF (compressed meshes, convex hulls, boxes) instead of the visual triangles; models without usable `bhk` data keep the visual-triangle collision. Needs `neutral world` and `game=skyrimse`; nothing shows with Morrowind.
- *Neutral tree sway* (0136): `neutral tree sway` makes Skyrim SE trees (shapes with the Tree_Anim flag) bend in the weather's wind: crowns move, feet stay. Needs `neutral world` and `game=skyrimse`; nothing shows with Morrowind.
- *Neutral terrain layers* (0149): `neutral terrain layers` draws the alpha layers (ATXT and VTXT) of each Skyrim SE terrain quadrant over its base texture, so roads, paths and ground transitions show. Needs `neutral world` and `game=skyrimse`; nothing shows with Morrowind.
- *Object animations* (0232): `neutral object animations` plays the NiControllerSequence "Open" and "Close" of a Skyrim door or chest model: a door swings on its hinge for a walking NPC or when the player opens it, a lid lifts while the inventory window is open. A model with no such sequence keeps the old behaviour (a door sinks into the floor). Needs `neutral world` and `game=skyrimse`.
- *Neutral grass* (0153): `neutral grass` grows Skyrim SE grass on the exterior land: each ground texture names its grass (GRAS records), the pack places the tufts by the dominant texture, slope and water, and the engine draws them through the groundcover path within `[Groundcover] rendering distance` (6144 by default; it does not need `[Groundcover] enabled`). Needs `neutral world` and `game=skyrimse`; nothing shows with Morrowind.
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
- *Riding* (0251): `[extension.skyrimse] riding = true` (with `creatures`, `player body` and the engine's `neutral actors`, `neutral player body`) lets the player mount a horse by activating it: the player's body sits on the saddle and plays the `horse_rider` clips, the controls drive the horse (forward walks, run runs at the horse's own speeds, the turn controls and the view turn it), the third person camera follows, and the jump control (E) or activating the horse again gets off beside it. `horse test owned` names the owned horse (editor id, or `*`): buying a horse is not built. No combat, saving or first person while riding.
- *Werewolf* (0259): `[extension.skyrimse] werewolf = true` (with `creatures`, `player body`, `character creation` for the `P` power key, `inventory` for feeding, and `[Game] neutral combat`, `neutral actors`, `neutral player body`) lets the player use the power Beast Form: his body becomes the werewolf in third person with its own idle, walk, run and claw clips for 150 s (`werewolf test seconds` changes it), gear and the inventory window are refused, activating a corpse feeds (adds 15 s for an animal, 30 s for a person, heals 50) and the form ends with "You return to your human form." The form, its time, the corpses fed on and the grant are saved in a chunk (task 0265): a load in a new process is the werewolf again. The Companions quest is not built: `werewolf test grant = true` gives the power. A pack setting, so not set in the profile. Nothing shows with Morrowind.
- *Paired mount and dismount* (0267): `[extension.skyrimse] paired animations = true` (with `riding`) plays the game's own paired clips: the horse is turned to have you on its left flank, you and the horse play the mount clip (1.7 s; the clip puts you on the saddle) and then the ride begins; the jump control (or activating the horse) plays the dismount clip (2.1 s) and puts you on the ground 112 units to the horse's left. A clip that cannot be read mounts and dismounts at once as before. Off, mounting is instant.
- *Werewolf* (0259): `[extension.skyrimse] werewolf = true` (with `creatures`, `player body`, `character creation` for the `P` power key, `inventory` for feeding, and `[Game] neutral combat`, `neutral actors`, `neutral player body`) lets the player use the power Beast Form: his body becomes the werewolf in third person with its own idle, walk, run and claw clips for 150 s (`werewolf test seconds` changes it), gear and the inventory window are refused, activating a corpse feeds (adds 15 s for an animal, 30 s for a person, heals 50) and the form ends with "You return to your human form." The Companions quest is not built: `werewolf test grant = true` gives the power. A pack setting, so not set in the profile. Nothing shows with Morrowind.
- *Buying houses* (0261): `[extension.skyrimse] houses = true` (with `papyrus = true`, `inventory = true`, `services = true` and the Papyrus-driven dialogue; `crime` and `rest` for the ownership rules) starts the steward-offer quest HousePurchase and lets the game's own scripts run a purchase: with the Whiterun jarl's goodwill Proventus Avenicci offers Breezehome (5000 gold, "I'd like to purchase a house in the city."), "I'll take it." takes the gold, gives you the key (the interior door opens only with it) and the Home Decorating Guide, makes the cell the player's for the pack's trespass, theft and bed rules, and the offer is replaced by "I'd like to decorate my home." (kitchen, bedroom, ... for gold; the furnishings are there the next time you enter the house). Saved. A pack setting, so not set in the profile; the test aid is `houses test permission = true` (the real condition is Friend rank with the jarl, reached by the jarl's favours: nothing in this build gives it). Nothing shows with Morrowind.
- *Skyrim main quest opening* (0268): `[extension.skyrimse] main quest start = true` (with `papyrus = true`, `actor packages = true`, `journal = true`, the voice archive and `[Game] neutral world`, `neutral actors`) makes a NEW game start on the cart at Helgen (not at `--start`) and starts the game's own quest MQ101 "Unbound": its stage 10 puts the cast at the cart (the pack makes actors and objects again near the player where the world cannot move them), the quest's cart-ride scene `MQ101Scene1` plays line by line with subtitles and voices, and the journal lists Unbound once an objective shows (stage 160; the aid `main quest test stage = 160,10` sets a stage after the given script seconds). The cart does not move yet. A pack setting, so not set in the profile; `tools/skymainquest_check/check.sh` is the check.
- *Companions, the start* (0264): `[extension.skyrimse] companions = true` (with `papyrus = true`, `actor packages = true`, `combat ai = true`, `[Game] neutral actors`, `neutral combat` and the Papyrus-driven dialogue) lets the Companions quest C00 "Take Up Arms" run through the game's own scripts, scenes and dialogue in Jorrvaskr's basement: ask Kodlak "I would like to join the Companions.", the argue scene plays, Kodlak forcegreets you (a ForceGreet package opens a conversation) and "I have much to learn." starts his dismiss scene, which sets stage 20 (journal: "Take Up Arms"). `companions test training = true` stands in for the walk to the training yard: it starts Vilkas' training quest, whose own script makes him fight you; hit him three times and the fight ends (an essential actor that a blow would kill collapses into bleedout instead). A pack setting, so not set in the profile. Nothing shows with Morrowind.
