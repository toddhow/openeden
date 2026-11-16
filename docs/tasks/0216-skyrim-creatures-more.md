# Task 0216: Skyrim creatures, second slice: nine more creatures and leveled stand-ins

Design doc section: `docs/design/game-packs.md` (the Skyrim SE pack), decision 0017 (neutral actors), task 0212 (the wolf).
Branch `creatures2`, from `81dd1240c7`.

Goal: the profile table of task 0212 holds ten creatures (wolf, dog, bear, skeever, horse, sabre cat, deer, goat, mammoth,
giant), easy to extend by one row; each draws with its own clips, fights with its own attack, reaction and death clips,
and a leveled stand-in (an NPC whose template is a leveled list) fights as the creature it shows. Still off by default
behind `[Game] neutral actors` plus `[extension.skyrimse] creatures`.

Inherited code touched: `game_packs/skyrimse/src/creatures.cpp` and `creatures.hpp` (the table), `combat.cpp` (`clipOf`,
the fighter lookup), `combat.hpp` (`CombatData::fighterFor`), `world_view.cpp` and `.hpp` (`fighter` became `fighterFor`),
`skyrimse_game_pack.cpp` (one log line). No engine, SDK or contract file changed.

## 1. What was found

- **There are no leveled-list actor placements.** Over the five masters 12,978 ACHR were scanned (`SkyrimCreaturesMeasure.
  whereTheCreaturesStand`): 12,973 name an NPC_, 5 name a record that is neither an NPC_ nor a leveled list, **0 name an LVLN**.
  Task 0212's "leveled-list bases of ACHR are LeveledList objects and are not drawn" was wrong: `LvlAmbientCreatures`,
  `LvlAnimalForestPredator`, `LvlGiantNoAggro` ... are NPC_ stand-ins whose template is a leveled list, and
  `ActorIndex::resolve` already picks the entry per placement (the engine passes the placement id as the variation,
  `mwclass/neutral.cpp`, `describeActor`). They were drawn all along.
- **The real defect was in combat.** `SkyrimWorldView::fighter(base)` resolved the template with pick 0 and the stand-in
  player's level, so a stand-in the screen showed as a bear fought with the clips of whatever entry pick 0 gave. Now
  `CombatData::fighterFor(base, variation, out)` (a new virtual with a default that forwards to `fighter`, so older fakes
  build) is called with the low 32 bits of the placement id, the number the appearance used; with `creatures` on the pick
  is that number, the profile is chosen at the appearance's level (`kAppearanceLevel`), and the stats and armour use the
  same pick. With `creatures` off the pick stays 0 (unchanged). A new trace line `COMBAT creature <actor> is a <name>` shows
  the result.
- Whiterun's hold resolves to many kinds in one run: the stand-ins near (25611,31480) gave deer, wolf, sabre cat, horse,
  mammoth and giant in the bear, sabre cat and goat runs below.

## 2. The table (every number from the installed data)

One row per skeleton folder in `creatures.cpp`. `creatureProfiles()` lists them, `creatureProfileFor(skeleton)` finds one,
`creatureClipFor(profile, slot)` is the slot table (Attack, PowerAttack, Recoil, Stagger, Death; a creature has no block,
bash, cast or bow clip). `SlotTable::wolf()` stays (its tests pin it) and equals the wolf's row (tested).

How it was measured (`apps/components_tests/skyrimse/testskyrimsecreaturesclips.cpp`, gated by `OPENEDEN_SKYRIMSE_DATA`,
prints `SKYRIMSE-CREATURE-CLIPS`): for every row, all eight clips exist in `Skyrim - Animations.bsa`; the sidecar project and
its motion file exist; the walk and run stride equals the last translation row of the block the clip links to (1 percent)
or, where the generator's name is not the file's stem (wolf walk, dog run, giant run), the block of the same duration (listed
below); both attack clips link to a generator with a `HitFrame` event. The test passes for all ten rows.

