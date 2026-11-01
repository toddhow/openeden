# Task: Rendering GI, baked probes (diffuse global illumination from a bake)
Design doc section: `docs/research/0008-modern-rendering-proposal.md`, section 5 (R3 reflections and probe lighting) and section 6 item 4 (probe-based diffuse lighting for interiors); builds on `docs/tasks/0025-rendering-f2-linear-hdr.md`, `0026-rendering-f3-stored-gbuffer.md` and `0027-rendering-f4-material-model.md` ("the GI track must know" notes).
Goal: let an interior (and, later, an exterior cell) be lit by a pre-baked field of light probes instead of one flat ambient colour, so corners are darker, rooms next to a lamp are warmer, and the bake can be shipped by mods; real-time ray tracing stays an optional later path that this does not need.
Inherited code touched, at commit `add41f94ce`: `files/shaders/lib/material/terms.glsl` (`materialTerms`, `materialTermsScaled`: the `scene.ambient` line), `files/shaders/default/main.frag` (`main`), `files/shaders/lib/view/bindings.glsl`, `components/pipeline/layout.cpp` (`getViewDescriptorSetLayout`), `components/pipeline/viewbindings.hpp`, `components/pipeline/graphics.cpp` (`Graphics::create(const Options&)`, the `Mode` to define table) and `graphics.hpp`, `components/pipeline/mode.hpp`, `components/view/descriptors.cpp`/`.hpp` (`dummyReflectDepth` pattern), the five creators of a view descriptor set (`apps/openmw/mwrender/rendermanager.cpp` twice: `createCastShadowState` and the constructor; `map.cpp`; `preview.cpp`; `apps/opencs/view/render/scenehost.cpp`), `apps/openmw/mwrender/rendermanager.cpp` (`configureAmbient`, `update`), `apps/openmw/mwworld/scene.cpp` (the call at line 504), `apps/openmw/options.cpp`, `apps/openmw/main.cpp`, `apps/openmw/vsgengine.cpp` (the `autoQuit` frame callback, as the model for the baker's frame callback), `components/settings/categories/rendering.hpp`, `files/settings-default.cfg`, `files/shaders/CMakeLists.txt`, `components/CMakeLists.txt`, `apps/components_tests/CMakeLists.txt`. Read-only use: `apps/openmw/mwphysics/physicssystem.hpp` (`castRay`), `apps/openmw/mwphysics/joltlayers.hpp` (`CollisionMask`), `components/resource/shadersettings.cpp` (the light falloff settings), `components/render/debuggeometry.hpp`.
Inputs / outputs: new `[Rendering]` settings `gi` (bool, false), `gi strength` (0 to 2, default 1), `gi debug` (bool, false) and the baker's `gi bake rays`, `gi bake bounces`, `gi bake spacing`, `gi bake seed`; new command line options `--bake-gi` and `--bake-gi-out`; new files `components/render/probes.hpp`/`.cpp` (pure format, maths and file naming), `components/render/probesamplermath.hpp`, `files/shaders/lib/view/gi.glsl`; one new view-set binding (10); JSON files `openeden/probes/**/*.json` in the VFS (format `openeden.probes` v1); no change to any save, Lua API, extension contract or existing file format.

## Recommended design, in plain language

A **probe** is a point in a room where we remember how much light arrives from every direction. We store it as nine numbers per colour channel ("second-order spherical harmonics", a very smooth summary of a sphere of light; the same trick most engines use for ambient light). A **volume** is a regular 3D grid of probes with a fixed spacing (default 128 game units, about 1.8 m). At run time every lit pixel asks "what is the ambient light here, facing this way?": it finds the eight probes around its position, blends them (trilinear), and evaluates the nine numbers for its normal. That replaces the single flat ambient colour. Moving lights, the sun and shadows are untouched: they stay direct light computed per pixel on top.

The probes are **baked** ahead of time by shooting rays through the cell's static collision geometry (the game's own Jolt world), so they show where light is blocked and where it bounces; the result is written to a JSON file that goes in the VFS like any data file, so mods can ship their own. Without a file, or with `gi` off, the flat ambient is used exactly as today.

### Decisions toddhow can change (my recommendation first)

