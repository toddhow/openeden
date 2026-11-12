# Task: Skyrim SE neutral actors loop an idle clip (M-S3, first slice)

Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.11 and 4.12 (decision D4 of 2026-11-11: O1 + O2, our own
graph on CreativeMotion plus clips converted from the game's `.hkx`; behaviour graphs are not read).
Status: approved under toddhow's blanket "go with your recommendations" (2026-11-11).

Goal: with `[Game] neutral actor idles` on (and `neutral actors`), a Skyrim SE NPC in a neutral session loops the humanoid idle
clip decoded from the user's own `.hkx` files instead of standing in the rest pose.

Inherited code touched: none changed in behaviour. `apps/openmw/mwrender/scene.cpp` `Scene::insertNeutralActor` (new call
behind the setting), `components/settings/categories/game.hpp` (one setting).

New code:
- `components/havokpack/idle.{hpp,cpp}` (formats layer, standard library only): `skeletonHkxPath`, `clipHkxPath`
  (`<actor folder>/animations/h2h_idle.hkx` from the skeleton model's path), `bindClip` (the lower-case bone name of every
  track), `loopTime`, `samplePose`. No new format fact: it uses task 0119's decoder as it is (provenance F13 to F36; nothing
  outside was read, nothing decompiled).
- `apps/openmw/mwrender/skyrimidle.{hpp,cpp}`: `loadIdle` (VFS read of the skeleton `.hkx` and the clip `.hkx`, decode, bind;
  cached per path pair, failures too, thread safe) and `SkyrimIdle::Controller`, an auto-play `Anim::Controller`.

Design choice, justified. The plan says "through the AnimSet/driver path or the most direct path that reuses CreativeMotion's
clip type". A neutral actor has no `CharacterController`, no `MWAnim::Play` state machine and no `AnimSet` source, and the
pose driver (`CreativeMotionPoseDriver`) is created by the controller; making them for a stationary NPC would pull in
mechanics. So the clip is played by the object's existing auto-play mechanism (`MWAnim::Object::autoPlay`, which Morrowind
scenery uses for flags and banners): one controller per actor samples the shared decoded clip and writes the actor's
`Anim::Transform` bones. The clip is not converted to `opas::anim::Clip`: a retarget is not needed because the track bones
ARE the skeleton's nodes (95 of 99 character-skeleton names match NIF strings, task 0119) and the engine's own `SkyrimActor`
skeleton uses those nodes. When M-S3 proper brings Skyrim locomotion on CreativeMotion's graph, this controller is replaced
by the pose driver playing an `AnimSet`; the decoder, binding and paths stay.

