# Task: Havok animation clip spike (clean-room decoder of Skyrim SE `.hkx` clips)
Design doc section: `docs/research/0054-skyrim-animation-clips.md` (spike plan, section 5), `docs/research/0054-provenance.md`
(the log of every format fact), `docs/design/skyrim-se-implementation-plan.md` 4.11, 4.12 and decision D4.
Goal: find out whether Skyrim SE's vanilla clips can be decoded by our own code, written from the user's data alone, and
turned into poses we can look at; time box 10 to 15 days with a go/no-go after about a week.
Inherited code touched: none. New: `components/havokpack` (formats layer, standard library only), `tools/hkxconvert`
(off by default behind `OPENEDEN_BUILD_HKXCONVERT`), tests in `apps/components_tests/havokpack`. `CI/layer_rules.txt` gets
`dir components/havokpack formats` (no new exception: 104 before and after).
Inputs / outputs: the user's own `.hkx` files in; glTF and pose text out, outside the repository (`hkxconvert` refuses an
output path inside a git work tree). No game data is committed. Tests use packfiles our own generator writes.
Constraints: determinism (no clocks, no randomness), hostile input capped and located, no allocation in `sample()`,
nothing in the engine calls the library yet, so the game is unchanged.
Acceptance criteria:
- [x] The container reader parses all 7,699 installed files (`HavokPackfileRealData`, opt-in)
- [x] `hkaSkeleton`: names, parents, reference pose; checked against the NIF skeleton (95 of 99 names; the other 4 explained)
- [~] Uncompressed clip for ground truth: **does not exist in vanilla** (see the log); replaced by other oracles
- [x] `hkaSplineCompressedAnimation` decodes: 6,126 of 6,126 clips, 6,505 of 6,505 blocks end exactly where the file says
- [x] Converter: glTF and pose text; CreativeMotion clip format not written (glTF is what its reader takes, not tried)
- [~] One idle clip playing in Eden Studio's mesh viewer: **not done**; the pose of `mt_idle`, `mt_walkforward` and
      `mt_runforward` at several times drawn as stick figures from the converter's output (frames viewed)
- [x] Provenance log complete for the facts the code uses
- [ ] The oracle comparison by toddhow's own tool (open: the only independent check of the interpolated values)
- [x] With the library unused by the engine: nothing in the game changes (nothing links it)
Out of scope (float tracks, root motion from the sidecars and annotations became stage 7): `hkaDefaultAnimatedReferenceFrame`, behaviour graphs,
other Havok versions or the 32-bit packfile, the engine's animation system, option (b) (shipping the decoder in the engine).

## Decisions of toddhow (2026-11-11), recorded
1. "Yes, spike it": a clean-room decoder written as a game-neutral library, used first by a converter the user runs on
   their own `.hkx` files with output kept outside the repository; 10 to 15 days with a go/no-go after about a week.
   (Answers question 1 and the recommendation of question 2 of note 0054.)
2. Outside projects: "Licences/READMEs only". Read only the licence files and READMEs of HavokLib, HKX2 and DSAnimStudio to
   judge provenance; use HavokLib's source to cross-check only if its provenance is shown clean by those files; never read
   HKX2's or DSAnimStudio's source; never use the Havok SDK or anything derived from it. (Answers question 3.)

## Stage log

### Stage 1: container (done, 2026-11-11)
`packfile.hpp/.cpp`: header, three sections, class names, local/global/virtual fix-ups, object list, bounds-checked reads.
Tests first (`testpackfile.cpp`, 18 tests run red against a stub whose `parse` throws: 12 failed by their own checks, the
refusal tests passed on a stub for the wrong reason and were rechecked after the implementation), generator `packgen.hpp` is ours. Hostile input: file size,
section count, every offset, every fix-up, class-name offsets, string lengths, caps in `Limits` checked before any
allocation; every truncation and 20,000 random mutations are refused or safe. **Real data: 7,699 of 7,699 files parse**,
116,102 objects, 98 distinct classes (`hkaSkeleton` in 328 files, `hkaSplineCompressedAnimation` and `hkaAnimationBinding` in
6,126, `hkaDefaultAnimatedReferenceFrame` in 13). The first Python prototype refused 142 files (class-name table ended at
a 0xFF tag byte): corrected to the marker byte 9 (provenance F8).