| creature | project | idle | walk (stride / s) | run (stride / s) | attack | power | recoil | stagger | death | capsule |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| wolf | wolfproject | mt_idle_wolf | walkforward_wolf (89.448 / 1.2, by duration) | runforward (300.081 / 0.5333) | Attack1 | AttackPowerStanding | Recoil | StaggerBackSmall | Death | 80 |
| dog | dogproject | mt_idle | walkforward (89.448 / 1.2) | runforward (300.081 / 0.5333, by duration) | attack1 | attackpowerstanding | recoil | staggerbacksmall | death | 76 |
| bear | bearproject | mt_idle | walkforward (99.714 / 1.667) | runforward (382.846 / 0.6) | attack1 | attackpowerstanding | recoil | staggerbacksmall | death | 192 |
| skeever | skeeverproject | mt_idle | walkforward (37.347 / 1.0) | runforward (260.128 / 0.5333) | attack1 | attackpowerforward | recoil | staggerbacksmall | staggerbacklarge | 57 |
| horse | horseproject | idle | walkforward (137.624 / 1.1) | runforward (212.32 / 0.4667) | attack2 | attack1 | recoil2 | staggerbacksmall | staggerbacklarge | 169 |
| sabre cat | sabrecatproject | mt_idle | walkforward (162.094 / 1.433) | runforward (359.399 / 0.7333) | attack1 | attackpowerforward | recoil | staggerbacksmall | staggerbacklarge | 117 |
| deer | deerproject | mt_idle | walkforward (169.802 / 1.0) | runforward (500 / 0.6) | attack1 | attack2 | recoil | staggerbacksmall | staggerbacklarge | 214 |
| goat | goatproject | mt_idle | walkforward (62.130 / 1.0) | runforward (360 / 1.0) | attack1 | attackpowerstanding | recoil | staggerbacksmall | staggerbacklarge | 82 |
| mammoth | mammothproject | mt_idle | walkforward (247.36 / 4.0) | runforward (400 / 1.0) | attack1 | attackpowerstanding | recoil | staggerbacksmall | deathpose | 376 |
| giant | giantproject | idle | walkforward (164.134 / 2.667) | runforward (415 / 1.0, by duration) | attack1x2 | attack_stomp | stagger_small | stagger_large | deathpose | 267 |

- **Capsule heights are measured**, not guessed: `SkyrimCreaturesMeasure.boundsOfTheCreatureSkins` flattens each skin model
  in its rest pose (`NifScene::Flat`, `mSkinnedAtRest`) and prints the bounds: z extent wolf 79.6 (the old estimate was 80),
  dog 75.9, bear 191.8, skeever 57.4, horse 169.0, sabre cat 117.2, deer 213.6 (antlers included), goat 81.8, mammoth 376.2,
  giant 267.3. The radius is the core's 22 for every creature (see the decisions).
- **Death clip:** where a creature has a `death` or `deathpose` file that is the row. The deer, goat, skeever, horse and sabre
  cat have none: their sidecars' own `Death` generator (deer: cache 13 = StaggerBackLarge, ragdoll event at 0.733 s) plays
  the large stagger before the ragdoll, so the row uses `staggerbacklarge`. The engine has no ragdoll for a creature
  skeleton, so the body keeps that clip's last pose (ESTIMATE).
- **Giant:** the giant's generators are named `attack1_start` and `attack1_end`, not `attack1`, and the pack reads events by
  the file's name, so `attack1` would deliver no `HitFrame`. The two attack clips whose names agree (`attack1x2`,
  `attack_stomp`) are used; the recoil and stagger are `stagger_small` and `stagger_large`.
- **Not in the table:** draugr, dragon, falmer, troll, hagraven, chaurus, spiders, atronachs, werewolf, hare, chicken, cow
  (their clips are not found by these names or they have flying or summon sets). Each is still drawn in the rest pose as
  before (cows stand in the frames below).

## 3. Stages and evidence

1. Tests first (`testskyrimsecreaturesmore.cpp`, 19 tests; `testskyrimsecreaturesclips.cpp` and
   `testskyrimsecreaturesmeasure.cpp` are gated real-data runs). The new API was added as stubs first (`creatureProfiles()`
   returned only the wolf, `creatureClipFor` returned empty, combat called `fighter`): **12 of the 19 failed**, the 7 that
   passed test absence and controls (a complete row, unknown skeletons, a person's clips, the setting off, the default
   forwarding of `fighterFor`). After the code all 19 pass.
2. Existing tests: `SkyrimCreatureProfile.peopleAndUnknownCreaturesHaveNoProfile` (task 0212,
   `testskyrimsecreatures.cpp` lines 38 and 39) asserts that the dog's and the bear's skeletons have NO profile; they now
   have one, so it fails. **Not edited; it needs toddhow's approval** (delete those two lines). Nothing else changed:
   `components-tests` filter `*Skyrim*:*Creature*:*Combat*:*Extension*:*Neutral*:*Frozen*` 2,360 pass, that one fails.
3. In game: see section 4.

## 4. In game

