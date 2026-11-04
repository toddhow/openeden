# Research note 0036: the extended skeletons of toddhow's body replacers, measured

2026-11-04. Task 0070, stage a (`docs/tasks/0070-skeleton-mapping-for-extended-skeletons.md`), the read-only
measurement that research note 0033 asked for. **Aggregates only:** this note holds labels, counts, shares, canonical
role names (CreativeMotion's), our heuristic ids and token stems. It names no file path, quotes no bone list and holds
no per-vertex data. The raw reports, the detail file that names the bones, the manifest and the role tables written
for the measurement are in the work folder (`I:\Projects\OpenEden-work\skelstat\`), never in the repository.

**Review gate (the spec's, moved by the brief): toddhow reads the table in section 3 and the decisions in section 9
before stage e starts. Stage e is not started.** (Stages b to d were built before this measurement; their review fixes
recorded that the gate was skipped then.)

## 1. What was measured, and how

The files are toddhow's installed set (spec, answer 10), read in place from his Mod Organizer 2 instances, read-only:

| Label | What it is |
| --- | --- |
| `sse.xpmsse.male`, `.female`, `.beast.male`, `.beast.female` | XP32 Maximum Skeleton Special Extended: the four humanoid skeletons (human and beast, male and female) |
| `sse.male.output`, `sse.female.output` | the BodySlide output the game loads (body, hands, feet; `_0` and `_1`): HIMBO and CBBE 3BA built |
| `sse.female.cbbe3ba` | CBBE 3BA's own shipped body, hands and feet |
| `sse.male.himbo`, `sse.male.himbophysics`, `sse.male.tng` | HIMBO V5 core, HIMBO V5 physics add-on, The New Gentleman: their shipped meshes |
| `sse.beastm.output`, `sse.beastf.output` | the same output files measured against the beast skeletons (whether the game pairs them is not checked) |
| `fo4.zex`, `fo4.zex.facebones` | ZaZ Extended Skeleton (ZeX): the body skeleton and its face-bones skeleton |
| `fo4.aafpatch` | the character skeleton of another mod in the load order, which **wins over ZeX's** (it is higher in the profile's mod list): measured too, because it is the one the game loads |
| `fo4.female.fusiongirl`, `fo4.male.bodytalk` (and `fo4.aaf.*`, the same files against `fo4.aafpatch`) | Fusion Girl and BodyTalk 3: body and hands |
| `mw.base`, `mw.beast` | Morrowind's own `base_anim.nif` and `base_animKnA.nif` (loose files of the GOG install): the "Morrowind base set" and the actor skeleton of the rest comparison |

Every file was loose; none had to be read from a BSA or BA2 (`components/bsa` has readers for both, unused here).

**The tool:** `edenskelstat --manifest <file> --profile <humanoid-to-eden.skeletonprofile.json> --out <folder>
[--detail]` (`apps/edenskelstat/main.cpp`). The NIF reading is `components/bodynif/skeletonread.*`, the aggregation
and the report `components/body/skeletonstats.*`; both are tested on synthetic in-memory records only. The output is
deterministic: two runs over the same files gave byte-identical `report.txt` (58,803 bytes, SHA-256
`7ad118b7...b22`) and `detail.txt`. `--detail` is refused inside a git work tree.

**Definitions** (also in `skeletonstats.hpp`):

- *Bones* are the NiNodes of the file (the root included), depth first; *depth* counts from the root (0).
- *In base*: the bone's `boneComparisonKey` (the suggestion engine's tokens without noise tokens, the side folded to
  `l`/`r`) equals the key of a bone of the Morrowind base skeleton.
- *Role bones* come from the analysis map: the suggestion engine (`suggestSkeletonMap`) run with a **pack-style role
  table written for this measurement** (20 roles per skeleton, by hierarchy position: Spine, Chest and Spine2 are the
  first, second and third spine bones, as the Morrowind profile maps Bip01 Spine, Spine1 and Spine2). Tokens-only
  results are reported beside it. *Extra bones* are all other bones.
- *Shares* are of a body set's total skin weight, summed over vertices, before the engine's cut to four influences.
- *Rest differences* are `compareRest`: each role's rest in its parent ROLE's frame, against the same role of the
  other skeleton (translation delta, length ratio, rotation angle).
- *Units*: file units divided by `Constants::UnitsPerMeter` (Morrowind's 64 units per yard). That Skyrim and
  Fallout 4 use the same unit is an assumption; with it the skeletons are 1.83 m (Skyrim) and 1.73 m (Fallout 4)
  tall, human sizes, so it is plausible. Not verified from any other source.

## 2. What the engine's reader can read

- **Records:** `components/nif/niffile.cpp` (`makeFactory`) registers every record these files use: `BSFadeNode`
  (as a NiNode), `BSTriShape`, `BSSubIndexTriShape`, `BSDismemberSkinInstance`, `NiSkinPartition`,
  `BSSkin::Instance`, `BSSkin::BoneData`. `Reader::parse` refuses any version other than 4.0.0.0 and 4.0.0.2 unless
  `Reader::setLoadUnsupportedFiles(true)` is set (as `niftest` does, and the tool does). All 43 distinct files (7
  skeletons, 34 body meshes, the 2 Morrowind skeletons) parsed without an exception: Skyrim SE files are 20.2.0.7,
  user version 12, stream 100; Fallout 4 files the same with stream 130.
- **Skeletons:** no geometry, no unnamed node, no node reached twice. Nodes with a scale other than 1: 7 (Skyrim
  male), 2 (female); rotations that are not orthonormal: 4 (female). On the way to a role bone: 0 (male), **2
  (female)**, so the female rest comparison is partly affected (the reader carries no scale into a RestBone).
- **Skin weights, Skyrim SE:** every skinned `BSTriShape` (60 shapes in 24 files) stores **0 as its vertex count**;
  its vertices are in the `NiSkinPartition`. The reader takes the count from the partition then
  (`ShapeWeights::mVertexCountFromFile` false for all 60). The weights come from `NiSkinData`'s lists; the
  partitions' own weights agree within 5e-6 of a share; the packed vertex data agrees **when its bone indices are read
  as skin bone indices** (at most 1e-6, in every set), while read as partition-local it disagrees in the male sets
  (up to 0.026) and happens to coincide in the female ones. Every vertex sums to 1 within 1e-2; no index out of
  range; no weight that is not finite.
- **Skin weights, Fallout 4:** `BSTriShape` with `BSSkin::Instance`; the weights are only in the packed vertex data
  (four half floats), with skin bone indices. Same sanity: every vertex within 1e-2, nothing out of range.
- **What the reader cannot do:** the RENDERER cannot draw any of these meshes: `components/vsgadapters/nif` handles
  `NiGeometry` only and has no case for `BSTriShape` or `BSSubIndexTriShape` (a search of the folder finds neither
  name). `components/bodynif/skinnedshape.cpp` (the merged-body reader of task 0069) reads `NiGeometry` only too.

## 3. The aggregate table

From `report.txt` (the run of 2026-11-04; "analysis map" is the pack table's):

| skeleton | base | strict verdict | bones | depth | height m | in base | extra | core roles (tokens) | core roles (table) | medium+ (skeleton, analysis map) | medium+ (first body set) | extra share | missing skin bones | rest plan | largest role angle deg |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| sse.xpmsse.male | mw.base | BODY-MAP-022 (relaxed) | 543 | 31 | 1.831 | 55 | 523 | 1/17 | 17/17 | 76.7% | 100.0% | 0.1994 | 0 | inverse-bind | 162.8 |
| sse.xpmsse.female | mw.base | BODY-MAP-022 (relaxed) | 649 | 31 | 1.831 | 55 | 629 | 1/17 | 17/17 | 65.5% | 98.7% | 0.4821 | 0 | inverse-bind | 162.8 |
| sse.xpmsse.beast.male | mw.beast | BODY-MAP-022 (relaxed) | 542 | 31 | 1.831 | 60 | 522 | 1/17 | 17/17 | 76.8% | 100.0% | 0.1994 | 0 | inverse-bind | 165.0 |
| sse.xpmsse.beast.female | mw.beast | BODY-MAP-022 (relaxed) | 649 | 31 | 1.831 | 60 | 629 | 1/17 | 17/17 | 65.5% | 98.7% | 0.4821 | 0 | inverse-bind | 165.0 |
| fo4.zex | mw.base | valid | 374 | 28 | 1.727 | 7 | 354 | 3/17 | 17/17 | 54.5% | 100.0% | 0.8593 | 0 | inverse-bind | 150.7 |
| fo4.zex.facebones | mw.base | valid | 269 | 15 | 1.821 | 7 | 249 | 3/17 | 17/17 | 58.2% | - | - | - | inverse-bind | 150.7 |
| fo4.aafpatch | mw.base | valid | 374 | 28 | 1.727 | 7 | 354 | 3/17 | 17/17 | 54.5% | 100.0% | 0.8593 | 0 | inverse-bind | 150.7 |

The Morrowind base skeletons have 61 (`base_anim`) and 68 (`base_animKnA`) bones. "medium+" is the share of extra
bones with a suggestion of medium or high confidence that is not `ask`.

**Weight per body set** (shares of the set's total; vertices are summed over the set's files):

| body set | vertices | skin bones | on role bones | on extra bones | extra, < 0.01 m from a role ancestor | 0.01 to 0.05 m | 0.05 to 0.15 m | 0.15 m or more |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| sse.male.output | 36,690 | 65 | 0.801 | 0.199 | 0.039 | 0.000 | 0.082 | 0.078 |
| sse.male.himbo | 35,542 | 60 | 0.799 | 0.201 | 0.041 | 0.000 | 0.080 | 0.080 |
| sse.male.himbophysics | 39,532 | 66 | 0.793 | 0.207 | 0.038 | 0.000 | 0.085 | 0.084 |
| sse.male.tng | 18,714 | 28 | 0.824 | 0.176 | 0.056 | 0.000 | 0.043 | 0.077 |
| sse.female.output | 84,786 | 83 | 0.518 | 0.482 | 0.016 | 0.007 | 0.150 | 0.310 |
| sse.female.cbbe3ba | 71,584 | 83 | 0.531 | 0.469 | 0.018 | 0.008 | 0.172 | 0.271 |
| fo4.female.fusiongirl | 24,029 | 109 | 0.141 | 0.859 | 0.148 | 0.064 | 0.351 | 0.297 |
| fo4.male.bodytalk | 14,924 | 103 | 0.136 | 0.864 | 0.140 | 0.075 | 0.361 | 0.288 |

Every skin bone of every body is a bone of its skeleton (0 missing). Weight is a share of a set and the sets differ in
size, so the shipped CBBE 3BA mesh and the BodySlide output agree in shape (0.469 against 0.482 on extra bones), not
in total.

**Shape of the skeletons** (the role table's analysis):

| skeleton | extra bones under a role bone | between two role bones | at the parent's position (< 0.1 mm) | rest offset to parent, median m | longest name, bytes |
| --- | --- | --- | --- | --- | --- |
| sse.xpmsse.male | 25 | 21 | 216 | 0.029 | 33 |
| sse.xpmsse.female | 27 | 21 | 229 | 0.033 | 33 |
| fo4.zex | 81 | 28 | 189 | 0.000 | 28 |

Both games wrap their bones: in XP32 every role bone has a child at its own position whose name differs only by a
noise token, and the next role bone hangs below that child; in ZeX every bone hangs below an `_Offset` parent at the
same position. These are the bones "between two role bones". The deform weight of ZeX sits on `_skin` helper bones one
`_Offset` node below each role bone (0 to 5 mm away for most), which is why "under a role bone" carries no weight.

Related skeletons, by name set (counts only): ZeX and the winning Fallout 4 skeleton share all 374 names, and 4 of
them differ in parent or offset; the role bones' rests are identical (0.0000 in every column). XP32 male and female
share 541 names (108 are female only); their role rests are identical. Human and beast XP32 differ by 1 to 2 names.

## 4. Rest-pose differences

Each role in its parent role's frame, the measured skeleton against the other (metres, ratio, degrees):

| role | XP32 male vs Morrowind | ZeX vs Morrowind | ZeX vs XP32 male |
| --- | --- | --- | --- |
| Spine | 0.141, 0.850, 3.0 | 0.055, 0.498, 5.6 | 0.107, 0.586, 7.5 |
| Chest | 0.174, 1.037, 8.7 | 0.004, 1.032, 10.1 | 0.176, 0.995, 13.3 |
| Neck | 0.550, 1.447, 18.4 | 0.154, 1.474, 19.2 | 0.652, 1.018, 30.9 |
| Head | 0.127, 1.489, 9.5 | 0.047, 1.657, 18.0 | 0.158, 1.113, 21.4 |
| LeftUpperArm | 0.483, 1.362, 162.8 | 0.108, 1.325, 150.7 | 0.564, 0.973, 27.8 |
| LeftForearm | 0.416, 1.268, 29.5 | 0.001, 0.998, 46.7 | 0.415, 0.787, 39.4 |
| LeftHand | 0.333, 0.948, 123.5 | 0.022, 1.090, 13.1 | 0.349, 1.150, 116.7 |
| LeftThigh | 0.134, 0.999, 10.5 | 0.000, 0.999, 15.9 | 0.134, 1.000, 18.9 |
| LeftShin | 0.688, 1.097, 7.5 | 0.012, 0.974, 7.1 | 0.680, 0.888, 11.0 |
| LeftFoot | 0.667, 0.748, 61.0 | 0.077, 0.855, 62.0 | 0.606, 1.143, 98.7 |

(The right side is within a few degrees of the left; the full tables are in the report.) The angles measure how the
bones' own axes are laid out, not a pose: a 163 degree upper arm is a different axis convention, not a raised arm.
Length ratios of 0.5 to 1.7 are the proportion differences. **Tier-1 proportions alone cannot make these rests
coincide: every skeleton needs the inverse-bind correction** (`planRest`: "inverse-bind" for all seven).

## 5. Token stems of the extra bones

The full histogram is in the work folder's report. The stems that drive the heuristics (XP32 male / female / ZeX,
counted once per bone): side `l` 153 / 186 / 119, `r` 96 / 128 / 115, `weapon` 145 / 145 / 4, `finger` 50 / 50 / 60,
`tail` 60 / 60 / 9, `breast` 25 / 35 / 30, `butt` 15 / 16 / 12, `skirt` 13 / 13 / 0, `twist` 8 / 8 / 16, `skin` 0 /
0 / 96, `offset` 0 / 0 / 170. Not listed here: stems that are tags of tools or mod authors (several per skeleton) and
the words of the genital region (a few dozen bones per skeleton); toddhow decides whether they may appear in the
repository (section 9).

## 6. The heuristics: go or no-go

Share of the extra bones each heuristic classifies, with the pack table and the first body set's weights (XP32 male
523 / XP32 female 629 / ZeX 354 extra bones); "share" is the skin weight they carry in that body set:

| heuristic | XP32 male | XP32 female | ZeX | verdict |
| --- | --- | --- | --- | --- |
| roles by tokens (no table) | 1 of 17 core roles | 1 of 17 | 3 of 17 | **no-go**: every XP32 role name exists twice once the noise token is dropped (ambiguous), ZeX names (`LArm_UpperArm` style) are not in the vocabulary. A pack role table is required (17 of 17 with one) |
| name.attach (high) | 200 (38%), share 0 | 200 (32%), share 0 | 22 (6%), share 0 | go: none of them carries weight |
| name.soft-tissue (high) | 68 (13%), share 0.004 | 79 (13%), share 0.116 | 76 (21%), share 0.091 | go; a vocabulary gap: genital-region bones named with other anatomical words get `weight.deform` (9 in XP32 female) |
| name.tail (high) | 61 (12%) | 61 (10%) | 9 (3%) | go (no weight on these human bodies) |
| name.cloth (high) | 13 (2%) | 13 (2%) | 0 | go |
| name.finger (high, fold) | 50 (10%), share 0.076 | 50 (8%), share 0.117 | 60 (17%), share 0.323 | go as a fold, but fingers carry up to a third of a set's weight (the hands meshes): recommend finger roles in the pack tables |
| name.twist (high, fold) | 8 (2%), share 0.075 | 8 (1%), share 0.034 | 16 (5%), share 0.023 | go; a fold loses the twist (these bones are animated in their games) |
| name.face (high, fold) | 1 | 1 | 10 (41 in the face-bones skeleton) | go |
| name.helper (medium) | 0 | 0 | 6 | go (too few to judge) |
| weight.deform (medium, fold) | 7 (1%), share 0.044 | 17 (3%), share 0.216 | 50 (14%), share 0.421 | go as the fallback: toes, the thigh and calf helpers, genital-region bones, ZeX's `_skin` helpers |
| hierarchy.leaf (medium, drop) | 32 (6%), share 0 | 58 (9%), share 0.00005 | 7 (2%), share 0 | go (the dropped weight is below the 1e-4 threshold) |
| hierarchy.transform (medium, attach node) | 83 (16%), share 0 | 134 (21%), share 0 | 98 (28%), share 0.00004 | go for leaves of transforms; **wrong for the 21 / 21 / 28 wrapper nodes between role bones** (a virtual node with a role bone below it): no action of the format fits them (section 9) |
| rest.coincident (low, fold; only without weights) | 35 | 48 | 76 | no-go as a decision (low confidence; mostly the wrapper nodes); a hint only |
| ask | 0 | 8 ("ambiguous": two high votes disagree) | 0 | - |

Stage c's proposed target, at least 90 percent of extra bones with medium or higher confidence: **met with the weights
and the pack table** (100.0%, 98.7%, 100.0%; BodyTalk 100.0%), **not met from the skeleton alone** (54.5% to 76.8%)
or by tokens alone (35.9% to 64.9%). So the weights must be part of the suggestion run, and the pack table is not
optional.

## 7. The two synthetic checks

- **A skin bone the actor's skeleton lacks** (`apps/components_tests/skelstat/testmissingskinbone.cpp`): the NIF
  reader copies every skin bone name without checking it (`components/vsgadapters/nif/nif.cpp`, `handleSkin`);
  `Anim::Skin::link` (`components/animation/skin.cpp`) prints `<name> bone.path.empty()` to stderr and leaves the
  path empty, and `Anim::Skin::apply` then writes `skinMatrix * computeTransform(empty) * invBindMatrix`. Not a crash,
  not a collapse to a point: a vertex weighted to the missing bone is **frozen at its inverse-bind position in the
  frame the skin is attached to**, pulled towards the skeleton's origin by the bone's bind height (a bone bound 1.5
  up moves its vertex 1.5 down), and **follows no bone**, Bip01's own motion included; it moves only with the actor's
  placement. Without a mapping, the share of weight that would freeze is the "extra share" of section 3: about 20%
  (Skyrim male), 48% (Skyrim female), 86% (Fallout 4).
- **Bone groups of unknown names** (`apps/components_tests/skelstat/testbonegroups.cpp`): `Anim::CopyBones`
  (`components/animation/bones.cpp`) starts a group only at a bone whose lower-cased name is in `MWAnim::groups`
  (`components/mwanimation/groups.hpp`: `bip01 spine1` torso, `bip01 l clavicle` left arm, `bip01 r clavicle` right
  arm); every other bone takes the group of its nearest ancestor that started one, and a bone with none above it is
  group 0 (lower body). A skeleton with other names puts every bone, the arms included, in the lower body. Custom
  bones (`Bones::addCustomBones`) take the group of the bone they are added under, and so do their children: the
  virtual bones of stage e will inherit their parent role bone's group as they should.

## 8. Caps against the measurement

At most 649 bones (cap 1024), depth 31 (cap 64), names of at most 33 bytes, all ASCII (cap 128 bytes). The caps hold
with room; toddhow's answer 9 says to tighten them after stage a (section 9).

## 9. Decisions for toddhow

1. **BODY-MAP-022 refuses every XP32 skeleton** because of one pair of names that differ only in case and separator
   (equal after `normalisedBoneName`, distinct for the engine's own lookup, `Misc::StringUtils::lowerCase`). As
   shipped, the main Skyrim skeleton is unmappable. Options: make the normalised-only collision a warning (exact
   engine-key duplicates stay an error); or allow it when the map names both bones explicitly. The measurement ran on
   a relaxed copy (one bone renamed), marked in every number.
2. **Pack role tables are required** for both games (tokens: 1 and 3 of 17). This matches answer 2 (maps ship with
   the pack); the tables used here are measurement aids in the work folder, not the packs' data.
3. **Wrapper nodes between role bones** (21 in XP32, 28 in ZeX, all at their parent's position and without weight):
   no action fits. Recommend a new action, or a rule that a zero-weight extra bone with a role bone below it is part of
   the chain (its rest is composed into the child role's rest, which `compareRest` already does through the world
   matrices).
4. **The inverse-bind correction is the main path, not the fallback** for these skeletons (answer 11 chose tier-1
   first): role rotations differ by up to 163 degrees by axis convention.
5. **Finger and toe roles in the pack tables**: the fingers carry 8 to 32% of a set's weight, the toes up to 19%
   (XP32 female feet); as folds they lose all their motion.
6. **Vocabulary**: genital-region words (several, beyond `genital`, `penis`, `scrotum`) and whether such words, and
   the tool and author tags among the stems, may appear in the repository (they are not listed in this note).
7. **Caps**: keep 1024 bones and depth 64; names could drop from 128 to 64 bytes.
8. **The renderer cannot draw `BSTriShape`** (section 2): a prerequisite of any Skyrim or Fallout 4 body in the game,
   outside task 0070.
9. **Which Fallout 4 skeleton the map is made for**: the one the load order loads is not ZeX's; its role bones are
   identical, 4 bones differ.

## 10. Not measured, and limits

- Animations: no `.hkx` was read, so which extra bones the games' clips drive is unknown (needed for `useClipTrack`).
- The beast skeletons were measured against the human bodies; that the game pairs them was not checked.
- Rest values: a node's own scale is not carried (7 nodes in XP32 male, 2 on a role path in the female); record 0's
  transform is the engine's (`Nif::NiNode::read` replaces it by the identity unless it is named `bip01`).
- The heuristics' correctness was judged by reading the detail file (work folder), by me; no ground-truth map exists.
- Not run: GCC and the Linux gate, a fresh reviewer.
