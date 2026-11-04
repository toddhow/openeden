# Research note 0033: the extended skeletons of body replacers, and how to map them onto OpenEden's own skeleton

2026-11-03. Docs only: nothing was built or run, and no game data was read. Written for task 0070
(`docs/tasks/0070-skeleton-mapping-for-extended-skeletons.md`). Related: note 0027 (what the code provides), note 0032
and task 0069 (merging the legacy parts), task 0063 (B1, the body package and its validator), the Body System Design
Guide (`docs/design/Body-System-Design-Guide.md`), design `docs/design/advancedbodies-in-openeden.md` (decisions D7, G3).

toddhow's point: body replacers tend to ship, or require, an *extended skeleton* mod, so a body package from the wild
is bound to bones our own skeleton does not have. We need a way to map them onto our own skeleton.

Everything below is labelled **verified** (read in this repository, cited by file and function) or **general
knowledge, verify** (what I know of other games' mods; say what real file would settle it).

## 1. What the repository already does with skeletons (verified)

1. **The skeleton profile (v1) maps names one to one.** `opas::anim::SkeletonProfile` (`components/creativemotion/anim/clip/include/opas/anim/SkeletonProfile.hpp`)
   holds `bones[]` of `{source, target}` (a clip bone name and a target skeleton bone name, each used at most once) and
   an optional `legChain`. The shipped `components/creativemotion/data/profiles/humanoid-to-eden.skeletonprofile.json`
   maps 17 canonical humanoid names (`Pelvis`, `Spine`, `Chest`, `Neck`, `Head`, arms, thighs, shins, feet) to
   Morrowind's `Bip01 ...` names. It has no notion of an extra bone, an alias, a weight rule or a proportion.
   The header says the format is documented in `docs/formats/skeletonprofile.md`; that file is not in this repository
   (**verify**: it may live in the standalone CreativeMotion repository).
2. **The body package's binding already has room for the problem.** `Binding` (`components/body/bodypackage.hpp`) has
   `mSkeletonPath` or an inline `mSkeletonProfile`, `mRootBone`, `mUnmappedBones` ("bones the profile does not name"),
   and an optional `mRestSkeleton` (name, parent index, translation, rotation per bone). The validator's binding rules
   (`BODY-BIND-004`: a skin bone must be a profile target or an unmapped bone; `-005`/`-006`: the rest skeleton is
   unreadable or missing; `-014`: CreativeMotion's assumptions are skipped without a rest skeleton) cover *naming*
   but say nothing about what the engine should DO with an unmapped bone.
3. **Design decision D7** (design doc, "Decisions") says the binding is `opas.skeletonprofile`, grown to v2 with
   "extra/unmapped bones, proportion data for retargeting, and tail/extra-limb chains". **Decision G3** keeps the
   skeleton unchanged for packages made from Morrowind parts and allows *optional* extra spring bones in packages
   that supply them. The Guide (section on the package contents) says the skeleton binding includes "a map for
   differently named or extra bones", and that retargeting goes through the canonical skeleton with proportion data in
   the retarget map. Cosmetic physics is one-way (Guide principle 3; design section 10).
4. **Skinned meshes reference bones by NAME, not index.** `handleSkin` in `components/vsgadapters/nif/nif.cpp`
   (about line 390) builds an `Anim::Skin` whose `bones[]` carry `skin.mBones[i].getPtr()->mName`, keeps the **four
   largest weights per vertex** (`maxInfluenceCount = 4`, a `std::sort` by weight, descending) and does not
   renormalise afterwards (note 0032 drew the consequence for the merger: ties need a deterministic break).
   `Anim::Skin::link` (`components/animation/skin.cpp` about line 182) resolves each bone with
   `context.bones->search(bone.name)`; a name that is not found prints `<name> bone.path.empty()` to stderr (about line
   185). What the renderer does with a vertex skinned to a missing bone is not verified here (**verify** with a
   synthetic mesh: it is likely a collapsed or static vertex).
5. **The pose driver takes the skeleton as the actor has it.** `CreativeMotionPoseDriver` (`apps/openmw/mwrender/creativemotiondriver.cpp`,
   about lines 225 to 305) collects EVERY bone of the actor's animation skeleton, orders them by depth then name (so
   the order does not depend on a hash map), derives each parent from the node path, validates the result with
   `opas::rig::validateSkeleton`, and finds the animation root as `bip01` or `root bone`. Names are lower-cased.
   Clips bind by lower-cased name (`registerSource`, about line 306): a source controller whose bone name is not in
   the skeleton is skipped (`bone < 0`), and a bone with no track simply keeps its rest value. So **an extra bone
   that no clip animates is not an error in the driver; it is a static bone.** Bone groups (`BoneGroup_LowerBody`,
   `Torso`, `LeftArm`, `RightArm`, `apps/openmw/mwrender/bonegroup.hpp`) come from `entry.mBone->group` (how a bone
   without a known name gets its group is not verified: **verify** in the Anim bone registration).
6. **Part-to-bone table.** `MWAnim::sPartBones` (used at `apps/openmw/mwrender/animcontext.cpp` about line 70) names the
   bone each body part slot hangs on; the legacy adapter (`apps/openmw/mwrender/bodylegacy.*`, task 0063) reads it.
7. **Units and axes** of packages are metres, radians, Z up, right-handed (design doc, "Units and axes"); the Morrowind
   legacy adapter converts at one named function. A foreign skeleton's rest rotations and axes are not checked
   anywhere today.

## 2. What extra bones are (general knowledge, verify each against real files)

I have not read any mod skeleton in this session. The following is what I know of body-replacer ecosystems; each item
says what real file would settle it. The analysis stage (task 0070 stage a) is meant to turn this into numbers on
toddhow's own installed data, reporting aggregates only.

| Category | Typical bones (examples from memory, **verify**) | Carries skin weights? | Needs |
| --- | --- | --- | --- |
| **A. Deform bones** | Twist bones (upper arm, forearm, thigh, calf), extra spine bones (a third spine), clavicles, breast / belly / buttock bones that the body mesh is weighted to, finger bones where the base skeleton has none | Yes | A rule for where their weights go if our skeleton lacks them (fold to a mapped ancestor, or keep as a virtual bone). Their rest pose must match the mesh's inverse bind matrices |
| **B. Secondary (physics) bones** | Chains below breast, belly, butt, hair, cloth, cape, tail; in other games' mods they are driven by a physics add-on, not by animation clips | Often (the mesh is weighted to them) | A driver: our cosmetic physics (design B7, one-way), or static rest. A kind tag so the solver picks parameters |
| **C. Attach nodes** | Weapon, shield, quiver, back and hip attach points, camera / eye / head nodes | No (transforms only) | Mapping to the engine's own attach points by role, or keep as a transform node |
| **D. Helper and IK nodes** | IK targets, pole vectors, "marker" nodes, face-rig helpers (face bones are usually separate from the body skeleton) | Face bones yes (head mesh); IK helpers no | Usually `drop` or `fold` to Head |
| **E. Naming and hierarchy differences** | Different prefixes and suffixes for the same role (for example a `Bip01 L UpperArm` against a `... [LUar]`-style name with a bracket code), different left/right tokens, different root names, an extra root above the pelvis | n/a | A normaliser plus an alias list per role; the root rule of `CreativeMotionPoseDriver` ("bip01" or "root bone") must still find an animation root |
| **F. Rest-pose differences** | Different rest lengths and rest rotations for the SAME role (the extended skeleton's bones are not at Morrowind's positions) | n/a | A correction (section 3.6): skinning uses `boneWorld * inverseBind`, so a rest pose that differs from the one the mesh was authored for deforms the mesh |

What would settle each: the skeleton NIF the mod installs (bone names, parents, rest), the body mesh's skin bone list
(which bones carry weight, and how much), and an animation file from the same mod (which bones the clips key). Skeleton
and mesh are the mod author's files, so they are read only on the user's machine; only aggregate statistics may enter
the repository (guardrail: no game or mod assets in the repo).

Notes on specific games (all **verify**): Morrowind's hand is a single rigid bone with no finger bones in the stock
skeleton (the legacy adapter sees one hand part per side, `sPartBones`); later games' skeletons have finger bones, so
the canonical role set below has optional finger roles; most body-mod extra bones in later games are category A and B.

## 3. The consequence of doing nothing

- A skinned mesh whose bone name is missing from the actor's skeleton logs one line per bone and renders in an
  unspecified way (item 4 above, **verify**). With dozens of extra bones that is a corrupted body.
- Clips authored for the extended skeleton only drive the bones whose names match; extra bones stay at rest (item 5).
  That is safe but also dead: nothing moves what the physics add-on used to move.
- The pose driver would include extra bones in its skeleton, its inertializer and its style layer, which treat them as
  ordinary animated bones (harmless but wasteful, and wrong for physics-driven ones).
- Rest-pose differences (F) distort the mesh even if every name matches.

## 4. Design (summary; the spec carries the rules and the stages)

**One idea: the mapping is data, it is proposed by the machine, accepted by a person, and applied without touching the
user's files.**

1. **A mapping file** `*.skeletonmap.json` (`openeden.skeletonmap` v1, canonical JSON that round-trips byte for byte, as the
   project's rule for authored data requires). It binds one *source skeleton* (identified by a digest of its sorted
   bone names, parents and rest transforms) to the *canonical roles*, and gives every source bone not mapped to a role
   an explicit **action**:
   - `secondary`: the bone is kept as a **virtual bone**, attached to a mapped parent, with a `kind` tag (`breast`,
     `belly`, `butt`, `cloth`, `hair`, `tail`, `twist`, `other`) that selects the cosmetic physics preset later; until
     the solver exists it stays at rest;
   - `fold`: the bone's skin weights are added to a named mapped bone (default: the nearest mapped ancestor), then the
     vertex's weights are renormalised, deterministically;
   - `attach`: the bone is kept as a plain transform node (weapon, shield, quiver, camera), mapped to an engine attach
     point by role where one exists;
   - `drop`: the bone is ignored, with a validator warning that says what was lost.
2. **Suggestions, never silence.** A suggestion engine reads the source skeleton (and optionally the mesh's weight
   statistics) and proposes an action for every unmapped bone from: the normalised name (strip game prefixes such as
   `Bip01 ` or `NPC `, bracket codes, digits; recognise left/right tokens), the hierarchy (leaf versus chain, depth,
   parent role), the weight mass (a bone that carries almost no weight is `attach` or `drop`), and the rest distance
   from the mapped parent (near the parent and in line: twist; far and lateral: soft tissue). Each suggestion has a
   heuristic id and a confidence. The suggestions are written to the editable file with `accepted: false`; the engine
   refuses an unaccepted file (it falls back to the stock body and says why) so nothing is ever mapped silently. A
   one-click "accept all suggestions" exists in Eden Studio and as a command line option; it is an explicit act.
3. **Canonical roles.** The 17 roles of `humanoid-to-eden.skeletonprofile.json`, plus optional roles: clavicles, a
   third spine, fingers (15 per hand), toes, tail chain, extra-limb chains (Guide: tails and extra limbs are ordinary
   packages). Each role may list **aliases** (other names for the same bone in other skeletons). A game's compatibility
   pack maps roles to its native names; the Morrowind pack already does (`humanoid-to-eden`).
4. **Our skeleton stays the actor's skeleton.** We do NOT replace the actor's skeleton with the mod's: that would break
   every animation, attach point and the pose driver's assumptions. Extra bones that need to exist (`secondary`,
   `attach`) are **virtual bones** added to the actor's skeleton at creation, parented to the mapped bone, with a rest
   transform taken from the source rest skeleton and expressed in the parent's frame.
