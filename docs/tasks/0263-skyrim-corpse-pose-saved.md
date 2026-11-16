# Task 0263: a corpse keeps the pose it fell in after a load (the third way named in task 0254, decision 1)

Status: BUILT, Windows only, local commit on branch `corpsepose` from `71c74da723`, not pushed. Approved by toddhow.
No new setting: it changes only what task 0254 shows for a corpse that died with `[Physics] creativemotion ragdolls` on (with
`[Game] neutral combat`, `[Game] neutral saves`). A corpse that died with ragdolls off keeps today's behaviour.

Goal: with ragdolls on, a dead actor's settled pose is kept and saved; after a load the corpse is drawn in exactly that pose at
once, with no ragdoll fall (task 0254 decision 1: "falls again at every load, about 7 s") and no death clip pose.

## 1. Where the pose lives (decision)

**The engine's neutral save record `NWLD`, version 3; not the pack's `deaths` chunk.** Reasons:

- The pose is render and physics data (bone rotations as the ragdoll driver drew them), produced by the engine and consumed by
  the engine. Carrying it through the pack would need new SDK paths in both directions (the core hands the pack a blob, the pack
  hands it back), and the pack would store bytes it cannot interpret. The only SDK rule that applies ("no new virtual on
  `CombatRequestWriter`, additive only") would have forced a trailing virtual on `ActorRequestWriter` or a new optional interface
  for no gain.
- The pack's chunk is about WHO died and WHEN (respawn rules). The pose is about HOW the body lies; if the pack decides the actor
  respawned, the engine never puts the corpse back, so the pose is simply unused.
- Cost of the choice: a strict decoder with a version. Done as an additive version 3 (see 2); versions 1 and 2 still load.

**No SDK change at all.** The signal "this death is a restored corpse, not a new death" is `ActorRequestWriter::placeCorpse`, which the
pack already calls only for a corpse it puts back from its ledger (never for a death of this session: `deaths.cpp`,
`entry->mPlaced = true` at death). The engine marks the actor when that call arrives (`NeutralActorEntry::mRestoredCorpse`,
`PhysicsSystem::takeRestoredCorpse`), so a respawned actor that dies again later is never given its old corpse's pose.

## 2. The format

`neutralsave.*`: version 3 = version 2's layout plus, after the created-object counter, `u32 pose count` and per pose:
`string game id, u32 placement, u8 bone count, bones: string name, 3 f32 position, 4 f32 rotation` (rotation = the bone's WORLD
rotation as drawn, x y z w; position = world, only the pelvis' is used). Written ONLY when the state holds a pose, so every other save
keeps its version 1 or 2 bytes. Strict decoder: poses sorted strictly by (game id, placement), 1 to 32 bones, names 1 to 32 bytes,
every float finite, at most 4096 poses (the pack's death ledger holds as many), counts checked against the bytes left before
anything is allocated, a version 3 with no pose is not canonical, bytes after the end are refused. A save with poses cannot be read
by a build from before this task (it names the unknown version; nothing else of the save is lost for the build that wrote it).

## 3. What the engine does

- `ActorRagdollDriver::drivenPose` returns, in the layout of the ragdoll's states, the world rotation of every driven bone and the
  pelvis' world position as the driver drew them on its last frame (the delta captured at spawn is already inside them). A new
  constructor flag `absolute` makes the driver draw such a pose as it is: identity offsets, pelvis position taken as given.
  `SkyrimRagdoll::Controller::armFrozen` / `drivenPose` (under the controller's mutex; the driver runs on an update thread).
- `World::trackNeutralCorpseRest`: when every body of a ragdoll has been asleep for 3 updates in a row, the driver's pose is kept in
  `NeutralCorpsePoses` (pure, bounded, keyed by game id and placement). A body that wakes again drops the kept pose.
- `World::writeNeutralSave` copies the poses into the state; `restoreNeutralSave` loads them.
- `World::startNeutralRagdoll` (a death reported by the pack): if the pack put this actor back (`takeRestoredCorpse`) and a pose is
  kept, the controller is armed with it (`beginNeutralRagdollFrozen`) and NO ragdoll is spawned; otherwise today's path runs and
  any stale pose of the actor is erased. A pose that cannot be used (no ragdoll controller: ragdolls off or no bone table; a
  rig bone the skeleton lacks) falls back to today's path.
- A corpse restored from a pose keeps its pose in the store, so the next save writes it again; a corpse put back after a cell
  reload inside one session also uses it.

## 4. Edits outside the new files

`apps/openmw/mwrender/actorragdolldriver.{hpp,cpp}` (flag, `drivenPose`, last-drawn copy), `skyrimragdoll.{hpp,cpp}`,
`renderinginterface.hpp` (two defaulted virtuals appended to the ragdoll group), `scene.{hpp,cpp}`; `apps/openmw/mwphysics/
physicssystem.{hpp,cpp}` (the flag and `takeRestoredCorpse`), `apps/openmw/mwworld/neutralsave.{hpp,cpp}`, `worldimp.{hpp,cpp}`;
`apps/openmw/CMakeLists.txt`, `apps/openmw_tests/CMakeLists.txt`. New: `neutralcorpseposes.{hpp,cpp}`, `testneutralcorpseposes.cpp`,
`testcorpsepose.cpp`. No existing test edited. No SDK, contract or pack file touched.

## 5. Stage log (Windows only; `I:\Projects\OpenEden-work\papyrus\corpse`)

**Tests first.** 32 new tests (`CorpsePoseDriver` 7, `CorpsePoseStore` 8, `CorpsePoseSave` 17) written before the code and run against
stubs (the store refuses everything and finds nothing, `neutralsave.cpp` as it was, the driver's new constructor ignoring the flag
and `drivenPose` returning false): **24 of 32 failed, the 8 that passed test absence and refusals** (`red_full.txt`). With the code
32 of 32 pass (`green_full.txt`); wider `openmw-tests` `*Ragdoll*:*NeutralSave*:*Neutral*:*Corpse*:*SkyrimRagdoll*` 948 pass.
Not unit tested: `PhysicsSystem::takeRestoredCorpse` and the World wiring (they need a physics world and a Ptr); both are shown in
game below.

**In game** (`tools/skydeaths_check/check.sh` through `corpseun.sh`: the Bannered Mare, `summons test kill = Hulda`, ragdolls on, REPORT 70; the
save phase takes a picture 70 s after the data is read, a NEW process loads the save and takes a picture at the same time; frames in
`corpse1` to `r3`, `user\screenshots`, `screenshot000` before the save and `screenshot001` after the load, one camera):

| run | result |
| --- | --- |
| `r1`, `r2`, `r3` (ragdolls on) | save: `Ragdoll started`, `Corpse pose kept for "skyrimse:b80803" (16 bones)` two real seconds later, `Neutral save: ... 1 corpse poses`; load: `DEATHS placed`, `COMBAT restored dead actor`, `Corpse pose restored ... (16 bones, no ragdoll)` and **no `Ragdoll started` line**; loot opens (`container opened 0001a66e`). **The corpse crop of the picture before the save and the one after the load differ in 0 of 30,800 pixels over 8 grey levels in r2 and r3** (r1: 9,841, which is the torch light flickering: the pose in the side by side crop `r1_pair.png` is the same, viewed). |
| `r4off` (ragdolls off) | no `Ragdoll started`, no pose kept (`0 corpse poses`, a version 1 save), load as task 0254 (`placed`, `restored`, loot opens): unchanged |

Different processes settle into different poses (r2's pose is not r1's: the ragdoll's fall is not repeatable between processes, task 0192 knew it);
r1 and r3 loaded the same pose. **Morrowind unchanged:** `balsum.sh` (Balmora, seed 7, frame 600) SHA-256 prefix `1c5d47ad0b5dc32d` for this build and
for the `integ2` build (a build of main, not of the exact parent); `tools/worldobjects_check` OK (110 result lines equal); `mwsaveload.sh` both
directions `MW-ROUNDTRIP-EQUAL` (the script's digest counters differ by small numbers, as in earlier tasks' runs).

## 6. Decisions for toddhow

1. The pose lives in `NWLD` version 3, not in the pack's chunk (section 1). A save that holds a pose cannot be loaded by an older build.
2. The pose is kept 3 updates after every body sleeps, in WORLD coordinates (a corpse the pack places elsewhere would not follow:
   it is placed where it lay).
3. A corpse the pack cannot put back with a position (no `mHasPosition`: the position was never read) has no `placeCorpse` call, so it
   uses today's path (the fall).
4. Ragdolls off at load time while a save holds poses: the poses are dropped at the first such death report (no controller), and the
   corpse shows the death clip's last pose as before.
5. Non-driven bones (the neck has no body in the rig, fingers) show the death clip's last pose after a load, as before the save.

**Not run:** the Linux gate and GCC `-Werror` (hand-checked for the traps), clang-format 14, clang-tidy, the Lua tests, a fresh review,
`components-tests`, a creature or a follower corpse, a Raise Zombie corpse with a pose, the slot-reuse (frozen ragdoll) path in game,
a cell reload inside one session.
