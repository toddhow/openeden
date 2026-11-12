# Provenance log for the Havok clip spike (task 0119, research note 0054)

2026-11-11. Every format fact the code in `components/havokpack` relies on, with how it was established. Rules (toddhow,
2026-11-11): clean-room; no Havok SDK header, source or derived material; only the licence files and READMEs of HavokLib,
HKX2 and DSAnimStudio may be read, and HavokLib's source only if its provenance is shown clean by those files.

**How to read this log.** The first guess for most facts came from my memory of public descriptions of the format
(community documentation, tool READMEs, forum posts). I cannot cite any of them file by file, and some of them may descend
from the Havok SDK, which is why none of them is trusted: every fact below was checked against the user's own files
(`Skyrim - Animations.bsa`, 7,699 `.hkx` files, Skyrim SE 1.6.x, extracted to `I:\Projects\OpenEden-work\havok\hkx`, never
committed) and is marked **DATA** with the evidence. **RECALLED** means a guess from memory that the data has not
tested (none of these is relied on by the code). **REASONING** means derived from other facts. Where the data contradicted my
memory the entry says so: those are the corrections that show the data, not the memory, is the source.

No other source was used: no web page, source file or binary of an outside project was read, except the licence/README
pages in the table at the end.

## Container (`packfile.*`)

| Fact | Established by | Evidence |
| --- | --- | --- |
| F1 Magic words `57E0E057` `10C0C010` at 0 and 4 | DATA | all 7,699 files |
| F2 File version 8 (u32 at 12) | DATA | all 7,699 files; the reader refuses other versions |
| F3 Layout rule bytes at 16: pointer size 8, little endian 1 (then 0, 1) | DATA (guess RECALLED) | the same 4 bytes in all files; a pointer size of 8 matches every array slot being 16 bytes (F12) |
| F4 Section count u32 at 20 (always 3: `__classnames__`, `__types__`, `__data__`), contents section index at 24, offset at 28, class names section index at 32, offset at 36, content version string at 40 (16 bytes, `hk_2010.2.0-r1`) | DATA | all files |
| F5 The header is 64 bytes; each section header is 48 bytes: a 19 byte name, a 0xFF byte, seven u32 | DATA (guess RECALLED) | names found at 64, 112 and 160; the first section's data start equals 64 + 3 * 48 in the file read first |
| F6 The seven u32 are: data start (absolute), local fix-ups, global fix-ups, virtual fix-ups, exports, imports, end (all after the first relative to the data start). The data runs from the start for `local fix-ups` bytes | DATA | the offsets are non-decreasing and inside the file in all files; the first fix-up table starts exactly where the data ends |
| F7 Local fix-ups are (slot, target) u32 pairs inside one section, global fix-ups (slot, target section, target) triples, virtual fix-ups (object start, class-name section, class-name offset) triples; each table ends with 0xFFFFFFFF and is padded with 0xFF | DATA (guess RECALLED) | every fix-up in all files has its slot inside the section's data and its target inside the target section's data; 116,102 objects |
| F8 The class-name table is entries of a 4 byte tag, the byte 9, a NUL terminated name; it ends where byte 4 of an entry is not 9 | **DATA, corrected my first guess** | my first reader ended the table at a first byte of 0xFF and refused 142 files (a tag may begin with 0xFF); testing the marker byte parsed all 7,699 |
| F9 A virtual fix-up's class-name offset points at the first letter of the name (after the 5 bytes) | DATA | all 116,102 objects resolve to one of 98 class names |
| F10 An object's size is not stored; it extends to the next object's start (or the end of the data) | REASONING | objects are contiguous in the data section (object offsets 0, 176, 304, 29280 ... in a clip file); only used as an upper bound |
| F11 A pointer slot is either fixed up or null; a non-zero unfixed slot is an inconsistent file | DATA | 376 skeletons and 6,126 clips are read through `pointerAt` without a violation |
| F12 An array member is 16 bytes: pointer, size (u32), capacity with a flag in the top bit (e.g. `0x80000063` for 99) | DATA | skeleton and animation arrays (size 99, capacity `0x80000063`) |