5. **Animations.** Clips bind through the existing profile (source name to target name) for canonical roles. Tracks for
   extra bones are ignored by default; an entry may set `useClipTrack: true` for a `secondary` bone whose clip really
   animates it. When no clip drives a virtual bone: the cosmetic physics solver (later), else a procedural idle
   (optional, later), else rest.
6. **Rest-pose and proportion differences.** Preferred: carry the source skeleton's rest proportions as tier-1 skeletal
   proportions (the body system's own feature: per-bone scale and offset applied before animation) so the rest poses
   coincide. Residual rest-rotation differences are corrected in the *inverse bind matrices* at load (derived data):
   `corrected = inverseBind * sourceWorldRest * inverse(canonicalWorldRest)` (to be derived and tested stage d). Which
   is needed depends on the data (**verify**, stage a measures the rest differences on toddhow's files).
7. **Two mods disagree.** The mapping belongs to the (body package, source skeleton digest) pair, so two bodies can
   map the same bone name differently without conflict. A conflict exists only when two **skeleton** mods provide the
   same names with different rests: the virtual file system's winner decides which skeleton file is read, the digest
   of what was read is compared with the mapping's digest, and a mismatch is a stale mapping (a validator error and a
   fallback to the stock body, not a guess). Garments authored for other extended skeletons get their own mapping; for
   a bone name that two mappings treat differently, the body's mapping wins and the garment gets a warning.
8. **The engine path.** Applying a mapping must not alter the user's files. Two places consume it:
   (a) skinned NIF load (`handleSkin`, nif.cpp): a bone-name remap and weight merge before the weights are sorted and
   truncated to four (so merged weights compete fairly, with ties broken by bone index), delivered as a loader option next to
   `skinfilter`; (b) the merged body of task 0069 (the merger applies the same function at the weights stage, and the
   mapping's digest is part of the body cache key). The mapping object is built once per (package, digest) on a worker
   thread, immutable and shared (`shared_ptr<const SkeletonMapping>`); virtual bones are created on the main thread.
9. **Failure behaviour.** Any failure (unreadable or unaccepted mapping, digest mismatch, cycle, cap exceeded, a
   failed validation, a skin bone with no rule) means the actor draws the **stock body**, never a half-mapped one, and
   one message appears once per package and session in the log and in the game; Eden Studio shows the full report.
10. **Determinism and limits.** Bones are ordered by (depth, name); weight merges sort by (weight descending, bone
    index ascending), accumulate in double and renormalise to float; a re-run gives the same bytes. Hostile-input
    limits (named constants with sources): at most 1024 bones, names at most 128 bytes of UTF-8 without control
    characters, hierarchy depth at most 64, no cycles, a file at most 1 MiB, at most 16 aliases per role, every source
    name used once, and two bones that normalise to the same name are an error (ambiguous).
11. **Eden Studio.** A "Skeleton Mapping" panel in the body module (`editor_modules/advancedbodies_editor`): the source
    skeleton as a tree (with the weight mass of each bone), the canonical roles as a second tree, the suggestions with
    their heuristic and confidence, accept and override per bone, a rest-pose overlay and a weight heat-map in the mesh
    viewer (task 0043), and save to canonical JSON with undo.

## 5. What this note does not establish

- Which skeleton mods toddhow uses, and what their bones look like (stage a measures them, aggregates only).
- Whether the renderer tolerates a missing skin bone, and how bone groups are assigned to unknown bones.
- The real size of rest-pose differences (decides whether tier-1 proportions are enough).
- Anything about physics add-ons' behaviour: the cosmetic solver is a later stage (design B7); this work only prepares
  the bones and their kind tags.
