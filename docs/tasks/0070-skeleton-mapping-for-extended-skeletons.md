# Task: Skeleton mapping for body replacers' extended skeletons

Status: DRAFT for toddhow's review (2026-11-03). Nothing is built. Evidence: research note 0033
(`docs/research/0033-extended-skeletons-of-body-replacers.md`); read it first, it separates what was verified in this
repository from what is general knowledge of other games' mods (marked **verify**).

Design doc section: `docs/design/advancedbodies-in-openeden.md`, decisions D7 (one bone-mapping format, profile v2) and
G3 (soft-tissue skeleton); Body System Design Guide, the package contents table ("a map for differently named or extra
bones") and sections on retargeting and cosmetic physics. Related tasks: 0063 (B1 package and validator, the
`Binding`), 0069 (merged legacy body: its merger and cache consume this mapping), 0043 (mesh viewer), the cosmetic
physics stage B7 (not started).

Pointer note: this spec does not edit `docs/design/advancedbodies-in-openeden.md` or `docs/tasks/0069-*.md` (another
agent edits them). The edits I recommend to them are listed at the end ("Recommended edits to other documents").

Goal: any body package bound to an extended skeleton (extra deform, secondary, attach and helper bones, different names
and rest poses) can be mapped onto OpenEden's own skeleton by a reviewed, editable, deterministic mapping file, and
applied at load without touching the user's files, or it is refused cleanly and the stock body is drawn.

