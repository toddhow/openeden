# Task: World LOD and streaming, stage S7 (grass)
Design doc section: `docs/design/world-lod-and-streaming.md`, section 10 (grass) and the staging row S7; measurement background in `docs/research/0018-world-lod-and-streaming.md`, sections 1.3 and 5.5; baseline numbers in `docs/research/0025-lod-baseline.md` (task 0054).
Goal: grass that thins out smoothly with distance instead of being cut at one line, whose surviving blades stay the same blades as you move, that is full density at least out to about 15 m, that fades into the terrain's own colour beyond its last blade, and that comes from a placement source that can be a groundcover mod today and something else tomorrow.
Inherited code touched (to be re-read at the commit that starts the stage, nothing is changed by writing this spec): `apps/openmw/mwrender/groundcoverinstances.{hpp,cpp}` (`GroundcoverDensity::isInstanceEnabled`, `collectGroundcoverInstances`, `groundcoverCellsInRange`), `apps/openmw/mwrender/groundcover.{hpp,cpp}` (`Groundcover`, `ChunkBuild`, `buildChunk`), `apps/openmw/mwworld/groundcoverstore.{hpp,cpp}` (`GroundcoverStore::init`), `files/shaders/default/main.vert` (the hard cut: `length((pc.modelview * vec4(inInstanceOffset.xyz, 1.0)).xyz) > scene.groundcoverParams.x`, the `INSTANCE` block near line 144), `files/shaders/lib/groundcover.glsl`, `files/settings-default.cfg` (`[Groundcover]`), `components/settings/categories/groundcover.hpp`.
Inputs / outputs: new `[Groundcover]` settings (all off or equal to today by default); pure maths in `components/render/grassmath.hpp` with a shader twin; a placement-source interface; an optional data file `openeden.grass` v1 (JSON, canonical writer); instance buffers sorted by hash; terrain far tint through the terrain tile data of spec 0055 (grass coverage entry type 0x0013).

Status: DRAFT written 2026-11-02, not approved, nothing built. Full loop when started (renderer, loader). toddhow has not chosen a groundcover mod (decision 0010 item 28: wait), so **no mod is assumed anywhere in this spec**; the vanilla game has no grass at all.

## What is wrong now, in plain language

Morrowind has no grass in its data. Grass in OpenMW comes from a *groundcover mod*: a content file (ESP) whose records are static models placed in cells like any other object, but flagged to be drawn by the groundcover system, which builds one instanced chunk per cell (`Groundcover::ChunkBuild`, `GroundcoverStore`). Two things make it look bad at a distance:

1. **A hard line.** The vertex shader moves every instance beyond `[Groundcover] rendering distance` (6144 units by default, about 88 m, since a metre is 69.99 units, `Constants::UnitsPerMeter`) to a degenerate point, so the grass ends along a circle.
2. **A pattern when thinned.** `[Groundcover] density` below 1 keeps every Nth instance of a cell's list (`GroundcoverDensity` adds the density to a running total and keeps the instance when it reaches 1). Which instances stay depends on their place in the list, so thinning shows rows and, because the list order is the content file's, it can not vary with distance.

## Design

**Three bands around the camera** (radii are settings; the values below are starting values to be tuned against frames):

