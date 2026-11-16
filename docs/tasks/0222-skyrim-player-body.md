# Task 0222: the player has a Skyrim body

Design doc section: `docs/design/game-packs.md`, tasks 0121 (appearance), 0148 (locomotion), 0184 (equipment), 0220 (held gear).
Goal: in a Skyrim session the player is drawn in third person as a Skyrim actor: race Nord, male by default (`player female`
chooses the sex), the race's skeleton, skin and head, the clothes and armour he has equipped, the weapon and shield he holds,
with the idle, walk and run clips of an NPC and the clip of each swing.
Inherited code touched: `apps/openmw/mwrender/rendermanager.*` (two small hunks), `scene.*` (a split, no behaviour change).

## 1. Survey

* **The player is a stub.** `insertNeutralStubPlayer` (task 0105) makes an NPC `Player` of a stub race with no body parts.
  `RenderManager::renderPlayer` makes a `MWRender::Player` from it, parents its node in `mPlayerGroup` and gives it to the
  camera (`Camera::setAnimation(Player*)`, which reads the head and camera bones in first person). So third person shows
  nothing of him (the stub has no skeleton or part models) and first person has no arms. The stub stays: the camera, the
  mechanics and the transform follow it.
* **Data facts (probe on the five masters, `OPENEDEN_SKYRIMSE_DATA`):** `NordRace` is form `0x13746` (skeletons
  `Actors\Character\Character Assets\skeleton.nif` and `...Character Assets Female\skeleton_female.nif`, skin ARMO `0xD64`),
  the race lists default head parts per sex (male indices 0 to 3: `0x5140E 0x5162F 0x51631 0xE7AEE`; female 0 to 4:
  `0x51176 0x51623 0x5150F 0x51548 0xE4D7C`). The record `Player` (`0x7`) has race Nord, one head part (`0x51507`) and the
  default outfit `0x1697C`; it is a male record, so the sex-neutral choice is the race's own list. The clothes the player
  starts in: `PrisonerOutfit` `0xA1983` = `ClothesPrisonerTunic` `0x3C9FE` (body slot) and `ClothesPrisonerShoes` `0x3CA00` (feet);
  the opening's cuffs (`PrisonerCuffs` `0x103941`) are left out.
* **What exists to reuse:** `ActorAppearance` and `Scene::insertNeutralActor` (task 0121) draw any `ActorAppearance`;
  `SkyrimLocomotion` (0148) blends idle, walk and run by a ground speed; `SkyrimHeldGear` (0220) attaches the weapon and
  shield named by the appearance; `SkyrimPose::playOnce` plays a combat clip. The inventory model (0184) holds the player's
  equipped items and an `equipmentRevision`.
* **The swing.** The player's swing is a hidden clock in the pack (`SkyrimCombat::beginAttack`, task 0171): the bridge
  refuses clips for a player subject and no clip is ever played on him. Routing the clip through the combat writer would also
  deliver clip events on top of the pack's own timing, so the swing is passed on a separate polled channel instead.

## 2. Design

* **Pack.** `player_body.{hpp,cpp}`: pure `makePlayerAppearance(index, PlayerBodyRequest, out, error, limits)` over the
  `ActorIndex`: race skeleton and height scale, race skin addons not covered by a worn piece, the start outfit's pieces whose
  slots no equipped piece takes, the equipped armour (sorted by form id, so the order of the list is not the order of the
  picture, first addon that fits the race and sex), the race's default head parts for the sex in INDX order, and the held
  weapon (melee types 1 to 6 with a model) and shield (the 0220 rules, a two-handed weapon gives none).
  `SkyrimWorldView` keeps the equipment and a revision (`setPlayerEquipment`) and answers `PlayerBodySource`; the game pack
  syncs the inventory's equipped items (crafted gear as its base record) when `equipmentRevision` moves. Setting
  `[extension.skyrimse] player body` (default false). `SkyrimCellSource::playerBody` adds the swing count and clip of the
  combat rules (`SkyrimCombat::playerSwingCount`, `playerSwingClip`; set in `beginAttack`, no change to a rule).
* **SDK** `openeden/game/player_body.hpp` 0.1 DRAFT, a new optional interface found by `dynamic_cast` on the registered
  `WorldCellSource` (nothing pinned changes): `playerBody(state)` (cheap, non-blocking: revision 0 until the actor records
  are in, a swing token and clip) and `describePlayerBody(appearance, error)`.