Inherited code touched, at the commit of this spec's writing (the draft touches nothing; stages d and e would):
`components/vsgadapters/nif/nif.cpp` `handleSkin` (bone names and the four-largest-weights rule),
`components/animation/skin.cpp` `Skin::link` (`context.bones->search(bone.name)`),
`apps/openmw/mwrender/creativemotiondriver.cpp` (skeleton from the actor's bones; clip binding by lower-cased name),
`apps/openmw/mwrender/bonegroup.hpp`, `apps/openmw/mwrender/bodylegacy.*`, `components/body/bodypackage.hpp` (`Binding`),
`components/body/bodyvalidate.cpp` (`BODY-BIND-*`), `components/creativemotion/anim/clip` (`SkeletonProfile`).
CreativeMotion is edited here through OpenEden specs (decision 0010); a v2 of the profile type is such an edit.

## Problem

Body replacers tend to ship or require extended skeletons (extra bones for soft tissue, twist and helper bones, cloth,
weapon and shield attach nodes, face bones; and different names and rest poses for the same roles). A package from the
wild is bound to bones our skeleton does not have. Today: the profile maps names one to one, the binding lists
`unmappedBones` as names only, skin bones resolve by name and a missing name is a stderr line (research note 0033,
section 1). Four categories need four different answers (note 0033, section 2): deform bones need weights moved or a
virtual bone; secondary bones need a driver; attach nodes need an engine attach point; helpers are dropped or folded.

## Design (the rules; the reasoning is in note 0033, section 4)

### D1. The mapping file: `openeden.skeletonmap` v1

Canonical JSON (project rule for authored data: a canonical writer, byte-for-byte round trip), file extension
`.skeletonmap.json`, inside a body package as `skeleton.skeletonmap.json` or, for a skeleton mod not tied to a package,
in `<user data>/openeden/skeletonmaps/<digest>.skeletonmap.json` (the package's own file wins).

```json
{
  "format": "openeden.skeletonmap",
  "version": 1,
  "name": "example-extended-skeleton",
  "source": { "boneCount": 118, "digest": "<64 hex: BLAKE3 of sorted (name, parentName, rest) records>" },
  "profile": "humanoid-to-eden",
  "roles": [
    { "role": "Pelvis", "bone": "Bip01 Pelvis", "aliases": ["Pelvis_01"] }
  ],
  "extra": [
    { "bone": "Breast_L_01", "action": "secondary", "parent": "Chest", "kind": "breast",
      "useClipTrack": false, "suggested": { "heuristic": "name.soft-tissue", "confidence": "high" }, "accepted": true },
    { "bone": "Forearm_Twist_L", "action": "fold", "to": "LeftForearm", "rule": "nearest-mapped-ancestor",
      "suggested": { "heuristic": "hierarchy.twist", "confidence": "medium" }, "accepted": true },
    { "bone": "WeaponBack", "action": "attach", "parent": "Chest", "attachPoint": "weapon.back", "accepted": true },
    { "bone": "IK_Helper_01", "action": "drop", "reason": "no weights, no children", "accepted": true }
  ],
  "proportions": { "mode": "tier1", "note": "derived, never authored" }
}
```

Rules: every source bone is either a role bone or listed once in `extra`; `accepted: false` entries are refused by the
engine (D5); `to` and `parent` name a role or a mapped bone; the file never contains vertex data or mesh data (bone
names are short functional identifiers, see question 8).

### D2. Actions

- `secondary`: kept as a **virtual bone**: added to the actor's skeleton at creation, child of a mapped parent, rest
  transform from the source rest skeleton in the parent's frame; `kind` is one of `breast`, `belly`, `butt`, `cloth`,
  `hair`, `tail`, `twist`, `other` and selects the cosmetic physics preset in stage B7. Until then it stays at rest.
- `fold`: its skin weights are added to the target bone's, then the vertex's weights are renormalised. Rules:
  `nearest-mapped-ancestor` (default), `named` (`to`), `distribute` (split by rest-distance weights between two
  mapped bones). Deterministic (D6).
- `attach`: a plain transform node, mapped to an engine attach point by `attachPoint` where one exists.
- `drop`: ignored, with a validator warning that says what is lost (weights present, children present).

### D3. Canonical roles

The 17 roles of `humanoid-to-eden.skeletonprofile.json`, plus optional roles: clavicles, a third spine, fingers (15 per
hand), toes, a tail chain and extra-limb chains (Guide: tails and extra limbs are ordinary packages). Roles have
aliases. A game's compatibility pack maps roles to its native names (Morrowind's pack already does). The profile type
grows by an additive v2 (decision D7): roles, aliases, extra bone rules. One reader and one writer.

### D4. Our skeleton stays the actor's skeleton

No replacement of the actor's skeleton by the mod's (it would break animations, attach points and the pose driver's
assumptions). Virtual bones are added; clips bind through the profile for roles only; tracks for extra bones are
ignored unless `useClipTrack` is true. A bone no clip drives and no solver drives stays at rest.

### D5. Suggest, review, accept, apply

1. `suggest`: a pure function reads the source skeleton (and optionally weight statistics) and writes the file with
   `accepted: false`. Heuristics (each has an id and a confidence): name (strip game prefixes and bracket codes,
   recognise left/right tokens, soft-tissue vocabulary, twist, attach vocabulary), hierarchy (depth, leaf versus chain,
   parent role), weight mass (nearly zero: `attach` or `drop`), rest distance to the mapped parent (in line and close:
   twist; far and lateral: soft tissue). Ambiguous: `action: "ask"` (a validator error until resolved).
2. `review`: a person edits the file or uses the Eden Studio panel (D8). "Accept all suggestions" is an explicit act.
3. `apply`: the engine loads only an accepted, valid, digest-matching mapping (D6, D7).

### D6. Applying it (the engine path; no user file is touched)

- At skinned NIF load, a bone-name remap and weight merge in `handleSkin` before the sort and the truncation to four
  influences, delivered as a loader option next to `skinfilter`; merged weights compete fairly, ties by bone index.
  Weights accumulate in double, are renormalised, and written as float.
- In the merged body of task 0069: the merger applies the same function at its weights stage; the mapping's digest is
  part of the body cache key.
- Rest differences: preferred, carry the source skeleton's rest proportions as tier-1 proportions so the rests coincide;
  residual rest-rotation differences are corrected in the inverse bind matrices at load (derived):
  `corrected = inverseBind * sourceWorldRest * inverse(canonicalWorldRest)`. Which is needed depends on stage a's measurement.
- The mapping object is built once per (package, digest) on a worker thread, immutable and shared
  (`shared_ptr<const SkeletonMapping>`); virtual bones are created on the main thread; the pose driver treats them as
  secondary (no clip, no inertialization, no IK).
- Order of everything: bones by (depth, name); no hash-map order anywhere that affects output.

### D7. Failure behaviour

Any failure (unreadable, unaccepted or stale file, digest mismatch, cycle, cap exceeded, failed validation, a skin bone
without a rule, an ambiguous `ask`) means the actor draws the **stock body**, never a half-mapped one, with one message
per package and session (log and in-game), and Eden Studio shows the full report. Setting `[Bodies] skeleton mapping`
is off by default; with it off nothing changes (compared as in earlier stages).

### D8. Hostile-input limits (named constants, each with its source in a comment)

At most 1024 bones; names at most 128 bytes of UTF-8, no control characters; hierarchy depth at most 64; no cycles; a
file at most 1 MiB; at most 16 aliases per role; every source name used once; two bones that normalise to the same name
are an error. Violations are located errors (file, pointer, field). The reader never throws.

### D9. Eden Studio: the "Skeleton Mapping" panel

In the body module (`editor_modules/advancedbodies_editor`): source skeleton tree with each bone's weight mass; canonical
roles tree; the suggestion list (heuristic, confidence, accept, override); a rest-pose overlay and a per-bone weight
heat-map in the mesh viewer (task 0043); save writes canonical JSON; undo and redo. No extension code involved.

## Inputs / outputs

Inputs: a source skeleton (a skeleton NIF, or the rest skeleton in a body package's binding), optionally the body mesh's
skin weights, the canonical roles (profile). Outputs: a `*.skeletonmap.json`, a validator report, at load a remapped
skin and virtual bones, and optionally a derived inverse-bind correction (cached like the other derived data).

## Constraints

- **Determinism:** same inputs, same bytes (suggestion order, weight merges, cache keys).
- **Threading:** mapping built off the main thread, immutable; virtual bone creation on the main thread; say in the
  pull request which thread each callback runs on.
- **No allocation** in per-frame code: the pose driver's per-frame path is unchanged (virtual bones are fixed after creation).
- **Off by default:** `[Bodies] skeleton mapping` (off); with it off, frames and skinning are identical to before.
- **No game or mod assets in the repository:** tests use synthetic skeletons built in the tests; the analysis tool reads
  the user's own installed data and writes aggregates only.
- **GPL-compatible:** no code or tables from BodySlide, CBPC, HDT-SMP, XPMSE or similar projects (ideas only, as with
  Delirium Physics); the heuristics' vocabulary is written from the research note and verified against the user's own data.
- Full loop (untrusted input readers, skinning and the renderer path): tests first by a separate test-writer with a red
  run, a fresh reviewer, GCC `-Werror -Wshadow -Wunused -Wdangling-else`, clang-format 14, 120 columns.

## Stages and acceptance criteria

- **Stage a: measure (read-only).** A command line tool (name proposed `edenskelstat`, built on the engine's NIF reader;
  or a mode of `edenbodyvalidate`) run on the user's own installed skeleton and body files. Output: aggregates only.
  - [ ] Per skeleton NIF: bone count, depth, bones not in the Morrowind base set after name normalisation, name-token
        histogram (no free text beyond the tokens), per extra bone the weight mass (share of total weight, sum over
        vertices) and the rest offset to its parent, in the same units as the design doc.
  - [ ] Rest-pose differences of the roles that exist in both (position and rotation difference, per role), to decide
        whether tier-1 proportions suffice.
  - [ ] What the renderer does with a skin bone missing from the skeleton (a synthetic NIF: collapsed, static, crash?).
  - [ ] How bone groups are assigned to bones with unknown names (read in the Anim registration; a synthetic case).
  - [ ] The report is deterministic (two runs identical); nothing per-vertex or per-file-content is committed; a research
        note with the aggregate table and a go/no-go for each heuristic (what share of extra bones each classifies).
  - Review gate: toddhow reads the table before stage b starts.
- **Stage b: the format.** `components/body` (or `components/skeleton`) types for roles, aliases, extra rules, the reader
  and canonical writer, the validator with located errors and the D8 caps, the digest function.
  - [ ] Round trip byte-identical for 30 generated valid files; every validator rule has a passing and a failing fixture;
        hostile fixtures (cycle, depth 1000, 100000 bones, a 2 MiB file, control characters, duplicate names, two names
        that normalise equal) give a located error, no throw, no unbounded memory (a test with an allocation cap).
  - [ ] An `ask` entry or `accepted: false` is a validator error with a rule id (`BODY-MAP-*`).
- **Stage c: the suggestion engine.** Pure functions: the name normaliser, the heuristics, the confidence order.
  - [ ] At least 40 tests on synthetic skeletons that cover every category (deform, secondary of each kind, attach,
        helper, naming differences, ties, an ambiguous bone) and pin the chosen action and confidence; a property test
        that `suggest` is deterministic under input bone reordering.
  - [ ] The stage a go/no-go table is met on the user's data when run locally (target to be set from stage a, proposed
        at least 90 percent of extra bones with a medium or higher confidence), reported as numbers only.
- **Stage d: the weight and rest maths.** `fold`/`distribute` weight remap with renormalisation; the inverse-bind
  correction.
  - [ ] Property tests: weights sum to 1 within 1e-3 after any mapping, no NaN, independent of the input weight order,
        idempotent; a synthetic skinned mesh with a rest mismatch has vertex positions at rest unchanged within 1e-4 m
        after the correction (and a test showing the uncorrected error, so the test would fail without the correction).
- **Stage e: the engine path.** Loader option in `handleSkin`, virtual bones, pose driver skips them, the failure
  fallback, the message, `[Bodies] skeleton mapping`.
  - [ ] With the setting off, a frame comparison against the commit before (same as earlier stages: `enabled = false`
        style byte-identical, others PSNR with the run-to-run noise stated honestly).
  - [ ] With a synthetic extended skeleton and a synthetic skinned mesh in a test mod: the mapped body renders (frame
        viewed), the unmapped one falls back to stock with exactly one message; the 32 Lua tests pass; every test game
        runs with `OPENEDEN_QUIET_WINDOW=1`.
  - [ ] Virtual bones and mapping object lifetimes: an actor destroyed mid-build, and a mapping changed on disk while
        the game runs, do not crash (a test).
- **Stage f: Eden Studio panel.** D9; opens a synthetic skeleton, shows suggestions, accept, override, save, undo; the
  saved file passes the validator and round-trips.
- **Stage g: close-out.** Documents, the guide's decision rows, the changelog row, the checklist of toddhow's own test
  with a real body replacer of his choosing.

Order: a, then b and c can overlap, d, e, f, g. Stage e is also where this meets task 0069's merger (shared weight
function) and B7's cosmetic solver (the `kind` tags).

