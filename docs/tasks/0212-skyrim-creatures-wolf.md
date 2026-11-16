# Task 0212: Skyrim creatures, first slice: the wolf

Design doc section: `docs/design/game-packs.md` (the Skyrim SE pack), decision 0017 (neutral actors), tasks 0121, 0126, 0135,
0145, 0148 (actors, idle and locomotion clips), 0168 to 0197 (combat). Branch `creatures`, from `8002c504fb`.

Goal: a wolf (NPC_ with a creature RACE) draws with its own skeleton and skin, loops its own idle and walk/run clips,
fights with its own attack, reaction and death clips through the existing `CombatRules`, and can be killed. Off by default
behind `[Game] neutral actors` plus the pack setting `[extension.skyrimse] creatures` (declared in `extension.json`).

Inherited code touched: `game_packs/skyrimse/src/appearance.cpp` (`makeActorAppearance`), `combat.cpp` (`beginAttack`,
`applyHit`, the constructor; slot lookups through `clipOf`), `world_view.cpp` (`fighter`, `describeActor`),
`apps/openmw/mwrender/scene.cpp` (`attachNeutralIdle`), `skyrimclipmeta.*`, `skyrimpose.*`, `mwphysics/physicssystem.*`
(`addNeutralActor`, `setGoal`), `mwclass/neutral.cpp`, `sdk/include/openeden/game/actor_appearance.hpp`.

## 1. Survey (what the code did with a creature before)

- `makeActorAppearance` (appearance.cpp) is generic: skeleton from the RACE (`race->skeleton(sex)`), skin from the NPC's WNAM
  or the race's skin ARMO, addons by `ArmatureRecord::appliesTo(race)`. It refused a creature whose female skeleton field is
  empty ("the race has no skeleton model for this sex") and dropped a skin addon whose model for the sex is empty (a wolf
  addon has only MOD2, the male model). Everything else drew.
- `Scene::attachNeutralIdle` (scene.cpp) loaded `animations/male|female/mt_idle.hkx` of the skeleton's actor folder
  (`HavokPack::clipHkxPath`): for the wolf `actors/canine/animations/male/mt_idle.hkx`, which does not exist: the wolf stood
  in the rest pose. Locomotion strides, speeds and the left-foot phase were the person's constants.
- `SkyrimClipMeta::projectFor` read only `defaultmale` / `defaultfemale`: a creature's clip events (HitFrame) were never found.
- `SkyrimCombat` used the humanoid one-handed table (`1HM_*`) for every actor; the clips do not exist in the wolf's folder.
- The capsule was a person's height times the race height (wolf 1.2: 154 units, taller than the wolf is long).
- A death ragdoll needs a bone map (`ragdollTableFor`, scene.cpp): the CreativeMotion rig is a 16-body humanoid, so a creature's
  skeleton gives no table and the engine already logs "No death ragdoll for the skeleton ...", keeping the death clip's last pose.

## 2. The data (five masters, read only; `testskyrimsecreaturesreal.cpp`, lines `SKYRIMSE-CREATURES`)

- 6,361 NPC_ records; creature races have a skeleton outside `actors\character\character assets`. WolfRace (24 NPCs), FoxRace
  (804 NPCs: leveled-template stand-ins such as `LvlAmbientCreatures`, `LvlAnimalCanyonPredator`, guards; the real race comes
  from the template), C06WolfSpiritRace and the death hounds share `Actors\Canine\Character Assets Wolf\skeleton.nif`.
- WolfRace: skeleton `Actors\Canine\Character Assets Wolf\skeleton.nif` (both sexes), behaviour graph
  `Actors\Canine\WolfProject.hkx`, skin addon `NakedWolfAA` (slot 4, model `Actors\Canine\Character Assets Wolf\wolf.nif`, male
  model only), health 12, stamina 200, height 1.2. EncWolf (id 0x23abe = 146110): no template, AI present, level 2, combat
  style `csWolf`.
