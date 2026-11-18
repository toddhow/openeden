# Task: Skyrim word walls do not teach while fighting (follow-up of task 0256)
Approval: toddhow, 2026-11-17 (one existing test edit, below). Same hand wrote tests and code; tests first, run red against stubs; no fresh reviewer.

## What changed
- `Actor.IsInCombat` is a Papyrus native (`papyrus_host.cpp`, before `Actor.HasLOS`, small and additive) that asks `PapyrusWorldHooks::inCombat`, which the pack answers with `SkyrimCombat::scriptInCombatOf(subject)`.
- Rule. NPC: engaged by the combat AI (`scriptInCombat`). Player: `playerInCombat()` (hurt or swinging in the last seconds) OR `enemiesNear()` (a living actor of the combat AI targeting the player and not given up, or any living `combat test hostile` actor, engaged or not).
- Side effects: `Actor.GetCombatState` of the player answers 1 in a fight; shipped scripts that call `IsInCombat` (carriage, others) now get real answers.
- Shipped rule (`WordWallTriggerScript.StartFX`): `dist < innerRadius(300) && !IsInCombat() || dist <= 200`. Inside 200 units the word is taught even in combat; the refusal applies in the 200 to 300 band. The scenario stands the player about 244 units from the look target (POS -7760,60,480).

## Existing test edit (approved)
`apps/components_tests/skyrimse/testskyrimsepapyrushost.cpp`, `a_native_nobody_answers_is_tolerant_logged_once_and_listed`: `Actor.IsInCombat` replaced by `Actor.IsGhost` (confirmed unregistered), lines 165, 624, 695 (`actor.isghost`) and 698. No other existing test edited.

## Evidence
- Red against stubs: 5 of 11 new tests failed (the other 6 pin absence and boundaries). Green: wide Skyrim/Papyrus/Carriage/Companions/Combat/Wall/Neutral filter 4,038 run, 3,930 pass, rest data-gated skips, 0 failed; `openmw-tests` Neutral/Combat/Papyrus filter 869 pass.
- Game (`tools/wordwall_check`, Bleak Falls Barrow, `combat ai`, quiet window, volumes 0), three runs each: calm = "Word of Power learned: Fus" in 3 of 3, identical filtered traces; test hostile `LvlDraugrAmbushMelee1HHelmetMale` engaged (then "no way to the target") = no word learned in 3 of 3, identical traces. The script polled `IsInCombat` about 1,500 times in 8 s.
- Not shown in game: the same run where the fight ends and the word is then taught (the test hostile cannot be killed or reached there); covered by `FightWall` unit tests (fight, then calm, teaches).
- Not run: Linux gate, clang-format 14, clang-tidy, Lua tests, fresh review, Morrowind comparison (no engine file touched; pack DLL only).

## For toddhow
- `CarriageNativesTest.theCartSeatCountsAsTakenAndNobodyIsInAFactionOrAFightEvenThoughIsInCombatStaysTolerant` has a stale name now (not renamed).
- `enemiesNear` counts any living test hostile even if never engaged.
