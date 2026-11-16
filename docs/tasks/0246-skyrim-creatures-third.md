# Task 0246: Skyrim creatures, third slice: seven more creatures and a collision radius per creature

Design doc section: `docs/design/game-packs.md` (the Skyrim SE pack), decision 0017 (neutral actors), tasks 0212 and 0216.
Branch `creatures3`, from `866c6c70d2`.

Goal: the profile table of task 0216 (ten creatures) gains draugr, falmer, troll, frostbite spider, chaurus, hagraven and
spriggan; every creature gets a collision capsule of its own radius (task 0216 decision 2); a skeever is shown in game.
Still behind `[Game] neutral actors` plus `[extension.skyrimse] creatures` (off by default).

Edits outside the new files: see section 5.

## 1. Survey (installed Skyrim SE data; `SkyrimCreaturesThirdSurvey`, gated by `OPENEDEN_SKYRIMSE_DATA`)

The test prints every generator of a project (name, cache index, block duration, stride, events, HitFrame), every clip
file with its link (`SKYRIMSE-CREATURE3-CLIPS`, `-GEN`), the bounds and mid-body width of 21 skin models
(`-BOUNDS`, `-SLAB`) and where each kind stands in the five masters (`-PLACED`). Race skeletons were read with an
independent Python RACE reader (ANAM = skeleton): DraugrRace, SkeletonNecroRace, RigidSkeletonRace, the Soul Cairn and
Dragonborn draugr races use `Skeleton.nif` / `SkeletonF.nif`; **SkeletonRace (the skeletons) uses `SkeletonS.nif`** in the
same folder and a sibling project `draugrskeletonproject` whose sidecar has the same 410 generators and 214 blocks as
`draugrproject`; the Falmer races, `TrollRace`/`TrollFrostRace`, the three frostbite spider races, `ChaurusRace`,
`HagravenRace` and `SprigganRace` (matron, earth mother, burnt) each name one skeleton for both sexes.

| creature | project | idle | walk (stride / s) | run (stride / s) | attack | power | recoil | stagger | death | height | radius |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| draugr | draugrproject | mtidle | mtforward (76.973 / 1.0) | mtfastforward (180.921 / 0.5333) | 1hmattacka | 1hmattackpowerslash | 1hmrecoil | mtstaggermedium | special_deathbackward | 145 | 18 |
| falmer | falmerproject | mtidle | mtwalkforward (167.438 / 1.6667) | mtrunforward (191.458 / 0.5333) | h2hattack1 | h2hattack2 | h2hrecoilright | 1hmstaggerbackmid | 1hmstaggerbackheavy | 145 | 21 |
| troll | trollproject | mtidle_standing | mtwalkforward (116.63 / 1.1667, by duration) | mtrunforward (179.669 / 0.6667, by duration) | h2hattackrighta | h2hattackrightforwardpower | h2hrecoilrighta | staggerbacka | staggerbackc | 173 | 20 |
| frostbite spider | frostbitespiderproject | mainidle | forward_walk (85.09 / 1.0) | forward_run (300.2 / 1.0) | attack_rightchop | attack_combochop | recoil_bite | stagger_small | death | 98 | 44 |
| chaurus | chaurusproject | mainidle | forwardwalk (142.513 / 1.6667) | forwardrun (350.265 / 1.0) | attackrcbite | attackrheadbash | recoilrbite | staggersmall | death | 124 | 46 |
| hagraven | hagravenproject | idle | walkforward (90.983 / 1.3333) | runforward (89.159 / 0.7667) | attackr | attackl | recoilr | staggersmall | staggerlarge | 128 | 17 |
| spriggan | spriggan | mt_idle | walkforward (95.799 / 1.4667) | runforward (263.173 / 0.7333) | attackright1 | powerattackforwardx2 | recoilr | staggerback_small | death | 156 | 16 |

Facts the survey settled:

- **Attack events:** every attack and power attack above has a generator with a `HitFrame` event (draugr `1hmattacka`
  0.566 s, power slash 0.633 s; falmer `H2H_Attack1_Clip` and `H2H_Attack2_Clip`; troll `h2hattackrighta` 0.6 s; spider
  `Attack_RightChop` 0.566 s; chaurus `AttackRCBite` 0.5 s; hagraven `AttackR` 0.733 s; spriggan `AttackRight1` 0.466 s).
  The falmer's generators are not named like the files, so `SkyrimClipMeta::sidecarAliases` adds `H2H_Attack<n>_Clip` for
  `h2hattack1` and `h2hattack2` (the lookup is still by duration; a clip that has a generator of its own name is unchanged).
- **Draugr** keep their `1hm*` clips in their own folder, so the creature row is the weapon row. The draugr folder holds
  three skeleton files; the row's folder is a prefix of the skeleton path (`.../character assets/skeleton`), so it matches
  `skeleton.nif`, `skeletonf.nif` and `skeletons.nif` and not a DLC subfolder. Draugr and the skeleton race are people
  with outfits and held weapons: the appearance code keeps drawing their parts and the held weapon; only the clips, speeds,
  height and radius come from the row. With ragdolls on, their humanoid skeleton resolves a bone table, so they fall as ragdolls.
- **No death file:** falmer, troll and hagraven. The row uses a long stagger (the same rule as task 0216 for the deer);
  the draugr use `special_deathbackward`, the spider, chaurus, spriggan their `death`.
- **Walk and run:** the troll's files have no generator of their own name (stride read from the block of the same duration:
  `MTForward` 1.16667 s, `MTFastForward` 0.666667 s). The hagraven's walk and run strides are close (91 and 89 units) but
  the run is faster per second (116 against 68).
- **Not built, with the reason:** the **werewolf** (`werewolfbeast`): its generators are named `WW AttackLeft.hkx00`,
  `RightAttack00` and so on, no clip file's stem matches any, so no `HitFrame` would arrive; it needs a general alias
  rule and a decision on which of its 80 attack clips is "the attack". Dragons, atronachs, dwarven spiders and the rest of
  task 0216's "not in the table" list are unchanged.
- **Where they stand** (interiors, for the in-game runs): draugr `LvlDraugrMelee1HMale` (base 350547) and frostbite
  spiders `EncFrostbiteSpider` (146090) in `Ansilvund01`; falmer `LvlFalmerMelee` (124850), chaurus `EncChaurus` (677376)
  and skeever `EncSkeever` (146103) in `TolvaldsCave03`; troll `EncTroll` (146106) in `ClearspringTarn01`; hagraven
  `EncHagraven` (146096) and spriggan `dunSnaplegSpriggan` (645787) in `SnaplegCave01`; mammoth `EncMammothTamed` (906817)
  at `@Tamriel,-38551,-19588`. The cave frames are dark (a gamma was applied to the montages).

## 2. The collision radius

A capsule is round, a creature is not. The rule used for every row (`SkyrimCreaturesThirdSurvey.boundsOfEverySkinForTheRadius`
prints the inputs): **half the smaller horizontal extent of the skin's mid-body** (the vertices between 20 and 60 percent
of the height, rest pose, so a person's raised arms and a deer's antlers are out), **at least 16** (`kCreatureMinRadius`,
ours: no needle capsules) and **at most 45 percent of the total height** (`NeutralActor::kMaxRadiusFraction`, so the
capsule stays a capsule). Results (x and y extent of the slab, then the radius): wolf 28.1 / 87.9 -> 16 (14.0), dog 28.4 /
86.4 -> 16, bear 161 / 136 -> 68, skeever 26.3 / 131 -> 16, horse 63.2 / 148 -> 32, sabre cat 64.6 / 202 -> 32, deer 68.5 /
201 -> 34, goat 27.6 / 102 -> 16, mammoth 200.5 / 572 -> 100, giant 138 / 64 -> 32, draugr 80.7 (arms) / 36.4 -> 18,
falmer 137 (arms) / 42 -> 21, troll 52.4 / 40.4 -> 20, spider 116 / 136 -> 58, cut to 44 by the height rule, chaurus 92.7 /
201 -> 46, hagraven 138 (arms) / 33.9 -> 17, spriggan 91.7 / 21.0 -> 16 (10.5 measured).

