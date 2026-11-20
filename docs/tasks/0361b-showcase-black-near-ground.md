# Task 0361b: showcase audit item 2, the black ground near the camera and the hard seam

Status: DONE in part (two root causes fixed and shown; the near ground in sun SHADOW is still very dark, see "Not solved").
Source: `docs/research/0149-skyrim-showcase-audit.md` item 2 (`survey3_a.png`, `diag_sheet.png`). Branch `blackground-0361b`
from `753c93599a` (harness `tools/showcase_check` cherry-picked). Windows only, nothing pushed, no Linux gate.

## What was ruled out, with evidence

Frames: `I:\Projects\OpenEden-work\followers\sc61b\` (`base`, `nrm`, `dbg1` to `dbg15`, `fix*`); the group is `diag`
(three poses from `tools/showcase_check/groups/diag.lua`: the seam, a near ground, a black slab).

* **Not the normals.** The decoded VNML are signed bytes and z-up in the data (cells (6,-1), (7,-3), (6,-3), (7,-2), (6,-2):
  minimum z 5 to 105, no non-positive z) and `gbuffer debug view = normals` shows plausible up-facing normals on the black
  ground (`s1.png`). Lambert against the sun is 1 on the black ground (debug band `lambert`).
* **Not back faces, not the vertex colours, not the weights.** The splat weights sum to 1; the vertex colours in the data
  are 0.88 to 0.99 on average; the albedo layer reads the true texture colour.
* **Not a single shadow caster.** `terrain shadows = false` alone, and `object/actor/player shadows = false` alone, leave
  the slab black; with `enable shadows = false` the ground is dim but not black.

## What the shader really computes (debug outputs written into the terrain fragment shader, GLSL, `slang shaders = false`)

On the black slab (pose 3): `sunShadow` 0 (a real shadow: the cliff above it), `diffuse` 0, `ambient` high but the final
colour about 0.002 linear. On the sunlit ground (pose 2): sun diffuse about 1, albedo about 0.015 linear (the Skyrim ground
textures are really dark: tundra, field grass, dirt and river-bed textures average 0.10 to 0.22 encoded, see the table of
114 landscape textures in the stage log), final about 0.02. So: **a dark ground texture times a weak ambient light in
shadow, and a weak ambient light in sun, gave pitch black.**

## Root cause 1: the ambient cube's vertical pair was the wrong way round

The ambient cube (task 0355) takes the record's six colours as `+X -X +Y -Y +Z -Z`. Decoding all 336 weather DALC
records and 589 interior XCLL of `Skyrim.esm` (Python, aggregates): the SIXTH colour is the brighter in 284 of 284 weather
records that differ and in 559 of 560 interiors, and for each weather the six read as a sky dome only in the order
(horizontals), fifth, sixth = (down, up): `SkyrimClear` day: fifth 77/129/136 (teal, the ground's green), horizontals about
100/140/155, sixth 191/214/221 (pale sky); sunrise 80/81/90, 133/136/154, 173/181/188; night 44/58/67, 60/85/100,
65/103/126. The cube sampled the fifth colour for a surface facing UP, so every up-facing surface (the ground, roofs, any
slope tilting up) took the darkest, greenest colour: the red channel of the ground's ambient was 0.045 linear against 0.52
for the sky. Walls (X and Y) were lit right, which is exactly the audit's "ground and slopes crushed to black while walls
are lit".
The Z order is RECALLED-VERIFY against xEdit (the previous note called the order `+Z -Z` from xEdit, also recalled); the
decision is the data's, and the old reading made the sky the colour of the ground.

Fix: `Render::AmbientCube::fromStoredOrder` (`components/render/ambientcube.hpp`) swaps the vertical pair once, at the one
place the cube is written for the GPU (`RenderManager`, the line that builds `viewCube`); interiors and exteriors both go
through it. No shader, no SDK, no golden `.interface` change. Sunlit ground: median pixel (6,2,0) to (20,13,0) and
(9,11,4) to (25,25,15) in the `diag` pose 2.

## Root cause 2 (the seam): partial LAND records of a child world

The pose-2 seam is on the cell border x = 28672: the east cell (7,-3) is Tamriel's land, the west cell (6,-3) is
`WhiterunWorld`'s OWN LAND record. That record has NO `VCLR` (vertex colours) and a `BTXT` for only quadrant 2 of 4; the
ground textures are `ATXT` layers only. Tamriel's (6,-3) has all of them (base `LFieldGrass01NoGrass`, vertex colours mean
227). The terrain source treated the absent fields as the defaults (the default ground texture `dirt02`, white vertex
colours), so the west cell drew gravel-dirt where its neighbour drew grass. Debug view of the splat's layer ids: the west
half has all ids 0 (the default), the east half real layers. Four of the seven `WhiterunWorld` cells with own land
differ the same way.

Fix: `SkyrimTerrainIndex::buildTerrain` takes what the record lacks (vertex colours; the base texture of a quadrant with no
`BTXT`) from the parent world's land of the same cell when the world's parent usage includes Land (the rule the core's
parent walk already uses), keeping the record's own fields and layers (depth-limited, recursion ends on a cycle).
RECALLED-VERIFY: that the game merges per field (it must take them from somewhere: no other source of the base exists).

## Tests (new files only, no existing test edited)

* `apps/components_tests/render/testambientcubeorder.cpp`: 5 tests (the swap, up takes the sixth, the dome order, shadowed
  ground lit by the sky, a slope).
* `apps/components_tests/skyrimse/testskyrimseterraininherit.cpp`: 6 tests on generated plugins (colours inherited, base
  per quadrant, own layers and colours kept, no inheritance without the Land usage, the parent unchanged).

## Results

* Tests: `SkyrimTerrainInherit` red against the old terrain source 2 of 6 (colours, base per quadrant; the other 4 pin what
  must not change); `AmbientCubeStoredOrder` 5 tests were written after `fromStoredOrder` (one first expectation was wrong
  and was corrected), so they are not red-first; the sky-first order is shown by the frames. 165 related existing tests
  (`*Skyrim*Terrain*`, `*AmbientCube*`, `*SkyrimLand*`, weather cube, light) pass, none edited.
* Frames (`I:\Projects\OpenEden-workollowers\sc61b\keep\`): `diag_before_after.png`, `survey3_before_after.png` (the
  bottom rows: near ground that was black is olive/brown with detail), `mare_before_after.png` (interiors: the before
  frames are the older sc60 build, so only a rough comparison; floors a little brighter, ceilings a little darker, no
  regression seen). The first shot of a run still shows the sky colour where the terrain is not loaded yet (a harness
  start-up artefact, also in the base run of an earlier build).
* Morrowind: `worldobjects_check` against `integ2` OK (110 objects), Balmora fixed frame `1c5d47ad0b5dc32d` for both builds,
  `hdr = true` `b451a8a6ca771e79` for both.

## Decisions for toddhow

1. The Z pair of the ambient cube is swapped for interiors too (one mechanism): floors now take the brighter colour and
   ceilings the darker. The data says the sixth colour is the brighter in 559 of 560 interiors; the previous reading
   ("ceiling dark, floor bounce bright") is also possible for a room; exteriors need the sky on top.
2. Partial LAND records inherit colours and base textures from the parent world's land when it lends land.

## Not solved

Ground in the shadow of a cliff (pose 3) is still black: with the cube fixed the ambient on that ground is high, yet the
final value stays about 0.002. The measured chain (albedo 0.016, ambient high, vertex colour and getAmbient about 0.45, total
about 0.12 against 1.7 in sun) leaves a light ratio shadow:sun of 1:14 where the game's is about 1:3, and the ground albedo
(0.02 linear) makes anything below 0.1 black. The next suspects, not tested: the AO pass on the terrain (`ao terrain`;
an `ao = false` run draws no terrain at all in this build, which is a separate bug to look at), the GI exterior bake (not
baked in these runs), and whether Skyrim's terrain shading scales the diffuse (RECALLED-VERIFY, no source read).