### Stage 2: skeleton (done)
`skeleton.hpp/.cpp` (the tests came with the code: `testspline.cpp`, 3 skeleton tests; mutation checks instead of a red run, below).
Layout found by dumping the first skeleton object's pointer slots and array sizes (F13 to F16). 376 skeletons, 5,575 bones
parse; every parent precedes its child and every rotation is a unit quaternion. Against `skeleton.nif` (extracted
from `Skyrim - Meshes0.bsa`, read as strings only; the NIF is not parsed): 95 of 99 character-skeleton bone names occur in
the NIF; the 4 that do not are `x_NPC LookNode [Look]`, `x_NPC Translate [Pos ]`, `x_NPC Rotate [Rot ]` (animation helper
nodes) and `Quiver`. The ragdoll skeleton (18 bones, `Ragdoll_NPC ...`) shares no node names with the NIF, as expected.

### Stage 3: the uncompressed clip (the ground truth does not exist)
**Finding.** `hkaInterleavedUncompressedAnimation` occurs in none of the 7,699 files, neither in the class table nor as an
object (a byte search for the name finds nothing). Note 0054 section 2.1 counted "2 files": that came from a check on
class-name presence in a different pass and does not hold; the note says so now. Consequently there is **no vanilla
uncompressed clip to give track layout ground truth**. Replacement oracles, all from the user's files:
1. **Block size invariant.** Any wrong layout of any component breaks the equality "track data ends at the float block
   offset"; it holds for 6,505 of 6,505 blocks (F31). This fixed the structure (alignments, 5 byte rotations, interleaved
   mixed masks only partly: it could not tell the order of constants and ranges, F32).
2. **Static tracks against the skeleton's reference pose.** Constant rotations land on the reference rotation to 0.06
   degrees (median), constant positions on the reference translation exactly: this fixed the 40-bit quaternion layout (F33,
   five alternatives rejected by 4 to 150 degrees) and the order of constants and ranges (F32: 2e-5 against 0.42 and 7.2).
3. **Header identities** (duration, block duration, block count: 6,126 of 6,126).
4. **Smoothness and unit norm** (no jump above 50 degrees a frame in a 20-frame run cycle; |q|^2 - 1 below 4e-7).
Not found: a mod file with a real uncompressed instance (not searched; the MO2 mod folders are untouched).

### Stage 4: spline decoder (done)
`animation.hpp/.cpp`: parse (caps, located errors, refuses an unknown quantisation byte, an unknown degree, a block that does
not end where the file says), `unpackQuaternion40`, `sample()` (no allocation). Written by data analysis in Python first
(`I:\Projects\OpenEden-work\havok\tools`, throwaway), then in C++; **the two implementations agree to 5e-4 units on the world
position of every bone of three clips** (same author, so it shows the port, not the truth). **Real data: 6,126 of 6,126
clips decode (6,505 blocks); 1,128 NaN floats (59 clips, F36) are replaced by the track default and counted.**
Tests: `testspline.cpp`, 18 spline tests on files `splinegen.hpp` writes (hand-derived values: a degree-1 midpoint, a
cubic's interior knot value 0.5, block overlap, axis order, bindings, hostile files, 30,000 random mutations). The
animation tests were written alongside the code, not before it (a deviation from the loop); instead the layout facts
were each checked against data and the encoder is independent of the decoder's code (it is the inverse written from the
facts). Mutation checks (each a one-line change to the code, run through the private rig, each caught by named tests): the quaternion
sign bit, the quaternion centre value, the block length (after one test was strengthened: it first escaped), the position
range scaling, the blend weight, the parent array and reference pose offsets of the skeleton, the final alignment, the
hemisphere fix (after a test was added: it first had none), the parent-child rotation order (after a test was added: it
first escaped), the local fix-up bound, the constant-and-animated overlap check, the degree check: 12 distinct mutations, all caught.

### Stage 5: export and the converter (done)
`export.hpp/.cpp` (`toWorld`, `poseAt`, `writeGltf`: one node per bone with the reference pose, one animation sampled at
every frame, buffer embedded, units and axes as stored); `tools/hkxconvert` (`info`, `gltf`, `pose`). 5 tests
(`testexport.cpp`). The CreativeMotion clip format was not written: its glTF reader was not run on our output.

### Stage 6: view (partly done)
Eden Studio's Skyrim path draws a bind-posed static scene (`CSMMesh::SceneModel`, nodes and hierarchy lines); it has no
per-frame pose hook, and the editor was not built here (a full Vulkan/Qt build at `-j 2` is hours). So `mt_idle`,
`mt_walkforward` and `mt_runforward` of the male humanoid are shown as stick figures drawn from `hkxconvert pose`
(front and side, six times each), `I:\Projects\OpenEden-work\havok\out\{idle,walk,run}.png` (frames viewed): an upright
figure that holds still with small breathing and arm motion (idle); alternating leg strides, bending knees and arm swing
(walk, side view). Left and right limbs coloured; nothing penetrates, no spike or flip. The glTF of `mt_idle`
(`out/mt_idle.gltf`, 1.5 MB, 275 frames) was written but not opened in any viewer.