For a person-shaped creature the arms hang in the slab, so its smaller extent is the depth; for a quadruped it is the
width. That is a rule of thumb, not a measurement of a body's footprint (a long creature overhangs its capsule).

Plumbing: `CreatureProfile::mCapsuleRadius` -> `ActorAppearance::capsuleRadius` (SDK `actor_appearance.hpp`, additive,
version 0.5 -> 0.6; 0 means the core's own radius, so a person and a pack that does not say are unchanged) ->
`MWClass::Neutral::insertActorRendering` -> `PhysicsSystem::addNeutralActor(..., radius)` ->
`NeutralActor::capsuleFor(heightScale, radius)`. The sweep candidates and the navigator agent already read the entry's
capsule (`NeutralCombatWorldImpl::collectCandidates`, `neutralActorAgentBounds`), so a mammoth is hit at its own width and
gets a navmesh agent of its own (the first path of each new radius builds a navmesh: up to 18 radii exist, most in no one
session). `OPENEDEN_NEUTRAL_ACTOR_TRACE=1` logs `NACTCAPSULE <ref> radius R half cylinder H` for each body.

## 3. Stages and evidence

1. Tests first (red against stubs: `creatureProfilesAll()` returned the first ten, `mCapsuleRadius` 0, version 0.5,
   `capsuleFor(h, r)` ignored the radius, no alias). Pack side `testskyrimsecreaturesthird.cpp` 15 tests: **11 failed**, the 4
   that passed test absence (unique folders, unknown skeletons, setting off, a person). Engine side
   `testneutralactorradius.cpp` (6 tests: **3 failed**, the 3 that passed pin the old behaviour and a bad radius) and
   `testskyrimclipaliasfalmer.cpp` (3 tests: **2 failed**). After the code all pass.
2. `components-tests` filter `*Skyrim*:*Creature*:*Combat*:*Extension*:*Neutral*:*Frozen*`: 3,088 pass, **1 fails**.
   `openmw-tests` `NeutralActor*:SkyrimClipAlias*` 81 pass.
3. In game (`tools/neutral_session_check` `n5wolf`, a copy of the task 0216 runs with `[Game] neutral actors`, `neutral actor
   idles`, `neutral actor locomotion`, `neutral combat`, pack `creatures`, `combat ai`, `combat factions`, `combat test
   hostile = <editor id>`, `combat test player swing = 2`, volumes 0.0, fixed frame time; scripts in
   `I:\Projects\OpenEden-work\ragdoll5\cr3run.sh`). Montages (frames viewed) in `ragdoll5\cr3\keep`: `draugr_m.png`,
   `spider_m.png`, `troll_m.png`, `rest5_m.png` (falmer, chaurus, **skeever**, hagraven, spriggan), `rest3_m.png`,
   `rest2b_m.png` (exterior hagraven and spriggan), `mammoth_m.png` (collision debug view).

| creature | seen in frames | trace |
| --- | --- | --- |
| draugr | walks, lunges and swings among the Ansilvund draugr | `is a draugr`, 39 player swings, damage lines both ways |
| skeleton race | a skeleton walks beside a troll (Clearspring Tarn) | `is a draugr` (the same row) |
| frostbite spider | two spiders walk, circle and chop | `is a frostbitespider`, `reaction clip recoil_bite`, 44 swings |
| troll | stands and turns, a frost troll beside it | `is a troll`, engaged |
| falmer | crouches (its idle), partly behind rock | `is a falmer`, `ai engage`, `no path` in the cave |
| chaurus | crawls, jaws and legs moving | `is a chaurus` |
| **skeever** | a skeever runs on a mossy ledge (third row of `rest5_m.png`) | `is a skeever` |
| spriggan | exterior: walks upright among trees, arms out | `is a spriggan` |
| hagraven | **not seen**: an exterior and a cave attempt were blocked by a hide rack and a wooden structure | `is a hagraven`, engaged (no swing seen) |

   Radii in game: `NACTCAPSULE` shows draugr 18, falmer 21, spider 44, chaurus 46, skeever 16, mammoth 100 (half cylinder 88).
   The collision debug view of a tamed mammoth shows a capsule as wide as its body (the old one was 44 wide).
4. **Determinism** (fight of one creature, `combat factions = false`, `[Lua] lua num threads = 0`, `[Physics] async num threads = 0`,
   `OPENEDEN_NEUTRAL_COMBAT_TRACE=1`, COMBAT and NCOMBAT lines with timestamps stripped): a mammoth fight three times:
   two runs byte-identical (363 lines), the third different (298 lines: an actor engaged at distance 0.2 at the start of
   the other two did not). Frostbite spider in Ansilvund01 three times in a row: 308, 308 and 304 lines; the first two differ
   only in the order of an `ai lost ... (no way to the target)` line (navmesh not ready), the third in distances at the
   third decimal. **The traces do NOT match in every run** (task 0197 also recorded this for mixed fights); the cause here is
   the moment the navmesh and the cell objects are ready, not investigated, and no run of the parent was made to say whether
   it is new (the settings `[Navigator] async nav mesh updater threads = 1` made it worse). Not claimed.
5. Morrowind unchanged: `tools/worldobjects_check` 110 lines OK against the `integ2` build; the fixed Balmora frame
   (`--start Balmora --random-seed 7`, frame 600) SHA-256 `1c5d47ad0b5dc32d` with defaults and with `neutral world`,
   `neutral actors`, `neutral combat` on.

## 4. Not done / not run

Linux gate, GCC, clang-format 14 (formatted by hand), clang-tidy, the Lua tests, `openmw-tests` as a whole, a fresh review,
a death frame of any new creature (the player cannot kill a 100+ health creature in a run), a draugr or troll blow landing
(the frames show swings, not damage on the player), a hagraven frame, the female draugr, creature ragdolls (draugr with
ragdolls on were not run), the werewolf, creatures with weapons in hand (draugr swing with the held weapon unseen).

## 5. Edits outside the new files

`game_packs/skyrimse/src/creatures.cpp`, `include/game_skyrimse/creatures.hpp` (table, radius, `creatureProfilesAll`),
`src/appearance.cpp` (one line), `src/skyrimse_game_pack.cpp` (the log line counts all rows), `game_packs/skyrimse/CLAUDE.md`;
SDK `actor_appearance.hpp` (field, version 0.6); `apps/openmw/mwphysics/neutralactor.{hpp,cpp}` (overload),
`physicssystem.{hpp,cpp}` (a defaulted last parameter, the trace line), `mwclass/neutral.cpp` (one argument),
`mwrender/skyrimclipmeta.cpp` (aliases); `CLAUDE.md` (changelog row); `apps/components_tests/CMakeLists.txt`,
`apps/openmw_tests/CMakeLists.txt` (new test files); `tools/neutral_session_check/luatest/n5colldebug.omwscripts` and
`scripts/n5/colldebug.lua` (a player script that turns the collision debug view on). No `CombatRequestWriter` virtual was added.

## 6. Decisions for toddhow

- **Approve editing one existing test:** `SkyrimCreatureTable.creaturesThisSliceDoesNotKnowAndPeopleHaveNoProfile`
  (`testskyrimsecreaturesmore.cpp`) lists the draugr and hagraven skeletons as having no profile; they now have one (delete
  those two paths). It was not edited. All other existing tests pass, because `creatureProfiles()` stays the first ten.
- The radius rule (half the smaller mid-body extent, 16 to 45 percent of the height): a footprint rule for a long creature
  would be larger; a bigger capsule blocks the player more.
- Creatures without a death file fall with a stagger and keep its last pose (as in task 0216); draugr and skeletons use
  the ragdoll when ragdolls are on.
- Draugr use their one-handed clips for every weapon (a greatsword draugr swings a one-handed clip); falmer use
  hand-to-hand clips whatever they hold.
- The werewolf: wanted? It needs an alias rule for names like `WW AttackLeft.hkx00` and a choice among its attacks.