Scenario: `I:\Projects\OpenEden-work\combatai\cr1.sh` (task 0212's `n5wolf`: the player stands east of the creature and
faces it, a side camera, a picture every 0.2 to 0.3 s), `combat ai`, `combat factions`, `combat test player swing = 2`,
`combat test hostile = <editor id>`, `[Sound] master volume = 0.0`, fixed frame time. Evidence montages (frames viewed) in
`I:\Projects\OpenEden-work\combatai`: `c_bear_m.png`, `c_sabre_m.png`, `c_goat_m.png`, `c_dog_m.png`, `c_deer_m.png`,
`c_horse2_m.png`, `c_mammoth5_m.png`, `c_giant5_m.png`. The trace lines are `COMBAT creature <actor> is a <name>` (new), `COMBAT ai
engage`, `COMBAT swing`, `COMBAT damage`, `COMBAT reaction`.

| creature | placement used | seen in frames | combat in the trace |
| --- | --- | --- | --- |
| bear | `EncBearSnow` (68012,-76095) | walks, circles and bites, rears for the power attack | `is a bear`, three bites of 45 kill the stand-in player in 1.2 s |
| sabre cat | `EncSabreCat` (433,744) | walks and prowls, bites with a head-forward lunge | normal 35, power 70; `reaction clip recoil` twice |
| goat | `EncGoatDomestic` (32234,27340) | stands, turns, runs (retreats at 45 percent health) | `reaction clip recoil`, `Advance -> Retreat` |
| dog | `Stump` (23490,-46185) | runs and circles | damage 8 to the player, `reaction clip recoil` |
| deer | `dunCGEncDeer` (18533,-89571) | two elks stand with antlers (the camera then turns away: the player moved) | engaged, no attack seen |
| horse | `HorseForCarriageNew` (17929,-12701) | stands at its cart (drawn with the horse skin and idle) | engaged, confidence 0, no attack seen |
| mammoth | `EncMammothTamed` (519,3950) | walks towards the player | swing, damage 65 and 130 |
| giant | `LvlGiant` (stand-in at (519,3950)) | two giants fight at a campfire | `swing`, damage 60 to the player |
| skeever | `LvlAnimalHillsPredator` stand-in | **not seen** (that stand-in resolved to bear and deer; the Lua found no target) | `COMBAT creature ... is a skeever` for another placement |

Facts the runs showed:

- **Leveled stand-ins now fight as what they show.** In one run the stand-ins of one hold resolved to deer, wolf, sabre cat,
  horse, mammoth and giant (`COMBAT creature actorNNN is a ...`), and `LvlAnimalHillsPredator` gave a bear (902564) next to a
  skeever (902566).
- Cows, hares, chickens and the other races with no row are drawn in the rest pose as before (cows are in the goat frames).
- A bear (45 a bite), a mammoth (65, 130) and a giant (60) kill the player in a second or two; the player (4 a swing) cannot
  kill them in a run, so **no death frame was taken for any creature of this slice** (the wolf's was seen in task 0212, the
  dog, goat, deer and skeever have health 10 to 50 but their fights were not run to the end). The trace line `COMBAT death`
  for a creature was therefore not checked here; the death clip of every row is pinned by unit tests and by the data (the
  file exists).
- The first mammoth and giant runs gave no frames: the stand-in player was killed before the Lua scenario teleported him
  (the creature is hostile from frame 1); a start 3,000 units away from another placement worked.
- One run of the Morrowind frame with the settings on gave a different hash (`2e0ab54b977c90be`) right after other runs; it
  did not repeat (three runs, `1c5d47ad0b5dc32d`, and the same with the pack built from the parent sources).

## 5. Not done / not run

Linux gate, clang-format 14 (formatted by hand), clang-tidy, the Lua tests, `openmw-tests`, a fresh review, a death frame for
the big creatures (the player cannot kill a bear: see section 4), a walking creature frame for every creature, determinism
runs (three of the same scenario), the female creature, creature ragdolls, swimming creatures, flying creatures.

## 6. Decisions for toddhow

- Delete the dog and bear lines of `SkyrimCreatureProfile.peopleAndUnknownCreaturesHaveNoProfile` (see 3.2).
- The capsule radius is 22 for every creature. A mammoth (about 200 wide) and a giant (about 140) are wider than that and
  a horse or a mammoth is much longer than wide; a per-creature radius needs an SDK field (`actor_appearance.hpp`) and a
  change in `PhysicsSystem::addNeutralActor`.
- Creatures without a death file fall with `staggerbacklarge` and stay in its last pose until a creature ragdoll exists.
- A giant uses `attack1x2` and `attack_stomp` because its other attacks have no events by the pack's linking rule.
