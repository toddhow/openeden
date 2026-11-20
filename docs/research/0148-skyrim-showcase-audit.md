# Research note 0148: Skyrim showcase audit (task 0360)

Date 2026-11-19. Audit only, no engine code changed. Build of `081d52c325` (branch `showcase-0360`), RTX 4070, 1600 x 900, fixed frame time
(1/60 s), quiet window, sound off, weather forced to `SkyrimClear` (`OPENEDEN_NEUTRAL_FORCE_WEATHER`), game clock stopped, the five Skyrim SE
masters only. Settings: the living test profile (`tools/testprofile/profile.cfg`) with `menus` and `character creation` off and `combat ai`,
`creatures`, `combat factions` on (`tools/showcase_check/check.sh`). Baked fresh for this run: WhiterunWorld terrain tiles (3,908 tiles) and
object clusters (2,496), interior probes of the Bannered Mare, Dragonsreach, Bleak Falls Barrow and Jorrvaskr, and the exterior sky field of
field cells (2,-2) to (4,0). **Judgement against the real game is from memory (marked "recalled") and from what the frames show; no side-by-side.**

Harness: `tools/showcase_check/` (`check.sh <build> <Skyrim Data> <work> shots <group>`, `groups/*.lua` shot lists, `bakeall.sh`, `sheet.py`).
Contact sheets (all under `I:\Projects\OpenEden-work\papyrus\sc60\keep`):
`sheet_whiterun_day.png`, `sheet_whiterun_lod.png`, `sheet_whiterun_dusk_night.png`, `sheet_anim_guard0.png`, `sheet_anim_player.png`,
`sheet_anim_stop_turn.png`, `anim_run_crop.png`, `sheet_mare.png`, `sheet_fight.png`, `sheet_dr.png`, `sheet_barrow.png`, `survint_sheet.png`,
`diag_sheet.png`, `diag2_sheet.png`, plus every single frame in `keep/<group>/NNN_name.png`.

## What looks good (do not touch)
Day exteriors from the ground (Gildergreen, the plaza and its stairs, `keep/whiterun/000..002`) and the aerial view of Whiterun
(`006_lod_hold_aerial.png`) look like Skyrim: materials, wood and stone, grass, foliage, soft sun, sky, haze depth. Dragonsreach's hall
(`keep/dr/000..003`, `survint_sheet.png` row 2) is convincing: glossy floor, banners, lit table, NPCs in place.

## Findings by area (severity 1 low .. 5 high; effort S/M/L are guesses)

### Lighting (the weakest area for a showcase)
1. **Dusk and night are almost black** (sev 5, M). `e_*` at hour 19.2 is already nearly night (recalled: Skyrim at 19:00 is orange
   twilight; here sunset is 16:00 + 4.5 h by the weather table, so 19.2 should still glow); `n_*` at 23:00 (`012..016`): the street shows only
   point-light pools and a blue wash, the aerial night view (`015`) and the far view (`016`) are black, no moon or star light on the land,
   no ambient floor. Needs a night ambient and moon/star fill, and a look at the dusk table.
2. **Pitch-black ground close to the camera in daylight** (sev 5, M to L). Many ground-level frames have near-field terrain and some slopes
   crushed to black while distant ground and walls are lit (`survey3_a.png`, `diag_sheet.png` column 3). Ruled out by A/B runs
   (`diag_sheet.png`, `diag2_sheet.png`): not baked GI exterior, AO, sun shadows, terrain layers, PBR, parallax or the LOD tiles (the black
   stays with each off). Likely back faces of the terrain seen from inside or below a slope, or the terrain's lighting using a normal that points
   away; needs a render of the terrain normals. Same area, `diag` col 2: a **hard vertical seam** where the near terrain is sharper and
   darker than the part right of it (also with each effect off).
3. **Interiors tint too yellow, volumetric fog far too heavy** (sev 4, S). The Bannered Mare (`sheet_mare.png`) is a uniform yellow-beige
   wash with no contrast; Dragonsreach is better but hazy (`sheet_dr.png`, dark side wall vs washed centre). Recalled: the Mare is warm but
   readable. Volumetrics density/colour or the cell image-space tint is too strong.
4. **Actors in shade are black silhouettes** (sev 3, M). In the fight frames (`sheet_fight.png`) Hulda is pure black against a flat grey wall:
   no ambient/GI on actors, no rim, lit only by direct light. The grey "wall" there is a flat untextured surface (see rendering 7).
5. Torches and fires: the Gildergreen glow and window lights read well by day (`000`); at night the fire glow is a flat cone (recalled: the real
   game has strong flicker and bounce on faces/walls). No fire bounce on nearby actors (sev 2, M).

### Rendering
6. **Morrowind HUD is in every frame** (sev 3, S): bars, hand icon and the Morrowind torch icon in the corner, and the message "Your
   Athletics skill increased to 2" with Morrowind font (`sheet_anim_stop_turn.png`). The pack's `hud style`/`menus` are off in the profile.
7. **Unlit/untextured flat surfaces** (sev 3, M): the grey plane next to Hulda in the fight, the black slab and spikes in `survey2` (007),
   the grey void in Barrow (`sheet_barrow.png`): models drawn flat, or positions outside geometry. Needs a pass on shape types that
   `neutral world materials` does not cover.
8. **Sky** (sev 3, S): cirrus/painted wispy streaks are the dominant sky feature at low pitch (`000`, `004`): they look like brush strokes,
   not clouds, and the fair-weather cumulus is missing; with World LOD off the sky is a flat pale cyan with no clouds at all
   (`diag2_sheet.png` row 4), so the sky only works with LOD on (sev 3).