## Out of scope

Replacing the actor's skeleton with the mod's; running the cosmetic physics solver (B7); animation retargeting beyond
the existing profile mechanism (clip bone names to target bone names); face rigs; other games' compatibility packs
(they supply role maps, not this tool); distributing mappings or skeletons (the repository never contains a mod's
skeleton or mesh); automatic acceptance of suggestions.

## Open questions for toddhow (recommendation first)

1. **Canonical skeleton.** Recommend: the canonical contract is CreativeMotion's roles (17 now, optional roles added),
   each game's compatibility pack maps roles to its native names, and a mod's extra bones are mapped onto the GAME's
   native skeleton plus virtual bones, not replaced.
2. **Adopt the mod's skeleton instead?** Recommend no for the first version (it breaks animations, attach points and
   the pose driver's assumptions); revisit for non-humanoid packages.
3. **Unmapped extra bones by default.** Recommend refuse until accepted (never silent), with an explicit "accept all
   suggestions" in the panel and on the command line.
4. **Secondary bones before the cosmetic solver exists.** Recommend: they exist as virtual bones at rest (static, with
   their kind tags), so a body made for soft-tissue physics looks right but does not jiggle until B7.
5. **Fingers.** Morrowind's stock hand is one rigid bone (**verify**); later games have fingers. Recommend optional
   finger roles in the canonical set so later-game bodies keep them.