### Stage 7: sidecars, float tracks and the clip export (2026-11-11)

toddhow's decisions of 2026-11-11 (second round): the spike is a **go** as our own clean-room decoder; HavokLib's, HKX2's,
DSAnimStudio's and every other Havok reader's source is **not** read; no Havok SDK and nothing derived from it; an axis a
track does not store defaults to 0 (F35 stands). Scope of this stage: only the three items below. Every new format fact is in
`docs/research/0054-provenance.md` (F37 to F53), marked DATA, RECALLED or REASONING with its evidence.

**1. Root motion and events from the text sidecars** (`components/havokpack/animationdata.hpp/.cpp`; note 0054 section 2.3
was inferred from three samples and is replaced). Settled by data over all 363 files (323 project files, `dirlist.txt`, 39
motion files): project grammar F45/F46, motion grammar F50, event times are playback time so that **animation time = stored time
x playback speed** (F48: 29,658 of 29,658 events fit their animation's duration scaled, 19 do not unscaled, and the 34 events
with speed -1 have negative times), root motion is cumulative displacement/rotation from the start with no row at time 0
(F50, F51: `MT_WalkForward` is one row of 93.448 units along +Y in 1.133 s while the animation's own root bone stays at 0).
The readers are strict (one-line grammar, single-space rows, `std::from_chars`, finite numbers, increasing times, unique cache
indices, caps on files, lines, events, blocks and rows, errors end in `(line N)`); two numbers per generator whose meaning is not
established are kept raw (`mUnknownA`, `mUnknownB`). **Linking:** the sidecars never name an `.hkx`, and the cache index is *not*
an index into the character file's animation names (tested: 327 of 1,177 durations agree, F52), so `linkClip(project, motion,
fileStem, duration)` links by generator name and block duration (1e-3 relative), keeping every matching generator (events) and
the first block found (root motion). **Result on the 6,126 installed clip files:** 4,567 lie in the directory of one of the 39
projects that have clips (the other 1,559 are props, traps and doors, whose projects list no clips); of those **2,390 get root
motion, 2,056 get events, 2,452 get either**; 8,598 of 8,827 generators have a motion block, 7,687 have events. Not linked: a
generator whose name is not its file's name (the behaviour graph holds the real file; out of scope).

**2. Float tracks** (`animation.*`, `skeleton.*`): 2,663 clips have them (10,137 tracks, 2,940 blocks), so they are decoded:
masks `0x03` (one float) and `0x12` (a one-dimensional spline between a min and a max), data after the transform tracks, the block
rounded to 16 bytes with zero padding (F37 to F43); the size invariant holds for all 6,505 blocks (also the 3,565 without floats),
and a block with float data must end on it. Slots: the binding's int16 array at +48 (or track i = slot i) and the skeleton's slot
names at +88 (`hkFade:AnimObjectA`, `hkVis:Weapon`, ...). Values are 0, 1 or 0.99999994 and 112 0-to-1 ramps. `Clip::mFloatBlocks`,
`sampleFloats()`. The glTF does not carry float tracks (not asked for).

**3. Clip export** (`export.*`, `tools/hkxconvert`): `writeGltf(skeleton, clip, name, link)` adds the root motion's *translation* to the
first root bone at every frame (into the root track's translation channel, or a channel of its own on the reference pose: glTF
allows one channel per node and path) and writes every linked generator (name, cache index, playback speed, each event with its
stored time and its time inside the animation) and the root motion's identity to the animation's `extras`. **The rotation rows are
read and described in the extras (`"rotationApplied":false`) but not applied** (not asked for; about 600 of the 5,456 blocks turn).
`hkxconvert gltf ... --sidecar <animationdata/project.txt>` finds `boundanims/anims_<project>.txt` beside it. Seen:
male `mt_walkforward` gives a root that moves 0 to 93.448 along Y over 35 frames and the events `FootLeft` 0.2 s and `FootRight`
0.767 s (half a cycle apart), as the model says.

**Tests** (`testanimationdata.cpp` 20, `testfloattracks.cpp` 11, `testexportmotion.cpp` 7, plus 3 opt-in real-data runs; the older
tests are unedited and pass): written before the code against stub implementations; red run (a private rig,
`I:\Projects\OpenEden-work\havok2\rig`): 34 of the 38 new tests failed, the other 4 can only pass on a stub (three 30,000-mutation
fuzz tests, whose stub throws `Error`, and "an empty link gives the plain output"). `splinegen.hpp` (a test helper) got a float
track encoder; its output for clips without it is byte-identical (the older float test, which writes zero masks, still passes
through the accepted mask 0). Then 81 of 81 pass (the 5 opt-in real-data tests skip without their folders).
Mutation checks, each a one-line change run through the same rig: 6 of the 16 prepared were run before the machine got too slow to finish (about 10 minutes each), all caught by named tests: event time not scaled by the speed, first colon instead of the last, no implicit origin at time 0, duration check off, zero duration accepted, translation times unchecked. Not run: data after a zero flag, no shorter-way rotation, float spline scale, missing alignment before the float range, block rounded to 4 not 16, unknown float mask accepted, root motion not added, reference pose dropped from the new channel, animationTime not scaled (the harness is `havok2/rig/mut.py`).
Opt-in runs (`OPENEDEN_HAVOK_HKX_DIR`, `OPENEDEN_HAVOK_ANIMDATA_DIR`): 7,699 files parse, 6,126 clips decode (6,505 blocks, 1,128
non-finite floats replaced, unchanged), 323 + 39 sidecar files parse with their invariants, the float tracks above, the linking counts above.

**Not run:** the CMake build of `components-tests` and `hkxconvert` (the machine had 5 GB of free commit memory throughout, below the
10 GB bar, so a gated job never started; everything was compiled with MSVC `/W4 /WX` in the private rig, which compiles the havokpack
sources, the new tests and the older ones, not the whole `components-tests`), the Linux gate and GCC `-Werror` (Docker hung),
clang-format 14 (version 19 formatted the new files and the changed `export.cpp`), clang-tidy, a fresh reviewer, sanitizers, the
oracle comparison of toddhow's own tool, the glTF in a viewer, the root-motion rotation in the export.

**Open for toddhow:** (1) the link is by name and duration; the exact link needs the behaviour graph (`hkbClipGenerator`'s animation
path), which is the next format to settle if the 2,452 of 4,567 are too few; (2) rotation in the export; (3) the two unknown numbers
per generator; (4) accept mask 0 for a float track (never seen; there for the older generated test file).

## Go / no-go assessment (written after the container, the skeleton and the layout work, before the converter)
**GO for spline decoding by data analysis, and it has largely succeeded within the first days of the box.**
- Evidence for feasibility, all from the data: the whole structure (container, masks, alignments, 5-byte rotations) is
  pinned by an exact size invariant over 6,505 blocks; the constant tracks (a quarter of all data) reproduce the skeleton's
  reference pose, which validates the numeric layouts independently of the sizes.
- What is not proven, and the reason this is a go and not a done: (1) the **interpolated** values between control points
  have no oracle (no uncompressed clip in vanilla; the check is plausibility: smooth, unit rotations, believable gait). A
  wrong knot interpretation would still produce a smooth curve. Toddhow's oracle conversion with his own tool is the
  check that closes this; (2) the rotation and position **quantisation variants other than 0x45** never occur, so a mod
  or a creature clip using one would be refused, not misread; (3) float tracks, root motion and annotations are not
  read; (4) absent axes default to 0, not to the reference pose (F35): 99.9% moot, a few clips differ.
- Estimate for the rest of option (a): converter to CreativeMotion's clip format and sidecar events (2 to 4 days), a pose
  hook in the viewer or the engine's actor driver (3 to 6, depends on the engine), float tracks and root motion
  (2 to 3), the oracle comparison and fixes (1 to 3), full unit of fuzz (1). Plus or minus 50 percent.
- Provenance risk: none found against our facts (the log names the source of each); the residual risk the note named,
  that a recalled description descends from the Havok SDK, is mitigated by the facts being data-verified, and two of my
  recalled orders were wrong and were corrected by the data (F8, F32), which is evidence the data, not the memory, drove
  the result. The licences of HavokLib/DSAnimStudio/HKX2 were read (README/LICENSE only); none was used.

## What was not run
The Linux gate (Docker hung; the code is standard library only and was compiled only with MSVC `/W4 /WX`), clang-tidy,
clang-format 14 (a local newer version was not run on these files either), the full `components-tests` binary (a private
rig compiled the havokpack sources and tests with gtest; the CMake build of `components` and `hkxconvert` was started in
`I:\Projects\OpenEden-work\havok\build`, see the result below), a fresh reviewer, sanitizers, Eden Studio integration, the
oracle comparison, CreativeMotion's glTF reader on our output.

## Open questions for toddhow
1. Run the oracle conversion (your tool, one idle clip) and give us the file: closes the interpolation question.
2. Read HavokLib's source as a cross-check? Its README and licence do not state its provenance; the only statement is
   a second-hand line in DSAnimStudio's README ("reverse engineered ... entirely"). Recommendation: ask its author or
   skip; the data checks make it optional.
3. Absent-axis default (F35): 0, or the reference pose? Recommendation: 0 until the oracle shows otherwise.
4. Where the pose hook for the viewer goes (a spike-local `SceneModel` per-time pose, or the engine's actor driver).