* **Engine.** `Scene::makeNeutralActorObject(ptr, appearance, player)` is the old body of `insertNeutralActor` returning the
  object (the NPC path is the same code; `player` forces the idle, walk and run controllers and no ragdoll).
  `MWRender::PlayerBody` (`playerbody.*`, owned by `RenderManager`, made on the first frame with `[Game] neutral player body`)
  polls the source, builds or rebuilds the body when the revision changes, copies the stub's translation and rotation every
  frame, feeds the gait the speed the player really moved at and plays the swing clip when the token grows. The body is a
  second child of `mPlayerGroup` beside the stub, only in third person (first person replaces the group's children).
  The pure rules are `playerbodytrack.hpp` (`GroundSpeed`, `SwingWatch`, `RebuildGate`).
* **Settings:** engine `[Game] neutral player body` (default false); pack `[extension.skyrimse] player body` (default false).
  Both in the test profile.

## 3. Stage log

(filled in below)

### Stage log (2026-11-16)

* **Tests first, red runs.** Pack (`SkyrimPlayerBody*`, 18 tests on hand-built records plus one gated real-data test): against a stub
  `makePlayerAppearance` 17 of 18 failed (the one that passed tests the unknown-race refusal), all pass with the code, and the
  gated test passes on the installed masters (male 8 parts, female 9, prisoner tunic and shoes, head parts). Engine
  (`PlayerBodyGroundSpeed`, `SwingWatch`, `RebuildGate`, 12 tests): against a stub header 10 of 12 failed (2 test absence),
  all pass. `Scene::makeNeutralActorObject`, `PlayerBody` and the pack's view/cell-source glue were written with the
  scenario and have no unit test of their own (they need a renderer or the whole pack). No existing test was edited.
* **Default outfit chosen:** `PrisonerOutfit` `0xA1983` (tunic `0x3C9FE`, shoes `0x3CA00`), found by a probe of the five masters.
* **Head:** the race's own default head parts per sex (male 4, female 5 entries): no facegen, no NPC template. The `Player`
  record's own single head part (`0x51507`) is male only.
* **In game** (`tools/neutral_session_check/check.sh ... pbody`, `[Game] neutral actors`, idles, locomotion, combat, inventory,
  `combat test player swing`, `held gear`, fixed frame time, quiet window, hour 12): frames viewed
  - Whiterun street (`@WhiterunWorld,25670,-7633`): the player standing in the prisoner tunic, then dressed (helmet, iron
    cuirass, boots, sword on the hip), male and female (`pb_stm`, `pb_stf` screenshots 002 and 003);
  - the plains outside the walls (`@WhiterunWorld,30720,-12150`, the street is a rocky slope where he cannot walk): walking
    (the stride), running (leaning, long strides) and a swing (`1HM_AttackRight`, sword in the hand): `pb_pl2` 005, 009, 015;
  - the standard third person camera behind him (Bannered Mare, `pb_mare1` 001); the first person frame shows no body.
* **Determinism:** the Lua `PBODY` lines of three runs of the plains scenario are byte-identical (md5 `25ecf08925`). The
  `PLAYERBODY` lines (build revision, swings) hold the same lines in two runs (sorted md5 equal) and differ in ORDER between
  runs: the equip event comes from a global script on another thread, the swing from the pack's clock. The first run had one
  extra swing line.
* **Gait:** walking moves him 150 units a second and running about 270 (the stub player's attributes), the clips' own
  strides are 82.45 and 350.87 (task 0148); the cycle rate follows the measured speed so the feet do not run in place, but
  the walk and the run are not matched to the clips: the feet slip (not measured). Decision 1.
* **Morrowind unchanged** (parent executable copied before any edit): Balmora frame `1c5d47ad0b5dc32d` and a Morrowind
  third-person Balmora frame `66e3262398a11590` are identical for the parent, this build, and this build with
  `neutral player body` and `neutral actors` on. With the Skyrim settings off no `PLAYERBODY` line appears.

### First person: what it shows and what it would take

It shows no body and no arms: the camera follows the stub `Player` object, which has no skeleton, and `setFirstPersonMode` puts
only the stub in the first person group. It needs the race's first-person arms (the ARMA `MOD4`/`MOD5` models, `mMaleFirstPerson`
is already decoded), an arms-only skeleton (`1st person` skeleton of the character assets), an animation controller for it
(idle and the weapon's draw and swing clips, which are first person clips of their own) and the camera tracking its `Camera`
bone. About 3 to 5 days; not built.

### Edits outside the new files

`apps/openmw/mwrender/scene.{hpp,cpp}` (the split of `insertNeutralActor`, two static helpers), `rendermanager.{hpp,cpp}` (the
body is created, updated and added to `mPlayerGroup`), `apps/openmw/CMakeLists.txt`, `apps/openmw_tests/CMakeLists.txt`,
`apps/components_tests/CMakeLists.txt`, `components/settings/categories/game.hpp`, `files/settings-default.cfg`; pack
`world_view.{hpp,cpp}`, `neutral_world.{hpp,cpp}`, `skyrimse_game_pack.cpp`, `combat.{hpp,cpp}` (a swing counter and the last
clip, no rule), `extension.json`, `CMakeLists.txt`; `tools/neutral_session_check/check.sh` (the `pbody` scenario and its
scripts), `tools/testprofile/profile.cfg` and `README.md`; `CLAUDE.md` (changelog row).

### Decisions for toddhow

1. The walk and run speeds are the stub player's, not the clips': match them (task 0148's mover speeds) or move the player at the
   clips' speeds in a Skyrim session.
2. The body shows the weapon and shield even without `held gear` (the player's own always show with his body).
3. Race Nord and the prisoner clothes are named constants; a character creation would replace them.
4. The head has no facegen, no hair colour or tint beyond the head part's own texture set.
5. First person (above).
