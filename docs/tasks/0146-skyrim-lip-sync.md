# Task 0146: Lip sync for Skyrim SE speech (milestone M-S4)

Status: STAGE (a) ONLY, STOPPED. The head half holds; the `.lip` half does not yet (the format is only partly settled by the
data). Nothing was built in the engine, the pack or the formats layer; no setting was added; no code was changed. Branch
`skylip` (from local `main` f7184f15b2), not pushed.

Design doc section: `docs/design/skyrim-se-implementation-plan.md` (Lip sync, section 4.22); task 0143 (voice; `.fuz` split,
`.lip` part unused); task 0126 (facegen heads); task 0071 (`getSayPlayback` loudness drive).

Goal: drive a speaking Skyrim NPC's mouth from the `.lip` track of its voice line, in step with playback.

## 1. Provenance (clean room)

Every fact below was established on the user's own files (`Skyrim - Voices_en0.bsa`, `Skyrim - Meshes0.bsa`, aggregates only,
read by throw-away Python scripts in `I:\Projects\OpenEden-work\skylip\data\`, never committed). No outside source of any
kind was read: no web page, no CK wiki, no source or binary of another project, and **not `Tools/LipGen`, the Creation Kit or
any game executable** (the approved outside sources cover function meanings, not file formats). Where memory suggested a
fact it is marked RECALLED-VERIFY and was then tested on the data. Question 1 below asks whether an outside documented source
may be used for `.lip`.

## 2. Facts settled by the data

### 2.1 The `.lip` part (after `Fuz::split`)

| Fact | Established by |
| --- | --- |
| 75,408 `.fuz` files; 1,338 (1.8 percent) have a zero-length `.lip` part | Python walk of the BSA |
| 12-byte file header: u32 version, u32 `expandedSize`, u32 `kind`; version is 1 in all 74,070 non-empty parts | all files |
| `kind` is 13 in 73,118 files and 9 in 952 (the 9 are a different rig, e.g. `cruniquepaarthurnax`) | all files |
| `expandedSize = 28 + 132 * A` in all 73,118 kind-13 files, where `A` is the u16 at offset 12; for kind 9 the 132 rule fails in 921 of the 952 (the 31 that fit are accidents) and the four examined by hand fit `28 + 32 * A` (not tested on the rest) | all files; four kind-9 examples |
| So the payload is a packed form of `A` fixed-size records of 132 bytes (33 floats) after a 28-byte prefix | the identity above |
| `A` is the number of frames. `A` divided by the audio duration (from the xWMA `dpds` block) has median 33.5 per second and an inter-quartile range of 31 to 39 over 2,925 files: **frames are roughly uniform in time (about 30 per second), no per-frame time is visible in the stream** (RECALLED-VERIFY: 30 fps exactly) | `dpds`-based duration, 2,925 files |
| After `A` the header is variable: `03 00 <i32>` (A under 256), or `00 02 00 <i32>` (A of 256 or more), or `00 06 00` with no `i32`; then `10 00` (16) for kind 13, `08 00` (8) for kind 9; the `i32` is a small negative number (-1 to -14) of unknown meaning | many examples, the three shapes counted |
| The stream is a **run-length coding of the expanded bytes**: tokens of `00 <skip> 00` followed by literal bytes. Every skip is a multiple of 4 in the steady state and skips plus literals add up to exactly 132 for each repeating frame, in every repeating pattern examined (for example skips 36, 56, 12, 8 with literals 4, 8, 4, 4 = 132; 116, 8 with 4, 4 = 132; 84, 8, 28 with 4, 4, 4 = 132). Skip counts cross frame boundaries; the first token's code has its low two bits set (3) and is skipped over `code - 3` bytes (hypothesis, fits the examples) | about 15 files read by hand; the sums |
| Literal runs are floats (4 bytes) or two floats (8 bytes, usually equal to each other), or 3 bytes when the code's low two bits are 01 (the float's low byte, zero, is left out). **What decides 4 against 8 bytes is NOT settled** | hand reading; the failures of a greedy parser (it parsed no file completely) |
| A few places show a 6-byte literal (`ad 00 01 00 80 3e` after a skip of 88): not understood | one file |

**Not settled, and not guessable from the data alone:** (1) the rule for the literal length; (2) the meaning of the 33 float
slots of a record (the offsets seen in the steady state are 36, 60, 96 (a pair), 116 and 128, and values at 116 and 128 are equal
to each other, so the slots are not simply "16 phonemes with two values each"); (3) the time base; (4) the order of the
phonemes (an alphabetical order would match the head morphs, but nothing in the data says so). A reader written now would
guess at all four, and there is no ground truth in the data to check a guess other than a weak correlation with the audio.

### 2.2 The head

| Fact | Established by |
| --- | --- |
| The races' heads have `.tri` morph files in `Skyrim - Meshes0.bsa`: `femalehead.tri`, `malehead.tri`, `maleheadargonian.tri`, `…khajiit.tri`, child, werewolf, and the `…chargen`/`…races` variants (the latter two are not needed) | BSA listing |
| `.tri` layout (read with a layout from memory and **verified by the parse ending exactly at the end of the file** for `femalehead.tri`, `malehead.tri` and `maleheadargonian.tri`): 8-byte magic `FRTRI003`, 12 × i32 (vertices, triangles, quads, label verts, surface label verts, texture coordinates, flags, morphs, stat morphs, modifiers, modifier verts, reserved) and 8 more bytes (64 in all); vertices 3 × f32; triangles 3 × i32; texture coordinates 2 × f32 and their triangles 3 × i32 when `flags & 1`; then each morph: i32 name length, name, f32 scale, 3 × i16 deltas per vertex | three files, exact end of file |
| `femalehead.tri` has 996 vertices and 44 morphs; `malehead.tri` 898 vertices and 44 morphs; `maleheadargonian.tri` 1,219 vertices and 47 morphs | same |
| The morph names include the **16 phonemes** `Aah, BigAah, BMP, ChJSh, DST, Eee, Eh, FV, I, K, N, Oh, OohQ, R, Th, W`, plus `BlinkLeft/Right`, `Brow…`, `Squint…`, `Combat…`, `Dialogue…` and `Mood…` expression morphs, `LookDown` and `SkinnyMorph` | same |
| The **facegen head NIFs are `BSDynamicTriShape`** (every shape in the 20 sampled files of `facegendata\facegeom`, 3,158 files in Meshes0), and the head shape's vertex count equals the race head `.tri`: 996 (female Nord-like heads) and 898 (male) occur among the shapes of the sampled files, the other shapes are hair, brows, eyes, mouth and so on | 20 sampled facegen NIFs through a small NIF 20.2.0.7 block walker |

So a speaking head CAN be moved: the dynamic vertex data of the head shape, plus a weighted sum of the race `.tri` morph
deltas (scale × i16), selected by the head's vertex count (or by the head part's `.tri` field, not yet decoded in the
pack). Whether the deltas are applied relative to the facegen vertices (so that the NPC's own face morphs are kept) is
RECALLED-VERIFY and would be judged in a frame. Teeth, tongue and eyes are separate shapes whose morphs are not covered.

## 3. Verdict

The approach holds for the head (a morph path exists), and **fails at the `.lip` reader**: the grammar is understood to the
level of a run-length coding of 132-byte records but not to the level of a decoder, and the meaning of the slots cannot be
established from the data alone in a bounded effort. Per the task's own stop clause the build stages (b) to (d) were not
started.

## 4. Decisions for toddhow

1. **May an outside documented source be used for the `.lip` format** (for example the Creation Kit documentation or an
   open-source reader whose provenance is shown clean)? That is the fastest way to a correct reader; the data route has no end
   point. Without it, the data route needs a few more days with an audio-based oracle and may still end with guessed slot
   meanings.
2. **Interim option, no `.lip` needed:** drive the Skyrim head's `Aah`/`BigAah` (and `Oh`, `Eee`) morphs from the voice
   loudness through the playback position already used by task 0071 (`SoundManager::getSayPlayback`), behind
   `[Game] neutral lip sync` (off by default). That needs the `.tri` reader (formats layer, tests on generated bytes), a
   dynamic-vertex update of the head shape in `skyrimactor.*`, and the setting. It makes the mouth move, not form words.
   Recommended if (1) is "no". Say so and it is the next task.
3. Whether the facegen head's other shapes (teeth, tongue, eyes, brows) should move too (they have their own morph data
   and vertex counts).

## 5. Not done / not run

Everything past stage (a): the `.lip` reader, the `.tri` reader, the setting, the engine wiring, the in-game frames, the
Morrowind comparison (nothing in the engine changed, so none was needed), the Linux gate, clang-format 14, clang-tidy.
The build folder `I:\Projects\OpenEden-work\skylip\build` holds a finished build of this tree (`openmw`, `components-tests`,
`openmw-tests`) for the next hand; the survey scripts are in `I:\Projects\OpenEden-work\skylip\data\`.
