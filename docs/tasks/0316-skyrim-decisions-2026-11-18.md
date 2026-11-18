# Task 0316: toddhow's decisions of 2026-11-18 ("go with your recommendations")

Status: stage log (same hand wrote tests and code; Windows only; local branch `decisions-0316` from `4ac6fb8c89`, not
pushed). One commit per item.

## Items

1. **`scene park seconds`** (task 0287). `SceneRunner::parkSecondsFor(setting, questAliasPackages)`: a set value above 0
   wins; else 5 s with `quest alias packages` on; else 0 (the runner keeps its own 120 s). `extension.json` keeps default
   0.0 (it means "auto"); the json has no description field. Test `SceneRunnerPark.questAliasPackagesDefaultToFiveSecondsAndASetValueWins`
   (compile-red: the function did not exist).
2. **`enemiesNear`** (task 0282). With the combat AI on only engaged actors count (the loop over `mAiActors`); the loop
   over `mHostiles` (a `combat test hostile` actor that never engaged) is skipped. With the AI off a living hostile still
   counts (it attacks through `startHostiles`). Test `CombatRest.withTheAiOnAHostileThatNeverEngagedIsNotNear`.
3. **Renames** (name only): `WorldSourceService.theVersionIsOneZero` -> `theVersionIsOneThree`;
   `CarriageNativesTest.theCartSeatCountsAsTakenAndNobodyIsInAFactionOrAFightEvenThoughIsInCombatStaysTolerant` ->
   `theCartSeatCountsAsTakenAndNobodyIsInAFactionOrInCombat`.
4. **Werewolf power attack** (task 0279). `werewolf::playerProfile()`'s power clip is `werewolfbeast::ww_attackright`
   (generator `AttackRightPower_State`), no longer the three-hit combo (the combat already used `playerBeastProfile()`,
   which now equals it). **Existing test lines edited** (approved): `testskyrimsewerewolf.cpp` line 165
   (`ww_attackrightpowercombo` -> `ww_attackright`) and `testskyrimsebeastclips.cpp` line 52 (the same).
5. **Bandits attack the player on sight** (task 0293). See below.

## Item 5: what the data says

Read with houseCARL on the five masters' winners:

- `BanditFaction` has 15 relations and none to `PlayerFaction`; `PlayerFaction` has 85 and none to `BanditFaction`.
- The player's NPC record is in `PlayerFaction` (and bed, charm and allegiance factions), AIDT Unaggressive; not in
  `PreyFaction`.
- Of the bandit NPCs most are **VeryAggressive** (AIDT 2: attack Enemy and Neutral, so they already attack on sight with
  the stated-relation rule). Only six `BanditFaction` NPCs are Aggressive (AIDT 1): `E3DemoTowerBanditMissile/Melee`,
  `MS03ChaletGuard*` (these three also carry `MS03ChaletGuardEnemyFaction`, an explicit Enemy of `PlayerFaction`) and
  `EncBanditDogAggro1024`. So "bandits never attack the player" holds for the Aggressive ones without such a faction.
- Wild animals are mostly Unaggressive (`EncWolf`, `EncSabreCat`, `EncBear`, `EncDeer`), `EncSkeever` Aggressive, none
  related to `PlayerFaction`: the data alone does not say how the real engine makes wolves attack; this task does not
  touch animals.
- Town soldiers, citizens and guards carry a crime faction (NPC CRIF) or `IsGuardFaction`; the street guards of Whiterun
  are Unaggressive (task 0197).

No relation rule in the data makes an unrelated Aggressive actor hostile. **The rule built is a guess, RECALLED-VERIFY**
(the Creation Kit wiki could not be read): `lawlessHostileRelation` (`actor_mechanics.*`): an actor with AIDT Aggressive,
a Neutral relation to the player, no crime faction (after templates) and no `IsGuardFaction` membership is an Enemy of the
player. Behind `[extension.skyrimse] combat hostile factions` (off by default, in `extension.json`, README line in
`tools/testprofile/`; a pack setting, so not in `profile.cfg`; read inside the `combat ai` block). It changes
`relationToPlayer` of the actor's disposition, so `hostileOnSight`, followers' targets and `sideRelation` follow.

Tests: `SkyrimLawlessHostile.*` (2, compile-red against the missing function).

## Evidence

Build: `I:\Projects\OpenEden-work\ragdoll5\build` (this worktree). Harness: `I:\Projects\OpenEden-work\dec316` (the task 0288
sweep scripts with a longer settle and a `combat trace` setting; plans `plan_camp.lua`, `plan_towns.lua`).

- Halted Stream Camp (`@Tamriel,16500,18918,-3700`), `combat ai`, `combat factions`, `combat trace`, 100 s settle:
  - off: no `COMBAT ai engage` line in the run (an earlier 12 s run showed only the Very Aggressive wizard and two animals).
  - on: `LvlBanditMissileAggro1024` (AIDT 1) actors 329038 and 329011 engage on sight from 0.3 s, bark ("Hey!", "Never
    should have come here..."); the frame (`run_camp_on2/user/screenshots/screenshot000.png`) shows their barks.
- Towns (Whiterun, Kynesgrove, Rorikstead, 40 s settle each), off and on: the same single `engage` line (two predators of
  `LvlAnimalCanyonPredator` against each other) in both; no guard, soldier or citizen engages the player.
- Tests: `SceneRunnerPark.*`, `CombatRest.withTheAiOnAHostileThatNeverEngagedIsNotNear` (red with the fix reverted: 1 of 1
  failed; green with it), `SkyrimLawlessHostile.*` (2, compile-red), the three werewolf pin lines; wide filter 4,542 pass.
- Morrowind: `tools/worldobjects_check` OK (110 lines); the fixed Balmora frame is `1c5d47ad0b5dc32d` for `integ2` and this build.

## Edits to existing tests (all approved by the task)

`testskyrimsewerewolf.cpp` (line 152: the player's power no longer equals the bare row's combo, which the bare row keeps;
line 165: `ww_attackright`), `testskyrimsebeastclips.cpp` (line 52: `ww_attackright`), and two renamed test names
(`test_world_source_service.cpp`, `testskyrimsecarriage.cpp`).

toddhow confirmed the line 152 edit on 2026-11-18.

## For toddhow

- Item 5's rule is a guess; the data does not say why unrelated Aggressive actors attack the real player. Wild animals
  are untouched (the real wolves are Unaggressive in the data).
- Not run: Linux gate, clang-format 14, clang-tidy, Lua tests, `openmw-tests`, a fresh review, a three-run determinism check.
