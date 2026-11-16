# Task: Skyrim wait, sleep and the vital bars (milestone M-S7)

Status: see the stage log at the end. Windows only; local commits on branch `rest` from `c70547f712`, not pushed.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.24 (UI); decision 0015 (UI system), task 0097 (script bindings for game packs); tasks 0185 (journal: the mode-replacement pattern), 0201 (map: a global script's Mutate call), 0206 (health maximum) as the patterns.
Goal: in a Skyrim session the player can wait 1 to 24 hours with a window and the rest key, sleep in a free bed, regain health, magicka and stamina by it, and gets a Rested state; the engine's clock, the weather and the NPCs' schedules follow.
Inherited code touched: none outside `game_packs/skyrimse` and new Lua files (see "Shared files").
Constraints: off by default behind `[extension.skyrimse] rest` and `[GUI] rmlui`; Morrowind's rest window unchanged (the scripts carry `REQUIRES=core.skyrimse.rest>=1.0`, so nothing starts without the pack's namespace; the engine's C++ is untouched).
Out of scope: fast travel, sleeping interruptions, a fade, a bed animation for the player (no first-person body: the player is not posed in the bed), trespass for a bed that is not yours (task 0204 did not build trespass), level up (the skills window confirms it, task 0206; sleeping does not need to).

## 1. Survey (citations at commit `c70547f712`)

- **The engine already has a wait.** The rest key is `A_Rest` (`apps/openmw/mwinput/actionmanager.cpp` `rest()`) and pushes `GM_Rest`, whose window is the MyGUI `WaitDialog` (`mwgui/waitdialog.cpp`). It advances time in one hour steps with `MechanicsManager::rest` and `World::advanceTime`. A bed activation reaches it through `WaitDialog::setPtr(bed)`. In Morrowind that stays as it is.
- **Time.** `World::advanceTime(hours)` (`mwworld/worldimp.cpp`) moves the weather (`mWeatherManager->advanceTime`), the `TimeManager` and recharges items; the Lua global API has `world.advanceTime(hours)` (`mwlua/worldbindings.cpp`), which also calls `fastForwardAi`. `World::doPhysics` hands `now.getHour()` and `now.getDay()` to `PhysicsSystem::setNeutralClock` every frame, and `PackageBrain::evaluate` (`game_packs/skyrimse/src/package_brain.cpp`) picks the package by that clock about once a second per actor (`mReevaluateSeconds`), so after a wait the actors walk to their new places; they are not moved there.
- **Saving the clock.** The engine's `TimeManager` and globals are written by `World::write` in every save (task 0177 section: globals, weather); the pack needs to save only what it owns.
- **The wait key in Lua.** The same trick as the journal (task 0185): the built-in rest key puts the mode `Rest` on the mode stack; a script that sees it replaces it with `Interface` and no MyGUI window (`I.UI.setMode('Interface', { windows = {} })`). No engine edit.
- **Bed activation.** `Neutral::activate` (`mwclass/neutral.cpp`) sends `openeden_skyrimse_bench_opened` (placement, base, name as hex text) for ANY furniture when `[Game] neutral inventory` is on; `crafting_global.lua` already listens. The pack decides what it is. `markersOf(model)` (`world_view.cpp`) reads the BSFurnitureMarkerNode of a furniture model; a sleep marker is type 2 (`furniture.hpp` `MarkerType::Sleep`). Ownership is `SkyrimWorldView::ownerOfPlacement` (task 0204).
- **Regeneration.** `SkyrimCombat::regenerate(seconds)` (`combat.cpp`) regenerates stamina and magicka of every fighter by the race's percent a second and health of non-players. The player's health is the ENGINE's: it is written through a zero-damage `DamageReport` as `syncProgression` does (`combat_skills.cpp`).
- **Combat state.** A fighter has `mCombatUntil` (the linger time of a blow, `SkyrimCombat::kCombatLinger...`); engaged NPCs are `mAiActors` with `mTarget`; test hostiles are `mHostiles`.
- **Vital bars.** `MWGui::replacedHudParts` (`mwgui/hudreplaced.cpp`, task 0188) hides the MyGUI health, magicka and stamina bars in a neutral cell when `[Game] neutral combat` and `[GUI] rmlui` are on; the combat HUD (`game_packs/skyrimse/src/hud.cpp`, `skyrimcombat.lua`) draws the PACK's bars (`mPlayerMagicka`, `mPlayerStamina`). Task 0206's note ("the engine's blue and green bars are on screen and never move") was a run without the replacement.

## 2. Design

1. **Pack** (`rest.*`, new): pure rules (`decideRest`, `clampHours`, `regenSeconds`, `hasSleepMarker`, `bedStateOf`), `RestedState` (the game time at which Rested ends), the save chunk `rest` v1 (one finite f64), and `RestService` with the namespace `skyrimse.rest` 1.0: Read `ready`, `max_hours`, `can_rest` (a reason word or empty), `bed_state(placement, base)` ("none", "free", "owned"), `rested`, `rested_hours_left`; Mutate `set_clock(gameSeconds)` and `rest(hours, regenSeconds, sleeping)`. The pack never reads a clock; the global script gives it the game time. `rest` refuses with the same rules as `can_rest`.
2. **Combat** (`combat.*`): `requestRest(seconds)` queues, `combatUpdate` applies once (`applyRest`) to every living fighter by its own rates out of combat; the player's health goes through the report. `playerInCombat()` and `enemiesNear()` feed `can_rest`.
3. **Lua**: `rest_windows.lua` (window logic, headless-tested), `rest_player.lua` (the `Rest` mode and the events), `rest_global.lua` (the clock, the bed event, `world.advanceTime`). Waiting regenerates over `hours * 3600 / game time scale` real seconds (the engine's own rule for recharging while time passes).
4. **Rested** (stand-in effect): sleeping 1 hour or more grants it for 8 game hours; it multiplies skill experience by 1.1 (`ProgressionService::setGainMultiplier`). Ours: Skyrim's own numbers are not in the data.
5. **Beds**: a free bed opens the sleep window (8 hours by default); an OWNED bed (an owner that is not the player) is refused with "This bed is not yours." Trespass is not built (task 0204), so refusing is the smaller correct change; `[extension.skyrimse] rest test ignore owner` is a test aid.
6. **Vital bars**: nothing to build when the combat HUD is on: the engine bars are hidden and the pack's follow the pack's values. Verified in game after a spell and a power attack. Without the combat HUD the engine bars are the engine's (not followed); a magicka/stamina write hook through `mwphysics` and `worldimp` was NOT added (full loop, other agents in that area tonight).
7. **Save**: the clock is the engine's; the Rested expiry is the chunk. A save and load keeps both.

## 3. Stages

A. This document. B. Stubs and tests red (pack rules, combat rest, window). C. Code. D. Game: wait 3 hours, sleep, the bars; three equal runs; save and load. E. Morrowind unchanged. F. Changelog row.

## Shared files touched (small edits)

`game_packs/skyrimse/{CMakeLists.txt,extension.json,src/skyrimse_game_pack.cpp,src/world_view.cpp,include/game_skyrimse/world_view.hpp,src/combat.cpp,include/game_skyrimse/combat.hpp,src/progression_service.cpp,include/game_skyrimse/progression_service.hpp}`, `apps/components_tests/CMakeLists.txt`, `files/data/{builtin.omwscripts,CMakeLists.txt}`, `CLAUDE.md` (changelog row).

## Stage log

2026-11-16; same hand wrote tests and code; no fresh reviewer; Windows only; branch `rest` from `c70547f712`; not pushed.

**Red runs (against stubs).** Pack and combat (`SkyrimRest*`, `SkyrimRested`, `SkyrimRestChunk`, `RestFixture`, `CombatRest`): 31 failed, 18 passed of 49, and 12 of those passes were the window tests, which ran against the real Lua then; so 37 tests, 31 red, 6 test absence (nothing queued, a short sleep, the dead, no hostile, a bad number, full health). Window tests (`SkyrimRestLua`) against a stub module: 12 of 12 red, 12 of 12 green with the real one. After the code: 260 selected Skyrim combat, progression, map and rest tests pass.

**In game** (Bannered Mare, five masters, `tools/skyrest_check/check.sh`, real keys and clicks through `OPENEDEN_RMLUI_INPUT_SCRIPT`, master volume as the base config): wait 3 hours: clock 105,598 to 116,398 game seconds (hour 5.27 to 8.40), the sleepers get up with `bed_leftexit` and walk to new goals, health bar 66 of 220 px to 220; sleep: an owned bed says "This bed is not yours."; with `rest test ignore owner`, "Sleep, 8 hours", clock 117,105 to 145,905, `Rested` with 7.95 hours left and health 40 to full; a Firebolt cast takes magicka to 42 of 220 px and the next wait is refused ("You cannot rest while you are in combat."), a power attack takes stamina to 102 of 220, and after 10 combat seconds a one hour wait gives both back (220). Three runs of the wait scenario give the same 73 log lines (sorted; the order of lines from different threads differs). Save and load: `REST restored: Rested`, the expiry is kept. **The clock after a load is cut to the whole hour** (save at 16.7 loads at 16.00; with no rest at all a save at 0.43 loads at 0.00): existing engine behaviour of a neutral save, not fixed here.

**Vital bars.** Nothing to build: see design 6.

**Decisions for toddhow.** 1. Rested numbers (8 hours, +10 percent, at least 1 hour of sleep) are ours. 2. An owned bed is refused, not a trespass. 3. A wait is refused for 10 combat seconds after a blow or cast and while a hostile is engaged. 4. Sleeping does not confirm a level up (the skills window does). 5. The player is not posed in the bed and there is no fade. 6. The load clock truncation above. 7. `crafting_global.lua` answers the bed event too.