9. **Dither stipple on distant slopes** (sev 3, S): snow slopes show a regular dot pattern in `lod_far_*` (cluster/tile fade or far-tree
   dither leaking at distance).
10. **Black first-person item** at the bottom edge of some frames (`001_d_market_east.png`): the held light/arm model draws black (sev 2, S).
11. **Contact shadows and AO are subtle**; building edges float on the ground in places (recalled: Skyrim has soft occlusion under eaves) (sev 2).

### LOD
12. **Far terrain is blurry and blotchy** (sev 4, M): at 9,000 up (`007`, `008`) the far tiles show smeared albedo with visible tile-sized
    patches and no detail texture, much lower resolution than the near terrain; the transition to live terrain has a visible tonal step.
    A detail layer or higher tile albedo on level 1 and 2, and matching tint, would be the biggest far-view gain.
13. **Far objects are thin**: the forest and rocks appear as speckles on the slopes (`006`, `009`) but the far hold, Dragonsreach silhouette and
    farms/Rorikstead style landmarks are missing from the far view (sev 3, M): cluster budget/size cutoffs hide landmarks.
14. **Aerial haze** works (`lod_far_north`) and hides the ends; the horizon is slightly muddy in LOD-on versus clear sky with LOD off (sev 2).
    Whiterun hold fills in quickly after a teleport (about 3 s) but frames right after a hop show the tile fade (not captured as a sequence).

### Animation
15. **Walk and run read as stiff, with little visible stride** (sev 4, M): `anim_run_crop.png` (run, 0.2 s steps): after the first step the
    character stays in the same upright pose with feet barely changing; at third-person distance the leg cycle and arm swing are not readable.
    The inertial lean/skid/stop transitions cannot be seen because the stop (`a_stop`) camera moved; the `walkstop`/`runstart` clips are missing
    (log line 'missing: runstart, walkstop') so starts and stops use the procedural fallback.
16. **Player movement speed looks low for a run** (sev 3, S): the scene shifts little over 2 s of "run" (`sheet_anim_player.png`); check the
    run speed vs the clip stride.
17. **Guard group idles are indistinguishable between frames** (sev 3, M): six guards stand in a stack along a wall in `sheet_anim_guard0.png`
    with near-identical poses across 12 frames spaced 0.5 s; no weight shift, no head turn, all in the same idle phase (identical poses of
    several actors suggests the same clip at the same time: needs a random phase per actor).
18. **NPCs in the Mare stand and walk but do not interact** (`sheet_mare.png` burst: static after the first frame; the bench guard never
    moves) (sev 2).
19. **Fight** (`sheet_fight.png`): Hulda moves through fighting poses (lunge, recoil) correctly, but she is a black silhouette and the camera
    sits behind a flat wall; hit reactions are readable. A fair-weather combat shot needs lighting first (see lighting 4).
20. No ragdoll, kill move or horse in this capture; not judged.

## Ranked top 15 fixes for the showcase (biggest visible gain per effort)
1. Night and dusk ambient: moon/star fill and a proper twilight ramp (lighting 1) - M.
2. Near-ground black patches and the vertical terrain seam (lighting 2) - M/L; find the cause with a terrain normal view first.
3. Reduce the Mare's yellow cast and volumetric fog; check Dragonsreach haze (lighting 3) - S.
4. Far terrain albedo/detail: higher-resolution level 1-2 tile pictures and a far detail layer; match tone at the live seam (LOD 12) - M.
5. Remove the Morrowind HUD and skill messages in a Skyrim session (turn on `hud style`/`menus`, or hide the Morrowind HUD) (rendering 6) - S.
6. Sky: tune cirrus so it does not look like strokes, restore fair-weather clouds, and make LOD-off sky match LOD-on (rendering 8) - S.
7. Ambient/GI on actors so shaded NPCs are not silhouettes (lighting 4) - M.
8. Walk/run readability: verify the pose controller drives the leg cycle at the clip stride; add `walkstop`/`runstart` clips (animation 15,16) - M.
9. Per-actor random idle phase and variation so groups do not move as one (animation 17) - S.
10. Far landmark visibility (Dragonsreach, farms) in clusters (LOD 13) - M.
11. Dither stipple on far slopes (rendering 9) - S.
12. Unlit flat surfaces and void positions (rendering 7) - M.
13. Fire glow with flicker and bounce on faces/walls (lighting 5) - M.
14. Black first-person item (rendering 10) - S.
15. Idle life for seated/standing NPCs in interiors (animation 18) - M.

## Worst shots
`keep/whiterun/016_n_lod_far_south.png`, `015_n_hold_aerial.png` (night black); `survey3_a.png` (black ground); `keep/mare/*`
(yellow wash); `keep/mare_fight/*` (silhouette); `keep/whiterun/007_lod_far_south.png`, `008_lod_far_east.png` (blurry far terrain);
`keep/barrow/*` (outside geometry).

## Not captured / limits
- Bleak Falls Barrow: the spawn point is inside rock and the offsets I tried lie outside the cave (grey void); only the four spawn views
  (`survey_barrow`, `survint_sheet.png` row 3) are usable. Needs real coordinates.
- Walking NPCs in the open: the guards stand in a group; no street NPC was seen walking in the exterior. The only walking NPC frames are the
  Mare's first frames (`sheet_mare.png`) and Dragonsreach (`sheet_dr.png`).
- The player's stop/start transitions: the stop shot's camera jumped; foot sliding was not measured (use `tools/footslip_check`).
- A creature fight, ragdolls, horse, dragon and the Morrowind comparison were not captured. No performance numbers were taken.
- At 1,600 x 900 the characters are small in third person; judge animation from a close crop (`anim_run_crop.png`).
- Everything uses forced `SkyrimClear` and a stopped clock, so weather variety, rain and snow were not audited.