Inputs / outputs: reads `meshes/actors/character/character assets/skeleton.hkx` (and the female folder's) and
`meshes/actors/character/animations/h2h_idle.hkx` through the VFS (`Skyrim - Animations.bsa` must be in the archive list);
writes nothing (no cache file: decoding one skeleton and one clip is cheap, measured below). Root motion is not applied.

Constraints:
- Off by default (`[Game] neutral actor idles = false`, needs `neutral actors`); in `tools/testprofile/profile.cfg` and README.
- Determinism: no random numbers, no clock. The start phase of each actor is a fixed hash of its reference id.
- Hot path: `Controller::run` allocates nothing (sample into a scratch sized at construction).
- Threads: `loadIdle` is called on the main thread; `run` on whichever thread updates that object, one at a time.
- Failure policy: a missing or damaged file, a skeleton without the clip's bones, or a creature skeleton with no `h2h_idle`
  leaves the rest pose and logs one Info line per actor; nothing throws.

Acceptance criteria:
- [x] Paths, binding, loop time and sampling are tested (`HavokIdle*`, written first and run red against stubs).
- [x] The controller writes translation, rotation and scale of each bound bone and skips unbound ones (`SkyrimIdle*`).
- [x] In game, the Bannered Mare's NPCs show a pose different from the rest pose, at two times different from each other.
- [x] With the setting off, nothing changes: Morrowind (`tools/worldobjects_check`, fixed-frame Balmora hash, C0/C2 pose trace).
- Out of scope: other idle clips, state changes (sit, sleep, talk), locomotion, creatures, root motion, the CreativeMotion
  graph, faces, Papyrus idle markers.

## Stage log

2026-11-12, one hand wrote tests and code (no separate test writer, no fresh reviewer); Windows only; local commits on branch
`skyidle`, not pushed.

- **The clip:** the humanoid has NO `mt_idle.hkx` (it exists for creatures only: `chicken`, `hare`, `bear`, ...). The standing
  unarmed idle of the character is `meshes/actors/character/animations/h2h_idle.hkx` (8 s, 99 tracks), so the default clip
  name is `h2h_idle` (decided from the extracted `Skyrim - Animations.bsa`, 8,979 files; the clip name is a parameter of
  `clipHkxPath`). The male and female skeletons share the clip folder (`character assets` and `character assets female`).
- **Tests first, red:** `HavokIdle*` against a stub `idle.cpp`: 7 of 8 failed (the 8th pins a wrong-size no-op), green after.
  Two mutations (no negative-time wrap; clip folder one level too deep) each failed named tests; restored. The
  `SkyrimIdle*` controller tests (4 of 5 written with the code, not run red) pass; the fifth's expectation was a test fault
  (the VFS throws "not found") and was corrected.
- **Real data:** the humanoid skeleton (99 bones) and `h2h_idle` bind with all 99 tracks; in game 96 of 99 track bones are
  scene nodes (3 missing: helper bones of the animation skeleton the NIF lacks, as 0119 found). One sample of all tracks costs
  **6.9 microseconds** (20,000 samples, opt-in test `HavokIdleRealData`, `OPENEDEN_HAVOK_IDLE_ROOT`), plus about 96
  `setAttitude` writes: no per-frame allocation by construction (scratch sized at construction), not measured with a counter.
- **In game** (`tools/neutral_session_check/check.sh ... WhiterunBanneredMare 3500 n5idle`, the new scenario; quiet window):
  all 6 actors log "idles: 96 of 99 tracks bound". Frames of one woman, setting off against on (viewed): off = the arms-down rest
  pose; on = the unarmed stance (hands raised, body lowered), and two frames 3.3 s apart differ in head and arm position.
  `idle.hkx` poses lower the pelvis: the actor stands visibly lower than in the rest pose (root motion and the root bone are
  ignored; the NPC's `NPC COM` z comes from the clip's track, not the NIF).
- **Morrowind unchanged** (parent = this tree with `scene.cpp`, `game.hpp`, `settings-default.cfg` taken from `main`, built in
  the same folder; `openmw_parent.exe`): `tools/worldobjects_check` dump 110 lines byte-identical; the fixed-time Balmora
  frame (`OPENEDEN_FIXED_FRAME_TIME`, seed 7, frame 600) SHA-256 `1c5d47ad0b5dc32d...` for parent, new with the setting off and
  new with it on; `tools/creativemotion_check` `cm` variant with `OPENEDEN_FORCE_ANIM_VARIANT=1` and a fixed frame time: the
  player's CMTRACE lines (1,401, positions removed, timestamps stripped) byte-identical between parent and new.
- **Not run:** the Linux gate and GCC `-Werror` (Docker is not to be used), clang-format 14, clang-tidy, the Lua tests, a fresh
  review, sanitizers, a frame-time measurement with many actors, female actors and other races in a frame, a third time sample
  (the 3500-frame run ended after two shots), creatures (they keep the rest pose: no `mt_idle` lookup for their folders).
- **Decisions for toddhow:** (1) `h2h_idle` (hands raised) as the standing pose looks like a fight stance; Skyrim's ambient
  standing NPCs use the behaviour graph's choice, which we do not read: a calmer clip (for example `idlelookaround` family or
  the `1hm_idle`/idle variants) can be named per race or actor later. (2) The pelvis lowering of the clip makes feet float
  above or sink; root handling belongs to M-S3 proper. (3) `Skyrim - Animations.bsa` is added to the archive list of
  `tools/neutral_session_check/check.sh`; a real game config needs it too.