## hkaSkeleton (`skeleton.*`), offsets from the object start

| Fact | Established by | Evidence |
| --- | --- | --- |
| F13 name pointer at +16 | DATA | resolves to "NPC Root [Root]" in the character skeleton |
| F14 parent indices at +24, an array of int16 | DATA | 99 entries, every parent before its child, in all 376 skeletons (the reader refuses any other) |
| F15 bones at +40, an array of 16 byte records whose first 8 bytes are a name pointer | DATA | the name pointers sit 16 bytes apart; 99 names equal the parent array's length |
| F16 reference pose at +56, an array of 48 byte transforms: translation (3 floats + pad), rotation x y z w, scale (3 floats + pad) | DATA | the array ends exactly where the next member starts (4576 + 99 * 48 = 9328); every rotation has unit length (0.9999999 to 1.0000001); translations are plausible (COM z = 68.9, thigh x = -6.6) |
| Cross-check with the NIF skeleton (`skeleton.nif`, read as strings) | DATA | 95 of the 99 character skeleton bone names are strings in the NIF; the 4 absent are the animation helper nodes `x_NPC LookNode [Look]`, `x_NPC Translate [Pos ]`, `x_NPC Rotate [Rot ]` and `Quiver`. The 18 ragdoll bones carry `Ragdoll_...` names and are not NIF nodes (rigid body names) |

## hkaSplineCompressedAnimation and hkaAnimationBinding (`animation.*`)