6. **Face bones.** Recommend `fold` to `Head` for now; face morphs belong to the head part (design section 7.2).
7. **Where mappings live.** Recommend both: inside the package, and a user folder keyed by skeleton digest for skeleton
   mods not tied to a package; the package's file wins.
8. **Can community mappings be shared?** The file holds bone names only (short functional identifiers) and never the
   skeleton or mesh. Recommend yes; confirm you are comfortable, since names come from mod authors' files.
9. **Caps (D8).** Recommend 1024 bones, 128-byte names, depth 64, 1 MiB; tighten after stage a shows the real maximum.
10. **Which mods first.** Please name two or three body replacers and skeleton mods you actually have installed (or want to
    support), so stage a measures those; I cannot see your installed files. Recommend one Morrowind pair and, later,
    one for the game you intend to target (Fallout New Vegas).
11. **Rest differences.** Recommend tier-1 proportions first and the inverse-bind correction as a fallback, decided from
    stage a's measurement.
12. **Numbering and docs.** This is 0070 (note 0033). The design doc's stage table is stale from B2 on (see task 0069's
    numbering note); recommend renumbering it when its owner next edits it.

## Recommended edits to other documents (not made here)

- `docs/design/advancedbodies-in-openeden.md`: decision D7: replace "grows to v2 with extra/unmapped bones" with a
  pointer to `openeden.skeletonmap` v1 and this task (the profile still maps roles, the mapping file carries the extra
  bone rules); G3: add "extra bones of foreign skeletons become virtual bones at rest until B7"; a new subsection
  "extended skeletons" under section 7 with D1 to D9 in summary; new decision rows H21 to H26 for questions 1, 3, 4, 5,
  7 and 11; the section 12 stage table gains this task's stages; the validator list (section on B1) gains `BODY-MAP-*`.