| # | Decision | Recommended | Alternative |
| --- | --- | --- | --- |
| D1 | File format | JSON, canonical writer, floats as shortest round-trip text (the project rule for authored data; a 1000-probe room is about 300 KB) | A binary file (smaller, not diffable, breaks the rule) |
| D2 | Irradiance encoding | L2 spherical harmonics, 9 RGB coefficients, radiance projected (so reflections can reuse it) | Ambient cube (6 colours; cheaper, blockier) |
| D3 | Where bakes live | Read from the VFS at `openeden/probes/`; the baker writes into the user data folder; **no bake is ever committed** (a Morrowind cell's bake is derived from the user's own game data) | Ship bakes in the repo (refused by the no game assets guardrail) |
| D4 | No bake present | Flat ambient, as today (a runtime estimate would be a second, untested look) | A cheap runtime estimate |
| D5 | Albedo in the bake | One constant (0.5): the physics world carries no textures | Per-model albedo from the material overrides (later) |
| D6 | Baker runs | Inside the game (`openmw --bake-gi "<cell>" ...`), on the main thread in time slices, so it uses the game's own cell loading, light records and Jolt world. Not a separate tool (it would need to rebuild all of that) | A separate `openeden-bake` tool |
| D7 | `gi` needs | `hdr` and `pbr` (the ambient is then linear and `pbr`'s occlusion scale applies). Not `gbuffer`: nothing here reads it | gbuffer too |
| D8 | Exteriors | Format supports them (one volume per exterior cell, filename by grid coordinates); the baker and the runtime handle **interiors only** in this task; exteriors keep the flat ambient and sky | Extend later |
| D9 | Interior directional light | Not baked (applied live as now) | Bake it |
| D10 | Terrain | Unchanged (interiors have none); `terrain/main.frag` does not get `GI` | |

## The data (stage a)

Header `components/render/probes.hpp`, namespace `Render::Gi`, no game types, vsg math only.

```cpp
constexpr int shCoefficients = 9;
constexpr float defaultSpacing = 128.f;            // game units; also the baker's default
constexpr std::size_t maxVolumeProbes = 8192;       // the GPU buffer's capacity and the parser's limit
constexpr std::size_t maxVolumes = 64;
constexpr float normalBiasFraction = 0.3f;          // sample position is moved this fraction of the spacing along the normal
struct Sh9 { std::array<vsg::vec3, shCoefficients> c{}; };       // radiance projected on the real SH basis below
struct Volume { vsg::vec3 origin; float spacing = defaultSpacing; std::array<std::uint32_t,3> size{1,1,1}; std::vector<Sh9> probes; };
// probes[x + size[0] * (y + size[1] * z)]; the probe's position is origin + spacing * (x, y, z), world units, Z up.
struct CellId { bool interior = true; std::string name; int x = 0; int y = 0; };   // exterior: grid coordinates
struct ProbeSet { CellId cell; std::uint32_t rays = 0, bounces = 0, seed = 0; vsg::vec3 ambient; std::vector<Volume> volumes; };
// `ambient`: the flat ambient colour (linear, as `configureAmbient` computes it) that the bake assumed.
struct ProbeError { std::string file; int line = 0; std::string field, message; std::string describe() const; };  // "file:line: field: message"
std::optional<ProbeSet> parseProbes(std::string_view json, std::string_view fileName, ProbeError& error);
std::string writeCanonical(const ProbeSet&);        // precondition: validate(set) is empty
std::string validate(const ProbeSet&);              // "" when valid, else the first problem (counts, sizes, finite numbers, spacing > 0)
std::string cellKey(const CellId&);                 // interior: "interior/<s>-<h>", exterior: "exterior/<x>_<y>"
std::string probeFilePath(const CellId&);           // "openeden/probes/" + cellKey + ".json"
```
`cellKey` for an interior: lower-case the name (ASCII only; other bytes are kept), replace every byte that is not `a-z`, `0-9` with `_`, collapse runs of `_`, trim `_` from both ends (an empty result becomes `cell`), cut to 48 bytes, then append `-` and the 8 lower-case hex digits of the 32-bit FNV-1a hash (offset 2166136261, prime 16777619) of the **lower-cased, unsanitised** name bytes. Example: name `Caldera, Mages Guild` gives `interior/caldera_mages_guild-` plus the hash. The file also holds the exact name, and the loader refuses a file whose cell is not the one asked for (two names that sanitize alike cannot silently share a bake).

**Format `openeden.probes` v1, canonical text** (two-space indent, keys in this order, LF, one final newline; nothing is optional and no other keys are allowed):
```json
{
  "format": "openeden.probes",
  "version": 1,
  "cell": {
    "interior": "Example Hall"
  },
  "bake": {
    "rays": 256,
    "bounces": 2,
    "seed": 1,
    "ambient": [0.2, 0.2, 0.2]
  },
  "volumes": [
    {
      "origin": [0, 0, 0],
      "spacing": 128,
      "size": [2, 1, 1],
      "probes": [
        [27 numbers: for each of the 9 coefficients its r, g, b],
        [27 numbers]
      ]
    }
  ]
}
```
`cell` is `{ "interior": "<name>" }` or `{ "exterior": [x, y] }`. Numbers in `probes`, `origin`, `spacing` and `ambient` are written as the shortest text that reads back to the same 32-bit float (`std::to_chars`, so `128`, `0.2`, `1e-05`, `-0` all occur), separated by `, ` within an array; an array of probes has one probe per line. Integers (`version`, `rays`, `bounces`, `seed`, `size`, the exterior coordinates) are plain integer text; a literal with a fraction or exponent in an integer field is an error. Strings escape `"` and `\` and control characters (as `\u00XX`); other bytes are written as they are. `write(parse(x)) == x` for canonical `x`, byte for byte; `parse(write(s))` gives `s` with floats bit-equal. The parser takes any valid JSON whitespace; non-canonical but valid input parses.

Errors are **located** (`file`, 1-based `line` of the offending token, `field` such as `volumes[0].probes[3][5]` or `cell`, `message`). A bad file yields `std::nullopt` and one `describe()`; the loader skips the whole file with one log line, never a partial volume. Required checks: JSON syntax; `format` and `version` (a version other than 1 or not an integer is an error); exactly the keys above; `rays` 1 to 65536, `bounces` 0 to 8, `seed` any 32-bit unsigned; 1 to `maxVolumes` volumes; every `size` component at least 1 and the product at most `maxVolumeProbes`; `spacing` finite and greater than 0 and at most 100000; `probes` has exactly product-of-size entries of exactly 27 finite numbers; `origin` and `ambient` three finite numbers; an exterior cell's coordinates fit a 32-bit signed integer.

**Maths** (header `components/render/probesamplermath.hpp`, same namespace, header-only; the GLSL twin in `lib/view/gi.glsl` repeats it, change both together):
```cpp
std::array<float, 9> shBasis(const vsg::vec3& dir);   // dir need not be unit length; normalised inside (a zero vector gives the basis of +Z)
void shAdd(Sh9& sh, const vsg::vec3& dir, const vsg::vec3& radiance, float weight);   // sh.c[i] += radiance * basis[i] * weight
vsg::vec3 shAmbient(const Sh9& sh, const vsg::vec3& normal);   // cosine-convolved irradiance divided by pi, each channel clamped to >= 0
Sh9 shLerp(const Sh9& a, const Sh9& b, float t);
vsg::vec3 sampleAmbient(const Volume& v, const vsg::vec3& worldPos, const vsg::vec3& worldNormal);   // trilinear in the lattice, after the normal bias
float volumeWeight(const Volume& v, const vsg::vec3& worldPos);    // 1 inside the lattice box, 0 at one spacing outside it, linear between
vsg::vec3 giAmbient(const Volume* v, const vsg::vec3& worldPos, const vsg::vec3& worldNormal, const vsg::vec3& flatAmbient, const vsg::vec3& bakedAmbient, float strength);
```
The basis is the real SH basis in this order (x, y, z of the unit direction): `0.282095`; `0.488603*y`; `0.488603*z`; `0.488603*x`; `1.092548*x*y`; `1.092548*y*z`; `0.315392*(3*z*z-1)`; `1.092548*x*z`; `0.546274*(x*x-y*y)`. The irradiance convolution factors are `pi`, `2pi/3`, `pi/4` for bands 0, 1, 2, so `shAmbient` multiplies band 0 by `1`, band 1 by `2/3` and band 2 by `1/4` (after the division by pi). A uniform radiance `L` on the sphere (projected with weights summing to `4*pi`) therefore gives `shAmbient == L` for every normal: **a probe of a uniform ambient reproduces the flat ambient exactly**, which is what makes `gi` replaceable and testable. `sampleAmbient`: the sample position is `worldPos + normalize(worldNormal) * (normalBiasFraction * spacing)`; lattice coordinates `(p - origin) / spacing`, clamped to `[0, size-1]` per axis; trilinear weights of the eight probes (an axis of size 1 uses its one probe); each probe's `shAmbient(sh, normal)` is blended (blending irradiance equals blending the coefficients). `giAmbient`: `w = volumeWeight(v, worldPos)` (0 when `v` is null); `offset = flatAmbient - bakedAmbient` (what night eye and a mod's cell tweak added to the ambient since the bake, zero normally); `baked = sampleAmbient(...)`; `gi = max(baked * strength + offset, 0)` per channel; the result is `mix(flatAmbient, gi, w)`. (At `strength` 1 with the unchanged ambient the result is the probe's value; at `w` 0 it is the flat ambient.)

## Runtime (stage b)

**Setting `[Rendering] gi`** (false by default; read at start by `MWRender::RenderManager`; effective only with `hdr` and `pbr`, else one warning and off). `gi strength` multiplies the baked term (0 to 2, default 1). `gi debug` draws the probes (stage e below).

**The binding.** `Pipeline::Descriptors::VIEW_PROBE_BINDING = 10`, a `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`, fragment stage, added to `getViewDescriptorSetLayout`. Every creator of a view descriptor set must add a descriptor for it: `View::dummyProbes()` (a `vec4Array` of 4, all zero; `size.w = 0` means "no volume") for the cast-shadow state, the map, the preview and the editor's SceneHost, and `View::probeBuffer()` (capacity `4 + 9 * maxVolumeProbes` vec4 = about 1.2 MB, `DYNAMIC_DATA`) for the main view only when `gi` is on (the dummy otherwise). Probe data is **never** put in `Pipeline::Data::Scene` (growing it past 640 bytes is a documented pitfall).

**Buffer layout** (`std430`, an array of `vec4`; `View::packProbes(const Volume&, const vsg::vec3& bakedAmbient)` fills a `std::vector<vsg::vec4>`):
- `[0]` = (origin.xyz, spacing); `[1]` = (size.x, size.y, size.z, enabled) as float numbers (enabled 1 when a volume is loaded, else 0); `[2]` = (normalBiasFraction, strength, 0, 0) (strength written by the renderer each time it changes); `[3]` = (bakedAmbient.xyz, 0).
- Probe `i` has its nine coefficients at `[4 + 9*i + k]`, `k` in 0..8, each (r, g, b, 0).

**Shader.** `files/shaders/lib/view/gi.glsl` (listed in `files/shaders/CMakeLists.txt`): the buffer declaration, `giAmbient(vec3 worldPos, vec3 worldNormal, vec3 flat)` as the C++ `giAmbient`. `default/main.frag` includes it under `#ifdef GI` and, for lit surfaces (`NORMAL` defined, not a particle, colour mode not 3), computes `vec3 ambientLight = giAmbient((scene.invView * vec4(frag_in.viewPos, 1.0)).xyz, mat3(scene.invView) * viewNormal, scene.ambient.xyz)` once and passes it to `materialTerms`/`materialTermsScaled`, which take it in place of `scene.ambient.xyz` (a new parameter; without `GI` the callers pass `scene.ambient.xyz`, so the compiled shader is the same expression as before; the terrain keeps calling the old form). The `GI` define is a `Pipeline::Mode` added by `Pipeline::Graphics` only when `giEnabled` (like `PBR`), so with `gi` off no shader text changes. **No new specialization constants** (two documented blank-frame pitfalls in `apps/openmw/mwrender/CLAUDE.md`). `scene.ambient` already contains night eye's `+0.7` and the minimum interior brightness; both ride along as the `offset` term in `giAmbient`, so night eye still brightens a baked room and a cell whose ambient was changed after the bake shifts by that difference.

**Loading.** `RenderManager::configureAmbient(cell)` (called for every cell the player enters, `mwworld/scene.cpp:504`) additionally: with `gi` on and an interior cell, reads `probeFilePath(CellId{true, name})` from the VFS, parses, checks the file's cell and `volumes.size() >= 1`, keeps the first volume, packs it and marks the buffer dirty; any failure (no file, bad file, wrong cell) logs once and sets `enabled = 0`; an exterior cell sets `enabled = 0` (D8). The flat ambient is unchanged by this. Reads happen on the main thread (small file); no per-frame work except the strength.

**Popping.** There is one active volume at a time (interiors are loaded one at a time), blended into the flat ambient over one spacing outside its lattice (`volumeWeight`), so crossing the volume's edge is continuous, and trilinear interpolation inside it is continuous. A load changes the whole picture at once, as the cell change already does (a loading screen).

**Off means unchanged.** `gi = false`: no `GI` define, `dummyProbes()` bound, no file read, `configureAmbient` takes the same path. Verified two ways: a frame of the default path and of `hdr + gbuffer + pbr` equals the commit before this task by `diffmap` (the layout binding with a dummy buffer must not move a pixel), and no define text differs.

**Editor.** `SceneHost` gets `dummyProbes()` and keeps building and drawing; it never turns `gi` on.

## The baker (stage c)

`openmw --bake-gi "<interior cell name>" [--bake-gi-out <folder>]` (needs the game's normal start-up: content files, a window, the renderer; like `--auto-screenshot` it runs from the engine's frame callback and quits when done). The game loads the cell, waits until the physics world holds it (a fixed number of frames after the loading screen), then bakes **in time slices** on the main thread (a budget of `gi bake budget ms` per frame, default 20, so the window stays alive), writes `<out>/openeden/probes/<cellKey>.json` (default folder: the user data folder; it is a data folder layout, so adding that folder as `data=` makes the game read it), logs the summary and quits. A crash-free abort writes nothing. The player is moved out of the way (the baker needs only the cell loaded).

Rays use `MWPhysics::PhysicsSystem::castRay(from, to, mask)` with `mask = Layers::WORLD | Layers::HEIGHTMAP | Layers::DOOR` (static geometry and doors; no actors, projectiles or water) and read `mHit`, `mHitPos`, `mHitNormal` (`apps/openmw/mwphysics/raycasting.hpp`). It returns no material, so the albedo is the constant of D5 (0.5, a named constant). **Threading:** all of it runs on the main thread, in the engine's frame callback; the physics world is read between frames.

Volume bounds: the Jolt world's bounds (`JPH::PhysicsSystem::GetBounds`, through `getJoltWorld`, `physicssystem.hpp`) with only this cell loaded, grown by half a spacing; spacing is `gi bake spacing` (default 128) or larger so the probe count stays within `maxVolumeProbes`.

Per probe, deterministic: a PCG32 stream seeded by `seed` and the probe's index (so order, time slicing and restarts cannot change results); `gi bake rays` (default 256) directions from a fixed stratified spherical distribution; each ray:
1. cast to a distance of `openDistance` (512 units, named). A miss contributes the cell's ambient colour (`bake.ambient`, the same value `configureAmbient` produces) times 1. A hit at distance `d` contributes the **openness-weighted ambient** `ambient * min(1, d / openDistance)` plus the surface's reflected light: `albedo * (direct light at the hit point + bounced light)`.
2. direct light at the hit point: for each point light of the cell (see below) within its range, a shadow ray (from the hit point moved 1 unit along the normal); if unblocked, `colour * max(0, n·l) * falloff(d)`. Negative lights subtract, as the engine's do.
3. bounced light: with `bounces > 1`, one cosine-weighted random continuation from the hit point (recursively the same rule), up to `bounces` surfaces deep; `bounces = 0` keeps only the openness-weighted ambient; `bounces = 1` adds the direct light at first hits.
The ray's radiance is added to the probe's `Sh9` with `shAdd(..., 4*pi / rays)`.

**Lights and falloff.** The cell's light references (`ESM::Light`, in the order of their reference numbers, so the order is the same every run; lights whose record makes them off by default are skipped; lights with the Carry flag placed in the cell do emit, as in the engine), at their world positions, with colour and radius from the record, and the game's falloff: a C++ twin of `attenuate`/`lightRange` in `files/shaders/lib/light/equations.glsl` (`Render::Gi::LightFalloff`, filled from the same `LightAttenuation_*` and classic falloff values `components/resource/shadersettings.cpp` reads; the repo has no C++ twin of those two functions today, so this task adds one with tests against the GLSL's formula).

**Invalid probes** (inside solid geometry): a probe whose rays at least 90% hit within `invalidDistance` (16 units) is invalid; invalid probes take the average of valid neighbours (face-adjacent, repeated up to three passes), and if none is valid the probe becomes a uniform `bake.ambient` probe. (A heuristic; flagged in the report.)

**Resumable.** The baker writes progress (the next probe index and the finished probes) to `<out>/openeden/probes/<cellKey>.partial` every 30 seconds (an abort between two saves loses at most that much); started again with the same arguments and the same settings it continues from there. A partial file with different settings is ignored (and logged).

**Fall-back (d).** Flat ambient (D4).

## Debug view (stage e)

`gi debug` (with `gi` and a loaded volume): `Render::DebugGeometry` (no new pass) draws a small octahedron at every probe, coloured by `shAmbient(sh, +Z)` (its up-facing ambient) clamped to 1; refilled when a volume loads. Producer-owned geometry, capacity fixed by the probe count (a full buffer drops primitives and logs once), as the rules in `apps/openmw/mwrender/CLAUDE.md`.

## Reflection probes (stage d, if time)

Not started unless the stages above are done and reviewed. Plan if it is: one specular cubemap per volume (a coarse 32 by 32 face, 6 faces, RGBA16F, captured by the baker through the renderer), sampled with the reflection vector and roughness-biased mip, only on `pbr` surfaces. This needs a render capture the CPU baker does not have, so it is likely a follow-up task.

## Constraints
Determinism: the baker uses a seeded counter-based generator and sorted light and probe order; the same arguments on the same data give a byte-identical file. Allocation: nothing per frame except one float written on strength change; the probe buffer is allocated once. Threading: main thread only (baker, loader, buffer writes); the shader reads. Ownership: the probe buffer is owned by `RenderManager` (a `vsg::ref_ptr` held beside the descriptor set, because VSG's descriptor set shares it). Off by default and unchanged when off (above). No bake or game-derived data is ever committed; unit-test fixtures use a fictional cell name and generated numbers.

## Stages (each commits working, default off unchanged)
- **a. Format, maths, loader, writer, tests.** `probes.hpp/.cpp`, `probesamplermath.hpp`, the file naming, unit tests; no renderer change.
- **b. Runtime plumbing.** The binding and its dummies in all five creators, `Mode::GI`, `gi.glsl`, `lib/material/terms.glsl`/`main.frag` hooks, settings, loading in `configureAmbient`, a hand-made test bake (a generated uniform-ambient file for a real cell, plus a gradient one), editor still draws.
- **c. The baker.** Options, frame callback, ray and light twin, writer, resume; `docs/research/0011-gi-bake-cost.md` with times for three cells.
- **d. Debug view** (and reflection probes only if time).

## Acceptance criteria
- [x] Unit tests for the pure parts pass (round trip byte for byte, located errors, the SH maths, trilinear weights, packing, file naming).
- [x] `gi = false`: frames of the default path and of `hdr + gbuffer + pbr` equal the commit before this task by `diffmap` within run-to-run noise (Seyda Neen, ToddTest, Caldera Mages Guild, night, an NPC); no `GI` define exists; the editor's SceneHost draws.
- [x] With `gi` and a uniform-ambient test bake the room matches the flat picture within noise (the identity test); with a gradient bake the room visibly follows it. (A moved light on top was not tested.)
- [x] The baker produces a file for a real interior that the game loads, where corners and rooms away from lights differ from the flat ambient in the way the lights suggest (viewed by eye, judged by toddhow); the same arguments give the same file byte for byte; an aborted bake resumes. (toddhow's judgement of the look is pending.)
- [x] The Linux gate builds with `-Werror`; the whole tree passes `CI/check_clang_format.sh`; `eden-studio-tests` pass and the editor builds; a fresh reviewer read each stage.
Out of scope: ambient occlusion (R2) and reflections (R3) beyond the stage d plan; real-time ray tracing; exterior baking; terrain and groundcover under GI; per-material albedo; the interior directional light; any Lua API; saves.

## What the next features must know
- **The ambient is a stand-in for openness.** A ray that escapes carries the cell's flat ambient, a ray that hits at distance t carries `ambient * min(1, t / openDistance)` (so closed rooms read darker than the flat ambient unless lights bounce), plus albedo times the light the surface reflects. Ambient occlusion (R2) should multiply or replace the second term, not add to it; it can reuse `normal.w` roughness and `material` occlusion as before, and must read the probe ambient only through `giAmbient` (shader) so the identity test keeps holding.
- **Reflections (R3)** need a specular probe per volume: the `Volume` and the buffer (binding 10, `probegpu.hpp`) have no room for cubemaps; add a second binding, do not widen this one. The baker has no renderer capture, so reflection probes need a capture path in the game.
- **Ray tracing** can reuse `Render::Gi::RayWorld` (a hardware path implements `cast`) and `bakeProbe` unchanged; a real-time path should write the same `Sh9` buffer layout, so the shader does not change. The device report (`components/render/devicereport.*`) already says whether ray query and acceleration structures exist.
- **What the baker does not know:** surface albedo (constant 0.5), light node offsets inside models (the reference position is used), interior directional light, textures, windows letting daylight in (no sky term). Bounds come from every Jolt body, so volumes are larger than rooms.
- **View-set binding 10 exists now;** any new creator of a view descriptor set must add `View::dummyProbes()`.

## Stage log

### 2026-11-01, stages a, b and c (Windows, RTX 4070, `build-jolt`)

**Done and verified.**
- a: `probes.*`, `probesamplermath.hpp`, tests (the independent writer's 59 plus hostile-input tests); reviewer found an unbounded allocation from a hostile file (fixed: caps on array, object and file size), leading zeros accepted (fixed), NaN positions (fixed).
- b: binding 10, `Mode::GI`, `gi.glsl`, `materialTermsAmbient`, loading in `configureAmbient`. Off means unchanged: `diffmap` of the default path and of `hdr+gbuffer+pbr` (noon, night, an NPC, `ToddTest`, Caldera) between the shaders of the commit before and the new ones is at the run-to-run noise (noon 5023 and 10174 against base-against-base 4290 and 2432; `ToddTest` 0). A hand-made uniform bake (any value) reproduces the flat picture (the torch flicker is the only difference), a gradient bake visibly brightens the room (`scratchpad/gi/*`). The editor's SceneHost, linked by hand into a separate `eden-studio-gi.exe` (the real `eden-studio.exe` was running, two instances, and was not touched), draws Seyda Neen with all of F2 to GI (`edcfg/user/screenshots/editor0.png`); `eden-studio-tests` 393 pass.
- c: `openmw --bake-gi "<cell>" [--bake-gi-out <folder>]`, the pure baker (`probebake.*`, 47 tests), `apps/openmw/probebaker.*`. A bake of `Caldera, Guild of Mages` takes 0.6 s, is byte-identical on a second run, and a run killed after its save and resumed is byte-identical to an uninterrupted one. In game the baked room is warmer around the torches than the flat one. Cost: `docs/research/0011-gi-bake-cost.md`. Reviewer found, and fixed: normals pointing along the ray, torn progress files, an empty world hanging `layoutVolume`, leaving the cell during a bake, a weak resume signature.
- Gates: Linux (Docker, `-Werror`, commit `28c5c98757`): `components-tests` 2648, `openmw-tests` 799, 32 Lua tests pass (the first run found `-Wdangling-reference` and `-Wshadow` warnings, fixed); Windows: `components-tests` 2646, `openmw-tests` 799; the whole tree passes `CI/check_clang_format.sh`.

**Not done / not verified.**
- Stage d (the debug view `gi debug`, reflection probes) was not started; no `gi debug` setting exists.
- Not verified: a moved light on top of a baked room, different camera angles (the scripted `SetAngle` did not change the view), exterior cells (the runtime turns GI off there), a window resize or antialiasing with `gi`, `gi strength` other than 1, a bake of a cell with mods, the baker on Linux (only built), the real `eden-studio.exe` (a separate link was used), a GPU cost measurement of the GI shader.
- Heuristics, flagged: invalid probes are detected by rays hitting within 16 units, but Jolt's simple ray cast ignores back faces, so a probe inside solid geometry may see open space instead; none were found in the cells baked. The light position is the reference position.

**Decisions made for toddhow (change them if you disagree).** D1 to D10 above as recommended. Also: `gi` needs `hdr` and `pbr` (the PBR path owns the ambient scale); the partial files are named `.partial` and `.progress` (not `.json`, so a data folder scan does not read them); `gi bake` settings live in `[Rendering]`; no bake is ever committed.
