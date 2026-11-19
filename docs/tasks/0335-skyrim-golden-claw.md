# Task 0335: Skyrim side quest MS13 "The Golden Claw"

Goal: run the quest through the game's own data, dialogue, scenes and Papyrus. Pack setting `[extension.skyrimse] golden claw` (off, declared in `extension.json`, README line in `tools/testprofile`).
Same hand wrote tests and code; the tests were written first against stubs (compile-red / stub-red), no fresh reviewer; Windows only.

## What runs (game's own)
Lucan's scene in the Riverwood Trader (`MS13` stage 5), the real conversation (stage 10), Arvel's body looted by real clicks (the claw: stage 40), the real keyhole at Bleak Falls Barrow (stage 50), the word wall (stage 60), and back to Lucan: "I have the golden claw." sets stage 100 (`QF_MS13` Fragment_17: the claw leaves the inventory, the reward arrives).

## Faults found and fixed (all behind `golden claw`)
1. `ConditionWorld::detects` (GetDetected, fn_45) was never answered, so the scene stalled at phase 3: with the setting an actor in the player's cell detects him (stand-in, named: no sight rule).
2. An ALCA bit 31 "In" alias only recorded the fill: the item is now also put in the inventory of the alias it names (`PapyrusHost::setCreatedInInventory`).
3. Quests that start with the game never created their alias objects: done at `startGame` for "In" aliases, deferred until the item table is ready (`PapyrusWorldHooks::itemsReady`).
4. The alias script never heard `OnContainerChanged` for a created item (it has no placed reference): `held == item` is accepted with the setting.
5. `GetItemCount` of the player was wired only under `marriage`; now also with `golden claw`.

## Stand-ins (named)
Teleports between the cells and the harness's `abs` steps (no walking through doors), the kill aid `summons test kill = MS13Arvel`, the story aid for stages 10/15/35 in leg 2 (the talk, the web and the cut), the harness `act` activations of the rings and the keyhole by `activateBy`, the detects rule above.

## Not run
Camilla's lines, the Linux gate, clang-format 14, clang-tidy, Lua tests, `openmw-tests`, a fresh review.

Check: `tools/skygoldenclaw_check` (`leg1.sh`, `leg2.sh` full Barrow to Trader, `leg3.sh` Trader return).
