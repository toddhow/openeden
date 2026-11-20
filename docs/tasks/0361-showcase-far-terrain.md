# Task 0361c: showcase far terrain (audit items 4, 10 and 11)

Same hand wrote tests and code; compile-red for the new tests (the API did not exist) and one mutation run (below); no fresh
reviewer; Windows only; local branch `farterrain-0361c`, not pushed. Audit: `docs/research/0149-skyrim-showcase-audit.md`
(its "LOD" items 12 to 14 and rendering item 9). Harness: `tools/showcase_check` (new groups `farlod` and `farlod_min`).

## Findings, with evidence

**Item 4 (far terrain blotchy, a step where it meets the live ground).** The far tile's ground picture is baked by
`bakeAlbedoImage` from the storage's blend map. The neutral baker never switched the storage's splat mode on
(`NeutralTerrain::Storage::setLayerSplat` is called only by `RenderManager::useNeutralTerrain`, from `[Game] neutral terrain layers`),
so the picture held ONLY the base texture of each quadrant, hard edged, while the live ground blends the quadrants' alpha layers
(task 0149). Evidence: `LodTerrainBaker::beginNeutral` builds `NeutralTerrain::Storage` and never called it; a bake with
`neutral terrain layers = false` (which is the old code) gives the recorded 49,991,192 byte pack; with every tile forced
(`terrain tile threshold = 100000`) against all live (`= 0.01`) the ground matches after the fix
(`keep/live_vs_tile_after.png`).

**Item 11 (regular dither dots on distant snow slopes).** Not a fade dither: with `terrain fade time = 0` and with
`object clusters = false` the lattice stays (`keep/variants_fade0_noclusters.png`); with every tile forced it stays at the
same places, so it is in the baked tile picture. Cause: the picture is 128 texels over 2 cells (level 1) and the ground texture
repeats 16 times a cell, so one repeat is 4 picture texels (2 at level 2), and the mip level the bake takes for that footprint
(4 x 4, 2 x 2) still holds a pattern a texel or two wide: a lattice with the period of the repeat. It shows on snow because that texture
has high contrast at low mips.

**Item 10 (far landmarks missing).** NOT reproduced as stated. The object bake now logs what level 1 leaves out: **one** model of
radius 1,000 or more (the Boethiah shrine statue, alpha blended); no model flattens to nothing (the 7,468 "empty" objects of
level 1 are pieces the simplifier collapsed, all small); nothing is excluded as animated, skinned or with particles. A census of the
kinds that stay live (door, activator, furniture, container, flora, item, movable static: 39,443 placements, 1,231 models; a throw-away log,
not kept) shows small things and effects, no building or tower. In the frames Dragonsreach and the city walls show from 40 to 50 thousand
units out (`keep/sheet_before_after.png`, rows 3 and 4) and farms show in the 25,000 up pose. The audit's far poses looked away from
the hold. A different fault showed on the way: a pose far from the start (High Hrothgar, `keep/before_highhrothgar_hole.png`) drew
only the floating mountain statics and no ground for ten seconds (tiles not adopted after a long teleport; not investigated).
No change for item 10 beyond the diagnostics.

## Fix

- `Terrain::Storage::splatBlendSide(sizeInCells)` (new virtual, default 0) says when `getBlendmap` gives a splat map;
  `NeutralTerrain::Storage` answers 17 for chunks of up to 8 cells with the splat mode on.
- `AlbedoSource::splatSide(sizeInCells)` (new virtual, default 0); `StorageAlbedoSource` forwards it.
- `bakeAlbedoImage`: for a splat source the weights and layer indices come from the splat map by the shader's own maths
  (`terrain/main.frag`: four texels blended bilinearly inside one quadrant slot) and every layer is taken as its mean colour
  (the lattice). `albedoInputsDigest`: only the index rows name used layers, and a splat bake adds the words "SPLT", the side and
  `splatAlgorithmVersion` (2), so old neutral packs are rebaked and **Morrowind's digests and packs do not change** (a source with
  `splatSide` 0 takes the old code path).
- `LodTerrainBaker::beginNeutral`: `setLayerSplat(Settings::game().mNeutralTerrainLayers)`. Levels of up to 8 cells (1 to 3) get it; levels 4
  to 6 keep the base layer (the storage's limit; far enough that the difference is small).
- Object baker diagnostics: the largest models a level 1 cluster leaves out, with the reason, and the models that flatten to nothing.
- New groups `tools/showcase_check/groups/farlod.lua` (the 25,000 and 9,000 up poses and far views of Whiterun) and `farlod_min.lua`.

No new setting: the bake follows `[Game] neutral terrain layers` (in the test profile). A pack baked with the setting on and one with it
off are not told apart by the runtime (the manifest has no flag): re-bake after changing it.

## Tests

New file `apps/components_tests/lod/testalbedosplat.cpp` (5): a weight ramp inside a quadrant blends smoothly, the other quadrants stay
layer 0, a splat map of the wrong size is refused, the digest reads layer indices only from the index rows, and a splat picture of a
black and white block texture has no lattice. Written with the code: compile-red against the old API; mutation: with the mean-colour
line disabled the last test fails (1 of 5 red), then restored. `Lod*:*NeutralTerrain*:Terrain*:*Splat*` of `components-tests`: 969 pass.
No existing test edited.

## Measurements (RTX 4070, 1600 x 900, shared machine)

| | before | after |
|---|---|---|
| terrain pack (WhiterunWorld, 3,908 tiles) | 49,991,192 bytes | 69,811,736 bytes |
| terrain bake | 218 s | 255 s |
| object pack | 1,076,581,800 bytes, 320 s | unchanged (not rebaked) |
| tile data held (log line) | 260 MiB | 260 MiB |
| main scene GPU, far_east pose, median | 1.416 ms | 1.273 ms (a different cluster count in that frame; the picture size is the same, so no extra draw cost by construction) |

Frames (kept in `I:\Projects\OpenEden-work\bow6\sc61c\keep`): `sheet_before_after.png`, `dots_before_after_crop.png` (the lattice
patches, left before, right after), `live_vs_tile_after.png` (all live against all tiles, after), `variants_fade0_noclusters.png`,
`before_highhrothgar_hole.png`.

## Morrowind unchanged

`worldobjects_check` against `integ2`'s build: 110 lines, OK. Balmora fixed frame `1c5d47ad0b5dc32d` and with `hdr = true`
`b451a8a6ca771e79`, for this build and for `integ2`'s. The Morrowind pins of `LodTileBake*` pass.

## Not done / decisions for toddhow

- The live terrain still shows the same lattice and blotches at 9,000 up (the dark patches in the middle of `002_aerial9k_s`): it is the ground
  texture's own minified pattern on the live chunks; a fix belongs to the live layer array's mip and sampler handling, not the bake.
- The far tile is not sharper: the splat weights and vertex colours are 128 units a vertex and the picture is 64 units a texel, so a bigger
  picture adds nothing without a detail texture (a shader and twin change). Not enlarged (the pack would grow 4 times).
- Item 10: if there is a specific missing landmark, name it; the census says the baker keeps every large non-mountain static.
- Not run: the Linux gate, clang-format 14, clang-tidy, the Lua tests, a fresh review, a flight with the new tiles (frame time over a walk),
  the High Hrothgar hole.
