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
  (`<actor folder>/animations/mt_idle.hkx` from the skeleton model's path), `bindClip` (the lower-case bone name of every
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
`meshes/actors/character/animations/mt_idle.hkx` through the VFS (`Skyrim - Animations.bsa` must be in the archive list);
writes nothing (no cache file: decoding one skeleton and one clip is cheap, measured below). Root motion is not applied.

Constraints:
- Off by default (`[Game] neutral actor idles = false`, needs `neutral actors`); in `tools/testprofile/profile.cfg` and README.
- Determinism: no random numbers, no clock. The start phase of each actor is a fixed hash of its reference id.
- Hot path: `Controller::run` allocates nothing (sample into a scratch sized at construction).
- Threads: `loadIdle` is called on the main thread; `run` on whichever thread updates that object, one at a time.
- Failure policy: a missing or damaged file, a skeleton without the clip's bones, or a creature skeleton with no `mt_idle`
  leaves the rest pose and logs one Info line per actor; nothing throws.

Acceptance criteria:
- [x] Paths, binding, loop time and sampling are tested (`HavokIdle*`, written first and run red against stubs).
- [x] The controller writes translation, rotation and scale of each bound bone and skips unbound ones (`SkyrimIdle*`).
- [ ] In game, the Bannered Mare's NPCs show a pose different from the rest pose, at two times different from each other.
- [x] With the setting off, nothing changes: Morrowind (`tools/worldobjects_check`, fixed-frame Balmora hash, C0/C2 pose trace).
- Out of scope: other idle clips, state changes (sit, sleep, talk), locomotion, creatures, root motion, the CreativeMotion
  graph, faces, Papyrus idle markers.

## Stage log

(filled in below as the work proceeds)