| Band | Range | What is drawn |
| --- | --- | --- |
| Near | 0 to `near` (default 1050 units, 15 m) | every blade, wind and stomping as today |
| Mid | `near` to `far` (default 6144, today's cut) | each blade is kept when its **stable number** is below the density at its distance; the density falls smoothly from 1 to 0 |
| Far | beyond `far` | no geometry; the terrain carries a grass tint (see below) |

**The stable number.** Each instance gets a number in [0, 1) that is a pure function of the instance itself (a hash of its position quantised to a centimetre-ish grid, plus its model index), never of its place in a list and never of the camera. It is computed in the vertex shader from the instance's offset (so no new vertex attribute and no change to the instance buffers) and by the same function on the CPU (`Render::Grass::instanceHash`, a twin of the shader; a test pins example values, as `testcloudvolumemath.cpp` does for the cloud noise). The blade is drawn when `hash < density(d)`. Because the number never changes, blades disappear one at a time, in no pattern, as you walk away, and the same blades come back as you return.

**Falloff.** `density(d) = 1 - smoothstep(near, far, d)`, raised to a power (starting 1.0) so the falloff can favour the near side. The curve is a function in `grassmath.hpp` with unit tests (monotone, 1 at `near`, 0 at `far`).

**Size compensation.** If only a fraction p of the blades remain, the ground would look bald. Surviving blades are scaled by `min(1/sqrt(p), maxScale)` (linear size; area goes with the square) with `maxScale` starting at 1.5, and each blade's height is faded to zero over the last tenth of the band so that removal is not a pop. These are starting values; spec stage G2 tunes them with frames.

**Far tint.** Beyond the last blade the terrain should look like grass-covered ground. The terrain tile (spec 0055) carries an optional grass coverage map (entry type `0x0013`, key tag `0x13`); the terrain shader multiplies its albedo by a grass colour weighted by coverage, fading in over the last tenth of the mid band so the change from blades to tint is gradual. Until spec 0055's tiles exist, the live terrain composite gets the same tint from a per-chunk coverage computed at chunk build time from the instance counts (G4).

**Placement sources (the data side).** The renderer should not care where blades come from. A small interface, `Render::GrassPlacementSource`, in `components/render` (no game types, no ESM types; a placement is a model name, a position, a rotation about Z and a scale):

    class GrassPlacementSource
    {
    public:
        virtual ~GrassPlacementSource() = default;
        // Placements inside the square [minX, maxX) x [minY, maxY), in world units, in a fixed order. Deterministic: two calls with the same square give the same result. Thread safe (called from the chunk worker).
        virtual void collect(float minX, float minY, float maxX, float maxY, std::vector<GrassPlacement>& out) const = 0;
        // True if the source has placements anywhere in cell (x, y); lets chunks that would be empty be skipped.
        virtual bool hasCell(int cellX, int cellY) const = 0;
    };

Two implementations are specified:

- **(a) Records** (`RecordPlacementSource`): a thin wrapper over what exists, `collectGroundcoverInstances` over the `GroundcoverStore`, i.e. a groundcover content file's cell references with the later file overriding the earlier by reference number. With `thinning` off, the wrapper keeps the old `GroundcoverDensity` behaviour exactly.
- **(b) Procedural** (`ProceduralPlacementSource`, **to be evaluated, not assumed**): scatter from the terrain. The land record of an exterior cell has a 16 x 16 grid of terrain texture indices (`ESM::LandRecordData::sLandTextureSize`, `components/esm3/landrecorddata.hpp`) and a 65 x 65 height grid (`sLandSize`). A data file `openeden.grass` v1 (JSON, canonical writer, round trip byte for byte, located errors) maps a terrain texture to a *grass rule*: density per square metre, scale range, a slope limit, and a model (a model name from the user's installed mods, or one of the built-in generated blade meshes below). The scatter is a jittered grid whose points and jitter come from `instanceHash(cellX, cellY, gridIndex)`, so it is deterministic, needs no random generator and no clock, and each point's height is read from the land's height grid. Texture names that have grass in Morrowind's own data are **not known to this spec** (it has not been looked up in the game's data); a small tool run on the user's own install (esmtool lists the land textures used in exterior cells) would produce the candidate list, and toddhow would pick the rules. The generated blade mesh is a few crossed quads built in code with a colour taken from the terrain's own albedo, so no game asset and no mod is needed to see grass at all.

**Precedence.** If a groundcover content file is enabled, the record source is used and the procedural one is off for the whole world (two sources would double the grass). A setting `[Groundcover] source = records | procedural | auto` (default `records`, so nothing changes for anyone who has a mod; `auto` picks records when a groundcover file is present, else procedural).

**Instance buffers.** Each chunk's instances are sorted by ascending hash when the chunk is built. A draw can then take a prefix: the number of instances whose hash is below the *largest* density the chunk can have at its nearest distance, found by binary search, so the vertex shader never runs for blades that cannot be drawn. The shader's per-instance test stays for the exact result. This moves the cost from the vertex stage to nothing for most of the mid band, and is a cheap step before the compute culling of stage S9.

### Decisions (my recommendation first)

| # | Decision | Recommended | Alternative |
| --- | --- | --- | --- |
| D1 | The stable number | A hash of the instance's quantised position and model index, computed in the shader | An attribute stored per instance (more bandwidth, a format change) |
| D2 | Falloff and compensation | Smoothstep falloff, `1/sqrt(p)` scale capped at 1.5, height fade over the last tenth | Fewer blades with no compensation (bald look), or alpha-fading blades (sorting trouble) |
| D3 | Far tint | From the tile's grass coverage map (0055), tint multiplied in the terrain shader | Scale the density into the terrain texture at build time (costs a re-bake whenever grass changes) |
| D4 | Placement source | The interface above; records now, procedural as an evaluated option | Only records (no grass without a mod) or only procedural (loses what mods authored) |
| D5 | Procedural fallback | Prototype and judge by frames before deciding; keep behind a setting; never assume a mod | Skip it until a mod is chosen |
| D6 | Default | Thinning off, source `records`, so existing setups render the same | Thinning on by default |
| D7 | Grass test data | A generated test content file (written by a small tool using the ESM writer, synthetic placements, licence-free) for instance and draw counts | A third-party mod (needs a licence record and a choice by toddhow, item 28) |

## Stages

- **G0** pure maths: `Render::Grass::{instanceHash, density, scaleFor, heightFade}`, the twin in `files/shaders/lib/groundcover.glsl`, tests including pinned hash values and a test that reads the shader's constants back. No behaviour change.
- **G1** the shader and settings behind `[Groundcover] thinning` (false): the hard cut replaced by the bands; frames at three distances compared with the old path (near band must match).
- **G2** tuning with frame sequences (stepped poses as in task 0054) and the numbers of research note 0025; sorted instance prefix.
- **G3** the interface and `RecordPlacementSource`; the old behaviour through it verified identical (instance lists equal for a generated test file).
- **G4** the far tint, first from chunk-build coverage on the live terrain, then from tile data (after 0055).
- **G5** the procedural source prototype and its evaluation: frames, instance counts, determinism (two runs equal, cells built in different orders equal), cost. The outcome (keep, change or drop) is toddhow's decision.

## Constraints
- Determinism: the number, the scatter and the sort order depend only on the instance or the cell, never on a clock, a thread or iteration order of an unordered container.
- Allocation: no per-frame allocation; the chunk build allocates on the worker thread as today.
- Threading: sources are called from the chunk worker (`Groundcover`'s own thread); everything attached or compiled stays on the main thread as today.
- Off means unchanged: with `thinning = false` and `source = records`, the instance lists and the shader's behaviour equal the current build; compared by an instance-list test and by frames.
- No game or mod assets in the repository; the generated blade is code; tests use generated data.
- New files under `components/` and `apps/openmw` use lowercase letters and digits only.
- Units: distances in game units; the 15 m requirement is 1050 units (`Constants::UnitsPerMeter` 69.99125109).

Acceptance criteria:
- [ ] `grassmath.hpp` tests: hash values pinned, hash uniform within a tolerance over a lattice, density monotone with 1 at `near` and 0 at `far`, compensation capped, height fade; the shader twin's constants equal the CPU's (a test reads the file).
- [ ] With `thinning` off and `source` at its default, the instance lists of a generated test cell are equal to the current build's and a frame of the near band is unchanged.
- [ ] With `thinning` on, walking away along a line removes blades one at a time with no row pattern, and the same blades return (frame sequence viewed, described in the stage log); the full-density radius is at least 1050 units.
- [ ] Instances and draws per frame at 1500, 3000 and 6144 units are recorded from a generated test file (research note 0018, 5.5), with the cost of thinning in the vertex stage.
- [ ] The far tint blends in without a visible line in a frame sequence, with and without tile data.
- [ ] The procedural source: deterministic (equal across two runs and across build orders), its frames and cost recorded, a written recommendation (keep, change, drop) for toddhow.
- [ ] A fresh reviewer read the code; unit tests, the 32 Lua tests, clang-format 14 and the check scripts pass.
Out of scope: choosing a groundcover mod; grass physics beyond the existing stomping; compute-culled grass (stage S9); trees and impostors; a New Vegas `GRAS` scatter (stage S12, which reuses the interface); changing how groundcover content files are loaded.