| Fact | Established by | Evidence |
| --- | --- | --- |
| F17 type (u32) at +16 is 5 for a spline compressed animation | DATA | 6,126 of 6,126 |
| F18 duration (float) at +20, transform track count at +24, float track count at +28 | DATA | duration = (frames - 1) * frame duration in 6,126 of 6,126 |
| F19 frame count at +56, block count at +60, frames per block at +64 (256 in nearly all, 128, 96, 64, 32 also occur), mask and quantisation size at +68 | DATA | blocks = ceil((frames - 1) / (per block - 1)) in 6,126 of 6,126; mask size = 4 * tracks + float tracks in 5,897 clips and larger in the other 229 (the data starts after it as stored) |
| F20 block duration (float) at +72, its inverse at +76, frame duration at +80 | DATA | block duration = (per block - 1) * frame duration and inverse * duration = 1 in 6,126 of 6,126 |
| F21 block offsets (u32 array) at +88, float block offsets at +104, the data bytes (u8 array) at +152 | DATA | the track data of every block ends exactly at the float block offset (F31) |
| F22 Blocks overlap by one frame: block b covers frames b * (per block - 1) to (b + 1) * (per block - 1), so a sample's local time is `frame - b * (per block - 1)` | REASONING + DATA | follows from F20; mt_idle (275 frames, 2 blocks) has the largest frame-to-frame rotation step 0.63 degrees across the whole clip, including the boundary at frame 255 |
| F27 Binding: skeleton name at +16, animation pointer at +24 (a global fix-up to the animation object), the track-to-bone array (int16) at +32 | DATA | the pointer resolves to the clip; the array is empty in 5,921 of 6,126 bindings and has exactly one entry per track in the other 205 |
| F28 An empty track-to-bone array means track i drives bone i | REASONING + DATA | the humanoid clips have 99 tracks and the skeleton 99 bones; the decoded tracks land on the right bones (static rotations equal the reference pose of bone i, F33) |
| F30 The quantisation byte (first of the four mask bytes of a track) is 0x45 in every track of every block (504,241 of 504,241); it is read as: positions and scales 16 bits per control value, rotations 40 bits | DATA | the sizes this implies reproduce the end of every block (F31); the 8-bit position/scale variants and the other rotation widths are RECALLED and **never seen, so not implemented** (the reader refuses any other byte) |
| F31 The four mask bytes of each track come first (4 * tracks, then the float tracks' masks, up to the mask size): quantisation, position mask, rotation mask, scale mask. Bits 0 to 2 of a position or scale mask say an axis is constant, bits 4 to 6 that it is animated; a rotation mask with any of bits 4 to 7 is a spline, else with any of bits 0 to 3 a single constant, else the identity (the individual bits of a rotation mask do not matter) | **DATA** | **the track data of 6,505 of 6,505 blocks (all 6,126 clips) ends exactly where the file says** (`floatBlockOffsets[b]`, after aligning to 4); this is a strong check, a wrong size for any one component would break it. A first version that read mixed rotation masks (e.g. `0xD2`) per component failed it |
| F31a Data order and alignment, relative to the block start: per track the position, the rotation, the scale; a spline starts aligned to 4 with `u16 items - 1`, `u8 degree`, then `items + degree + 1` knot bytes; the control points of a rotation follow the knots directly (5 bytes each); the ranges and constants of a position or scale start aligned to 4 after the knots and its control points (2 bytes per animated axis) follow them; a constant position or scale and a constant rotation each start aligned to 4 | **DATA** | same invariant as F31 (alignment mistakes shifted the end by 1 to 3 bytes in the first runs; with the alignment above 3,001 then all 6,505 blocks matched) |
| F32 In a position or scale, the axes are stored in x y z order: a constant axis as one float, an animated axis as a `min` float and a `max` float, interleaved; the control values are then 16-bit fractions of (min, max): `value = min + (max - min) * q / 65535` | **DATA, corrected my first guess** | sizes do not tell the orders apart. 643 tracks had both a constant and an animated axis in the humanoid clips: constants read in the axis-interleaved order are within 2e-5 of the skeleton's reference translation in the median, against 0.42 (min/max first) and 7.2 (constants first). The 16-bit scaling: control values of the COM track of `mt_idle` span the reference height (68.94 to 69.11) |
| F33 A rotation control point is five bytes: a 40 bit little-endian integer; three 12-bit fields at bits 0, 12 and 24, each `(field - 2047) * (sqrt(2)/2 / 2047)`; bits 36 and 37 name the dropped component (0 x, 1 y, 2 z, 3 w); bit 38 is the dropped component's sign; the dropped component is `sqrt(1 - sum of squares)`; the three fields fill the other components in x y z w order | **DATA** | the 13 constant rotations of `mt_idle` (bones that do not move) agree with the skeleton's reference rotation to 0.06 degrees in the median (the quantisation); the alternatives I tried give a median angle of 21 degrees (fields reversed), 27 (fields rotated), 157 (index at bit 38) and 8.2 (sign at bit 39) against 4.9 for the chosen layout on the whole idle pose. Unit norm: sampled rotations of 6,126 clips have |q|^2 - 1 below 4e-7 |
| F34 A spline is a clamped B-spline of the stated degree (1 to 3 seen; the reader refuses others) with knots in frame numbers, evaluated by Cox-de Boor; successive rotation control points are put in one hemisphere and the result is normalised | REASONING + DATA | knots are non-decreasing, begin with `degree + 1` equal values and end with equal values (`00 00 00 00 1a 1a 2e ...` for a cubic); control point values are smooth along the knots; no frame-to-frame jump above 50 degrees in a 20-frame run cycle (fast hands), 0.63 in idle. **No independent oracle for the interpolated values exists** (see the assessment): the knots-in-frames reading is consistent with every size check but not proven for the values between control points |
| F35 An axis a track does not store is 0 (position), 1 (scale); a missing rotation is the identity; the reference pose is not substituted | REASONING + DATA | in the humanoid clips 223,474 absent position axes occur, and the reference translation there is within 4e-4 (thigh y, forearm twist x, ...) except a few clips (`NPC L MagicNode`, `Weapon`: about 40 files) where it is not: marginal, noted as an open question |
| F36 59 clips (all `beast...` clips of the character folder) store NaN for the range or constant of some position/scale axes (1,128 floats) | DATA | the reader replaces such a float by the track's default (the whole range for a NaN range) and counts it in `Clip::mNonFiniteValues`; it does not refuse the clip |

## Float tracks (`animation.*`, stage 7, 2026-11-11)

Same rules as above: nothing from an outside source; every fact was checked against the 7,699 installed files (scripts in
`I:\Projects\OpenEden-work\havok2\tools`, throwaway).

| Fact | Established by | Evidence |
| --- | --- | --- |
| F37 Float track data follows the transform tracks' data: it starts at the block's float offset (the file's `floatBlockOffsets[b]`, already used as the end marker of the transform data, F31) and holds the float tracks in order. The float tracks' mask bytes are the bytes at `4 * T + i` of the block, right after the transform masks | **DATA** | 2,663 clips have float tracks (10,137 tracks, 2,940 blocks); in all of them the bytes parse into exactly the tracks the masks name, and everything between the end of the last track and the end of the block is zero |
| F38 The only float masks are `0x03` (11,119 block entries) and `0x12` (112); the reader refuses any other value. A mask `0` (nothing stored, the value is 0) is accepted by analogy with the transform masks: **never seen** (REASONING; accepted only because the older generated test files write zero masks) | DATA + REASONING | histogram over all float tracks of all blocks |
| F39 Mask `0x03`: one `f32` aligned to 4. Mask `0x12`: the spline header of a transform track with one dimension (`u16 items - 1`, `u8 degree`, `items + degree + 1` knot bytes), then aligned to 4 a `min` and a `max` `f32`, then `items` `u16` control values; value = `min + (max - min) * q / 65535`. Evaluated like the transform splines (F34) | **DATA** (the layout is the transform tracks' own, F31a and F32, with one axis) | the 112 splines have non-decreasing knots, `min` 0 and `max` 1, and control values that step evenly (idlebook_onepage: 0, 0, 0, 0, 0.333, 0.667, ...); every sampled value of every clip lies in [0, 1] |
| F40 A block is rounded up to a 16 byte boundary: `next block start - block start = roundup16(end of the data)` and the last block ends at the end of the data array; the padding is zero | **DATA** | 6,505 of 6,505 blocks (2,940 with float data measured at the end of the float data, 3,565 without measured at the float offset); the padding bytes are zero in all of them. The reader enforces it only for a block with a non-zero float mask (older generated files are not padded); the slack is up to 15 bytes, so this checks the float data's size to 16 bytes, not to the byte: the zero padding and the plausible values (F39) are the support for the exact end |
| F41 `maskAndQuantizationSize = roundup4(4 * T + F)` | DATA | 6,126 of 6,126 clips (5,897 equal, 229 rounded up: this is the "larger in 229" of F19) |
| F42 `hkaAnimationBinding` +48 is an array of `int16`, one float slot index per float track; empty in 332 of the 2,663 clips (then track `i` drives slot `i`). `hkaSkeleton` +88 is an array of string pointers (8 bytes each), the float slot names | **DATA** | the slot array has exactly F entries in 2,331 clips and none in 332; the character skeleton holds `hkFade:AnimObjectA`, `B`, `L`, `R`, `hkVis:Shield`, `hkVis:NPC L MagicNode [LMag]`, `hkVis:NPC R MagicNode [RMag]`, `hkVis:Weapon`, and the humanoid clips map their four tracks to slots 4 to 7; 36 skeletons have slots (90 names) |
| F43 Float track values: constants are 1.0 (9,338), 0.99999994 (1,535) or 0 (246); the splines are ramps from 0 to 1. The slot names say fade and visibility, so a float track is a 0 to 1 weight of an attached object: REASONING from the names (the names are DATA) | DATA + REASONING | as F39 and F42 |

## Text sidecars (`animationdata.*`, stage 7, 2026-11-11)

Extracted from `Skyrim - Animations.bsa` to `I:\Projects\OpenEden-work\havok2\side` (never committed; 1,221 `.txt` files in the
archive, of which 324 are under `meshes/animationdata`, 39 under `animationdata/boundanims`, the rest `animationsetdata`,
which this stage does not read). Note 0054 section 2.3 inferred the fields from three samples; this replaces it.

| Fact | Established by | Evidence |
| --- | --- | --- |
| F44 Every file is text with CR LF line ends and a final CR LF, bytes below 0x100 read as Latin-1. `animationdata/*.txt` are 323 project files and `dirlist.txt` (the list of project file names, a different format the reader refuses at line 1); `boundanims/anims_<project>.txt` are 39 motion files. The reader also accepts a lone LF (REASONING, not seen) | **DATA** | all 363 files |
| F45 Project file: line 1 is `1` (323 of 323), line 2 a count N, then N file names (the behaviour files, the character file and the skeleton file, with backslashes), then a flag line `0` or `1`. Flag `0` (284 files): the file ends there. Flag `1` (39 files, exactly the 39 that have a motion file, by name): generator blocks to the end of the file | **DATA** | all 323 files parse with this grammar and end exactly after the last block |
| F46 Generator block (8,827 in 39 files): the name (spaces allowed; some end in `.hkx`), a cache index (unsigned), a playback speed (float), two floats, an event count, that many `name:time` lines, and a blank line | **DATA** | every block of every file; the blank line is always present, also after the last block |
| F47 An event is `name:time`, split at the **last** colon (no installed event name contains one); 29,968 events; the time is a decimal float, exponents like `e-007` occur | DATA | all events parse; none has an empty name |
| F48 Event times are playback time: **animation time = stored time x playback speed** (negative stored times with negative speed). Evidence: unscaled, 19 events lie beyond their animation's duration (`staggerStop:1.77778` at speed 0.75 in a 1.33333 s animation: 1.77778 x 0.75 = 1.33333); scaled, all 29,658 events of generators with a motion block lie inside [0, duration]; a footstep pair of `MT_WalkForward` (`FootLeft` 0.2, `FootRight` 0.767 in a 1.133 s stride) sits half a cycle apart | **DATA** | 29,658 of 29,658 (the other 310 events belong to generators without a motion block, which have no duration to compare with) |
| F49 The two floats after the speed are `0 0` in 8,728 of 8,827 generators; the rest are like `0.333333 0`, `0.018 0`, `0 0.1`. Their meaning is **not established**. RECALLED (not tested, not used): the crop amounts of the behaviour graph's clip generator | NOT ESTABLISHED | the reader keeps them as `mUnknownA` and `mUnknownB` |
| F50 Motion file: blocks to the end of the file; a block is the cache index, the duration, a translation row count, that many rows `time x y z`, a rotation row count, that many rows `time x y z w`, and a blank line. Cache indices are unique within a file. Times strictly increase; every block has at least one row of each kind; **no row is at time 0**; the last time of both lists equals the duration; times are on the 30 Hz grid; rotations are unit quaternions; the duration is positive | **DATA** | 5,456 blocks: first time above 0 in 5,456, last time equals the duration in 5,456, 10,062 of 10,062 translation times on the 30 Hz grid, 8,158 of 8,158 rotations of unit length. The reader enforces the structure, increasing non-negative times and a positive duration; the rest are checked by the opt-in real-data test only |
| F51 The rows are **cumulative root motion** measured from the start of the animation: `MT_WalkForward` has one row `1.13333 0 93.448 0` (one stride along +Y in Havok units, 82 units per second), `MT_RunForward` `0.633333 0 222.219 0` (351 per second); the animation's own root bone does not move (the male `mt_walkforward.hkx` root track is constant 0 over the clip), so the displacement is stored only here. Between rows the value is linear and **at time 0 it is zero / the identity**: REASONING (no row is stored at 0, F50, and the single-row walk clip is the whole displacement) | DATA + REASONING | the walk and run numbers above; 1,568 single-row blocks move, 617 single-row rotations turn |
| F52 The sidecars never name the `.hkx` a generator plays, and the cache index is **not** an index into the character file's animation name list: the 1,656 `hkbCharacterStringData` animation names of `DefaultMale` (an array of string pointers at +48 of that object) agree with the motion durations by index for only 327 of 1,177 blocks and the order drifts after the first Hearthfire (`BYOH`) entries. So the link is by **name and duration**: a generator whose name equals the file stem (case-insensitive, `.hkx` ignored) and whose block duration equals the clip's duration within 1e-3 relative (a generator without a block cannot be checked and is kept). On the installed data this links **2,390 of the 4,567 clip files that lie in a project's directory to root motion, 2,056 to events, 2,452 to either** (6,126 clip files in all; the other 1,559 are props, traps and doors, which have no clip list) | **DATA** (negative result for the index; the counts are the opt-in test's) | duration check by name alone before the rule: 2,372 agree and 23 disagree in the projects where the directory was found, so about 1% of name matches are another animation; male and female share one directory and their motion files differ |
| F53 A name can match several generators (TurnRight60, Fast and Slow share one cache index with different speeds and events): root motion belongs to the file, events to each generator, and the link keeps all of them | DATA | project files |

## What is not established

- **`hkaDefaultAnimatedReferenceFrame` (root motion, 13 files)** is not read: root motion comes from the text
  sidecars (F50, F51). Float tracks are decoded (F37 to F43); annotation events come from the sidecars (F46 to F48).
  Not established: the two floats after a generator's speed (F49), the link from a clip file to its generator by
  anything but name and duration (F52), the meaning of float mask bits, root-motion rotation in the glTF export.
- The values between control points (F34) have no oracle: vanilla Skyrim SE has **no uncompressed clip** to compare
  against (`hkaInterleavedUncompressedAnimation` does not occur in any of the 7,699 files; note 0054 counted two, from a
  class-name presence check that does not hold on the data: see the task log). Toddhow's oracle step (convert the same
  clip with his own tool) is the independent check that is still open.
- Unit scale and axis convention: values are as stored (Z up, the same unit as the NIF skeleton: `NPC COM` z = 68.9).
- Rotation and position quantisation types other than 0x45, other packfile versions, tagfiles, 32-bit packfiles and the 6
  Creation Club files with another signature are out of scope.

## Outside projects: what I read, and the verdict

Per toddhow's decision only licence files and READMEs. The pages were fetched through the web fetch tool, which returned a
summary by a small model, not the verbatim page: the quotes below are as that summary gave them and should be read from
the pages before any decision that depends on them.

| Project | Licence | Provenance statements found | Verdict |
| --- | --- | --- | --- |
| HavokLib (`github.com/PredatorCZ/HavokLib`) | GPL v3 ("This library is available under GPL v3 license. (See LICENSE.md)"; `LICENSE.md` is the GPL 3 text) | README: "an independent Havok packfile serialize and XML store library for C++"; lists `hkaSplineCompressedAnimation: 5.0.0 - 2017` as supported. It says nothing about the Havok SDK or where the layouts came from | **Provenance not shown clean by its own files, so its source was not read** and it was not used as an oracle |
| DSAnimStudio (`github.com/Meowmaritus/DSAnimStudio`) | GPL-3.0 | README credits "A small portion of HavokLib, specifically the spline-compressed animation decompressor, adapted for C#" and says PredatorCZ "Reverse engineered Havok Spline-Compressed Animation entirely". It does not mention the SDK | The only statement that HavokLib's spline work was reverse engineered is second hand (a README of another project), and it does not say from what (data, the game binary or the SDK). Not enough: source not read |
| HKX2 (Katalash's C# library and its forks) | forks found by search: `ret2end/HKX2Library` MIT, `Monitor221hz/HKX2-Library` MIT, `NCF-Library/Hkx2Library` AGPL-3.0; the original repository was not found | not read (the exact README of the original could not be fetched; search snippets only). Note 0054 recalled that its class definitions come from the SDK's XML reflection dumps (RECALLED-VERIFY) | Off-limits: source not read |

**Result: the spike used no outside source at all**, and there is no cross-check oracle from an outside project. If toddhow
wants HavokLib's decompressor as a cross-check, the decision to read its source needs a provenance statement by its author
that I could not find in its README or licence.