- Clips: `meshes/actors/canine/animations/` (no sex folder; the dog shares it). Sidecars: `animationdata/wolfproject.txt`,
  `animationdata/boundanims/anims_wolfproject.txt` (the project name is the skeleton folder's suffix + `project`).
  Sidecar generator names equal the file stems for the clips used: `Attack1` (weaponSwing 0.2, preHitFrame 0.267, HitFrame
  0.433, 0.867 s), `AttackPowerStanding` (HitFrame 0.6, 1.333 s), `Recoil` (0.667 s), `StaggerBackSmall` (1.167 s), `Death`
  (0.333 s, event `Ragdoll` 0.267), `RunForward` (0.533 s). The walk generator `WalkForward00_Wolf` is not a file stem; its block
  gives the stride.
- Root motion (cache indices 67 and 46): walk 89.448 units in 1.2 s (74.54 units/s), run 300.081 units in 0.5333 s (562.65 units/s).
- Clip choice: `mt_idle_wolf`, `walkforward_wolf`, `runforward`. The behaviour graph that chooses them is not decoded; this is the
  pack's pick by name (ESTIMATE).
- Wolves in the exterior: EncWolf placements in Tamriel cells (6,-8) at (26008,-32560) and (26378,-30912), (5,-9), and
  `LvlAmbientCreatures` stand-ins near Whiterun (leveled-list bases of ACHR are not resolved by the neutral world: they are
  `LeveledList` objects and not drawn).
- EncWolf's AIDT aggression is 0 and "hostile on sight" is false in the pack's reading, so a wolf does not attack by itself with
  `combat factions` alone (a decision for toddhow; the run used `combat test hostile = EncWolf`).

## 3. Design

- `creatures.hpp/.cpp`: `CreatureProfile` (names of project, idle, walk, run, the five combat clips, speeds, strides, capsule height)
  and `creatureProfileFor(skeletonPath)`; only the wolf. The capsule height 80 is an ESTIMATE.
- SDK `actor_appearance.hpp` 0.4 (additive trailing fields): `animationProject`, `idleClip`, `walkClip`, `runClip`, `walkSpeed`,
  `runSpeed`, `walkStride`, `runStride`; all empty or 0 for a person, so the core plays what it always did.
- `AppearanceLimits::mCreatures`: fills the fields, sets `heightScale` from the profile capsule, and lets a sex without a skeleton
  or skin model take the other sex's.
- Engine: `attachNeutralIdle` uses the named clips with no sex folder and `Gait::Params` of the creature;
  `SkyrimPose::setProject` / `SkyrimClipMeta::loadAsync(..., project, ...)` read the creature's sidecar;
  `PhysicsSystem::addNeutralActor` takes the creature's walk and run speeds for `goTo`.
- Combat: `FighterProfile::mCreature` (set by `SkyrimWorldView::fighter` only with the setting on); `SlotTable::wolf()` (type 0,
  no weapon: Attack, PowerAttack, Recoil, Stagger, Death); `SkyrimCombat::clipOf` picks the table; a creature's death plays its
  `Death` clip. Damage and reach are the race's unarmed numbers (`FighterProfile::mUnarmedDamage/Reach`, existing).
- Death: no ragdoll (no skeleton map for a creature: the engine logs it and holds the last pose of `Death`, a collapsed crouch).

## 4. Stages and evidence

1. Tests first (`testskyrimsecreatures.cpp`, 15 tests): run red against stubs (creature lookup returning null, appearance and
   combat ignoring the profile): 7 of 15 failed, the 8 that passed test absence (people, setting off, humanoid table). All 15
   pass with the code; one test helper bug (a hostile must be re-added after the setting) fixed in the test only.
2. In game (`tools/neutral_session_check` scenario `n5wolf`, new; `I:\Projects\OpenEden-work\combatai\wolf1.sh`): the player
   stands 320 units east of EncWolf at `@Tamriel,26008,-32200`, daylight, `combat ai`, `combat test hostile = EncWolf`,
   `combat test player swing = 2`. Frames (`w2_montage.png`, `w2_montage2.png`, `w2_death.png` in the work folder): the wolf
   walks and runs with the wolf cycles, rears for `AttackPowerStanding`, bites with `Attack1`, recoils, and dies in a crouch.
   Trace: wolf damage 10 (power) and 5 (normal) to the player, player 4 per hit; death at health 0 of 22; the wolf's `Attack1`
   events at 0.033, 0.2, 0.267, 0.3, 0.433 (HitFrame) s: the creature sidecar is read.
3. Determinism: 3 runs of the same scenario: runs 1 and 3 are byte-identical (combat and NCOMBAT lines), run 2 differs in one early
   event (a Recoil `FootFront` event and the order of one swing); the `NACTGOTO path status` lines of the wolves far away differ
   in order between runs (path timing, known). Not met: three identical runs.
4. Off: `creatures = false` in the same scenario: the wolf asks for `1HM_*` clips as before (traces show `1HM_AttackPower`).
   Pure checks: unit tests pin that appearance, the humanoid table and a person's fighter are unchanged.
5. Morrowind unchanged: `tools/worldobjects_check` OK (110 objects) and the fixed Balmora frame SHA-256 `1c5d47ad0b5dc32d`
   for the older reference build (`ref`), this build, and this build with the settings on.

## 5. Other creatures (count over the five masters)

By a prefix test of the archive listings (`creatsurvey.py`), of 112 race rows with a non-person skeleton, 34 races (996 of 1,939
NPCs) have a skeleton, a skin model, the sidecar project and idle, walk, run and attack clips under their actor folder: for
example bear, skeever, sabre cat, horse, deer, goat, mammoth, giant, hagraven, dog (some by name differences). None works as is:
a `CreatureProfile` per skeleton is needed (the wolf is the only one), because clip names differ (`attack1` against `attack`,
`mt_idle` against `idle`, walk and run generators). The rest (draugr, dragon, falmer, troll, werewolf, ...) either live in the
humanoid folder or have different sets (flying, summons). Each one needs a decision on the capsule and on the walk and run clips.

## 6. Not done / not run

Linux gate, clang-format 14, clang-tidy, Lua tests, the whole `openmw-tests`, a fresh review, the parent-exact "off" comparison
(no parent build exists; `ref` is an older build), dog and other creatures, the female wolf in game, leveled actor placements
(`LvlAmbientCreatures` has no drawing path), sneaking up, flee and howl behaviour, wolf packs, swimming.

## 7. Decisions for toddhow

- EncWolf has aggression 0: should wolves be hostile by faction relation (the AI rule) or by a creature rule?
- The mover speeds are the clips' own (walk 74.5, run 562.7 units/s); a person's are 82 and 351.
- Death keeps the collapsed pose (no ragdoll for creatures without a rig).
- SDK `actor_appearance.hpp` is 0.4. Combat contract unchanged; no pin test edited.