- `docs/tasks/0069-advancedbodies-merged-legacy-body.md`: stage b's merger takes the weight-remap function of this
  task's stage d (one implementation), and stage c's cache key includes the skeleton mapping's digest and version.
- `components/body/CLAUDE.md`: a section for the mapping file and its caps when stage b lands.

## Answers of toddhow, 2026-11-03

1. Yes: CreativeMotion's roles are the canonical skeleton; each game's compatibility pack maps them to native names.
2. We never adopt a mod's skeleton: we always map onto our own. Since each game has essentially one extended skeleton mod, the maps are made per compatibility pack and SHIPPED with the pack, so a player gets the mapping with no work. (Consequences for the spec: a shipped map is our own authored data (bone names and actions, no mod meshes or files), recorded with its source skeleton digest; a user whose skeleton digest does not match a shipped map gets the suggestion flow of question 3.)
3. Yes: refuse unaccepted extra bones, with an explicit "accept all".
4. Yes: secondary bones stay static until the cosmetic solver exists.
5. Yes: add optional finger roles.
6. Yes for now: fold face bones to `Head`. A design document for facial animation and its Eden Studio module exists on the CreativeMotion side; the face-bone stage must read it before it replaces the fold (not available in this repository: ask toddhow where it is).
7. Yes: mappings live inside packages and in a user folder keyed by skeleton digest.
8. Yes: mappings that contain only bone names may be shared.
9. Yes: the proposed caps, tightened after stage a.
10. toddhow's installed set, which stage a measures (read-only, aggregates only): Skyrim: CBBE 3BBB (female body), XP32 Maximum Skeleton Special Extended (XPMSSE), Himbo (male body) with The New Gentleman (TNG, genitalia); Fallout 4: Fusion Girl (female body), ZAZ Extended skeleton, BodyTalk 3 (male body), which use the same skeletons as above. Both games are later compatibility packs, which the project guide lists as "not yet"; their maps are prepared as data and tested on synthetic skeletons plus the read-only measurement, with no engine support for those games yet.
11. Yes: tier-1 proportions first, inverse-bind correction as the fallback.
12. Yes: the design doc's stage table is stale and gets renumbered (the edit waits until the agent working on 0069 has finished).

## Stage log

### Stages b, c and d, 2026-11-04 (synthetic data only; no game or mod data was read)

Built in `components/body` (target `eden_body`), no engine code:

- **Stage b, the format** (`skeletonmap.*`): `openeden.skeletonmap` v1 types, reader (pre-scan of size, nesting and object,
  array and value counts before anything is parsed; located errors; never throws), canonical writer (byte for byte round
  trip), `skeletonDigest` (BLAKE3 of the sorted records, rest values as float bits, order independent), the validator with
  rules `BODY-MAP-001` to `019` (the map) and `020` to `023` (the source skeleton), the canonical role table (17 core roles
  plus the optional ones of toddhow's answer 5: clavicles, `Spine2`, toes, 30 finger roles, `Tail1` to `Tail8`). The optional
  roles live in this table only: CreativeMotion's `opas.skeletonprofile` v1 is untouched (it is another agent's area; its v2
  is stage e's business).
- **Stage c, the suggestion engine** (`skeletonsuggest.*`): `suggestSkeletonMap`, `acceptAllSuggestions`, `boneNameTokens`;
  roles by the pack's table (native name, normalised name, aliases) then by name tokens; extras by name votes (high or medium),
  falling back to weight, hierarchy or rest votes only when no name vote exists; equal top votes that disagree make an
  `ask` entry. Output order is (depth, normalised name, name): reordering the source bones does not change a byte.
- **Stage d, the maths** (`skeletonmapping.*`, `bodyinfluences.*`): `buildMapping` (the consumer API that refuses an invalid,
  stale or unaccepted map; `BuildOptions::mAcceptAllSuggestions` is the explicit act and never accepts an `ask`),
  `SkeletonMapping::remapTable` (an error names every skin bone without a rule), `remapVertex` (direct, split by rest
  distance, dropped with a fallback so no mass is lost; then the shared rules), row-vector `Mat4` helpers,
  `correctedInverseBind`, `compareRest`, `planRest`. The weight rules (`normalisedWeights`, `cutToFourWeights`) were MOVED
  from `bodymerge.cpp` into `bodyinfluences.*` and the merger now calls them (spec recommendation: one implementation); the
  code is the same, a `static_assert` ties the two influence counts.

**Tests.** 186 new tests in `apps/components_tests/body/` (`testskeletonmap.cpp` 54, `testskeletonsuggest.cpp` 89 of which 59
table cases, `testskeletonmapping.cpp` 39, and the 4 `BodyInfluences` tests inside it), written before the code on synthetic
skeletons built in the tests. Whole `components-tests` on Windows: 5235 tests, 5231 passed, none failed (4 skipped, not mine).
The merger's own tests (`BodyMerge*`) pass unchanged after the move.

**Red run.** Against stubs: 186 tests, 173 failed, 13 passed. The 13 that pass against a stub are tests whose correct answer
is an empty or refused result (a missing file, an unaccepted map, a singular matrix, a clean source skeleton against a stub
validator, determinism of an empty result), and 3 of the 4 `BodyInfluences` tests, which characterise code that already
existed (they could not be red). The 4th `BodyInfluences` test failed because my expected value assumed a negative weight is
dropped before the sum, which the moved function does not do; the test was corrected to the function's real contract (inputs
are never negative: the remap filters them before calling it).

**Mutation checks** (a deliberate bug, one build, tests run, bug removed): swapped distribute fractions caught by
`a_distribute_fold_splits_by_rest_distance_and_the_closer_bone_gets_more`; no lower-casing in `normalisedBoneName` caught by
`normalisation_follows_the_stated_rule`; "top votes always agree" caught by 4 ask-entry tests; dropping the `isfinite` filter
in `remapVertex` was NOT caught at first (an equivalent mutant for NaN, since `NaN > 0` is false; only +infinity gets through),
so an infinite weight was added to `non_finite_negative_and_zero_weights_count_as_nothing`, which then caught it.

**Errors in my own tests found by the first green run (not implementation bugs):** the missing-`accepted` test cut 17
characters of an 18 character text and so produced a syntax error; a world-matrix test forgot the upper arm's 0.10 in the
expected height (1.35, not 1.25). Both corrected after seeing the failures; stated here because the expected values were
changed after the first run.

**Deviations from the spec.**
- No separate test-writer (a fork cannot start one): the tests were written by the implementing agent before the code, and a
  fresh reviewer is still to come.
- The "allocation cap" test of stage b is not built: the guarantee is by construction (the pre-scan refuses before the parser
  runs) and is asserted by the hostile fixtures (size, depth, object count) checking the pre-scan's own messages.
- The target "at least 90 percent of extra bones with medium or higher confidence on the user's data" is not evaluated: it
  needs stage a's measurement. The heuristics' vocabulary is written from research note 0033 and general knowledge, not
  verified against real skeletons.
- A secondary bone's `parent` in this implementation may be a role name, a role bone's source name or another secondary
  bone; an attach bone cannot be a parent (BODY-MAP-010).

**Not verified.** The files have been built with MSVC only: no GCC `-Werror -Wshadow` build, no clang-tidy, no Linux gate.
`openmw-tests` was not run (nothing there changed); the real-data bake of task 0069 was not repeated after the move of the
weight rules (the code is the same and the merger tests pass, but byte identity of the cache on real data is not re-proved).
Stage e (the engine path) and everything after it are not started.
