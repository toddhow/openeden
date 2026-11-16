# Task 0223: Skyrim carriages and the world map indoors

Milestone M-S7. Branch `carriage` (from `78f77f1c70`, task 0218's fast travel on `main`). Same hand wrote tests and code; no
fresh reviewer. Windows only. Behind the existing pack settings `[extension.skyrimse] fast travel` (needs `map`) and
`papyrus`; no new setting.

## 1. Goal

1. The carriage drivers work: talking to Whiterun's driver and choosing a destination charges the fee, moves the player to the
   destination (followers included), and advances the clock, through the vanilla `CarriageSystemScript` running on the Papyrus
   host, not a reimplementation.
2. The world map opens indoors. It shows the last exterior world the player stood in, else the world of the interior's
   location. Travel from an interior is still refused with the interior reason.

## 2. Survey (Skyrim.esm, aggregates; scripts decompiled from `Skyrim - Misc.bsa`, files in `I:\Projects\OpenEden-work\ragdoll5\carriage\psc`)

- `CarriageSystemScript` (quest `DialogueCarriageSystem`, 0x17F01). A driver's topic fragment (`TIF__00017EFD` and about 40
  others) calls `(GetOwningQuest() as CarriageSystemScript).Travel(n, akSpeaker)` with n = 1 Whiterun, 2 Solitude, 3 Markarth,
  4 Riften, 5 Windhelm, 6 Morthal, 7 Dawnstar, 8 Falkreath, 9 Winterhold, 10 to 19 the small-town stops.
- `Travel`: with `bWaitForPlayerToSit` (default true) it forces the player's alias to the driver's linked seat
  (`LinkCarriageSeat`), sets the driver's `variable01`, and, if `IsFurnitureInUse` says the seat is taken, calls
  `PlayerIsSitting`; with it false it pays and activates the seat and waits one second (`OnUpdate`).
- `PlayerIsSitting`: checks `IsDead`, `IsInCombat`, `InventoryWeight > CarryWeight`, `DisablePlayerControls`, then (unless the
  driver is in `BYOHCarriageDriverFaction`) `player.RemoveItem(Gold, CarriageCost.value as int)` (`CarriageCost` for
  destinations 1 to 5, `CarriageCostSmall` for the rest), `Say`, three `utility.wait(2)` around the black fade
  (`FadeToBlackImod.Apply`, `PopTo`), then `SkipToDestinationSimple`: `game.FastTravel(<destination marker>)` for the number,
  the fade back, `ClearWaitingState`, `EnablePlayerControls`.
- The destination is an ObjectReference property of the quest script (`Whiterun`, `Windhelm`, ...: the
  `<Town>CarriageDestinationMarker` placements), not a map marker.
- The data: the Whiterun driver is `CarriageDriverWhiterun` (placement `WhiterunCarriageDriver`, 0x13698) at (17590, -12412,
  -4745).

## 3. Design

- `travel.*`: `planToRef`, `refusalToRef`, `commitToRef` (the same rules as a marker's: not ready, interior, unknown, combat,
  enemies, no destination; a carriage goes to a place whether or not the map knows it, so "undiscovered" never applies; the
  reference must stand in the player's world, its parent chain or a world that borrows from it), `queueRequest`,
  `requestSequence`, `requestedRef`. Namespace `skyrimse.travel` 1.1: Read `request_seq`, `request_ref`, `refusal_ref`,
  `plan_ref`; Mutate `commit_ref`. The arrival faces the way the player travelled. Time and discovery are as for a marker.
- Papyrus host (new natives only): `Game.FastTravel(akDestination)` calls the host's `FastTravelSink` (the pack's
  `queueRequest`); `GlobalVariable.GetValue/SetValue` (the GLOB's value, a map of what scripts set; `PapyrusGameData::
  globalValue`, `SkyrimWorldView::globalValue`); counted no-ops: `Game.DisablePlayerControls`, `EnablePlayerControls`,
  `FadeOutGame`, `ForceFirstPerson`, `ShowFirstPersonGeometry`, `SetInChargen`, `ImageSpaceModifier.Apply`, `PopTo`,
  `Remove`, `Actor.SetVehicle`, `SetRestrained`, `Say`, `EvaluatePackage`; answers: `Actor.PlayIdle` true,
  `IsInFaction` false, `IsInCombat` false, `ObjectReference.IsFurnitureInUse` true (the player sits at once, so the script
  takes its sit path). The fee is paid by the script itself through the existing inventory natives.
- `travel_global.lua`: polls `request_seq` each update; a new request runs the refusal, plan, `commit_ref`, clock and
  teleport (`arrive`, shared with the map's travel) like a marker.
- The map indoors: `map.interior_world(editorId)` (new Read function, `skyrimse.map` 1.1) answers the world of an interior by
  its location (`SkyrimWorldView::worldOfInterior`: the cell's location, then its parents until one lists a grid square);
  `openeden.skyrimse.mapplace` gives the window the player's place: exterior as is; indoors the last exterior world and
  position, else the location's world with no position (no player marker). The compass keeps the player's own place.
  `worldOfInterior` also falls back to a location inside the chain's ancestors when none of the chain lists a grid (the
  Mare's WhiterunLocation record lists none in the final load order); the Lua answer is cached only when non-zero.
- Found while running it (each needed a fix beyond the first design): the dialogue box PAUSES the world, so the fragment runs
  only after Goodbye; `(GetOwningQuest() as CarriageSystemScript)` was None because a quest form value is not its script, so
  `TopicInfo.GetOwningQuest` and `Alias.GetOwningQuest` now return the attached script instance (`questValue`); a GLOB had
  class `Form`, so `CarriageCost.value` never reached `GetValue` (the fee was 0): `formClass` now says `GlobalVariable`;
  `Say` is declared on ObjectReference in the game's scripts; the player's driver stands in Tamriel, not WhiterunWorld.

## 4. Edits outside new files

`game_packs/skyrimse`: `include/.../{map,papyrus_host,travel,world_view}.hpp`, `src/{map,papyrus_host,skyrimse_game_pack,
travel,world_view}.cpp`, `CLAUDE.md`; `files/data/openeden/skyrimse/worldmap.lua` (no player marker without a position, range
point 0,0 indoors), `files/data/scripts/openeden/skyrimse/{map_player,travel_global}.lua`, `files/data/CMakeLists.txt` (one
line), `apps/components_tests/CMakeLists.txt` (two lines). New: `files/data/openeden/skyrimse/mapplace.lua`,
`apps/components_tests/skyrimse/testskyrimsecarriage.cpp`, `ui/testskyrimsecarriagewindows.cpp`, `tools/skycarriage_check/`.
Not touched: the combat contract, `dialogue_runtime.*`, any existing test. `papyrus_host.cpp`: new natives, `questValue`, the
`GlobalVariable` class rule and the native trace only.

## 5. Stage log

Tests first against stubs (travel stubs returning nothing, no natives, `mapplace` passing the place through): **24 of 31 failed**
(the 7 that passed test absence or refusal). Later additions: `CarriageOwnerTest` (GetOwningQuest): 1 of 2 red; the GLOB class
test was written after the in-game finding (the fee of 0), not run red as a test. Final: 36 `Carriage*`, `SkyrimMapPlaceLua*` and
`SkyrimMapIndoorsLua*` tests pass; the wider `*Skyrim*:*Papyrus*:*Travel*:*Map*:*Rest*:Ui*:*Lua*:*Extension*:*Neutral*:*Frozen*:
*Dialog*` filter 3,3xx pass, no existing test edited (one existing test, `a_native_nobody_answers_is_tolerant_...`, named
`Actor.IsInCombat`, so that native was NOT registered).

In game (`tools/skycarriage_check/check.sh`, Tamriel start next to the Whiterun driver Bjorlam (CarriageDriverWhiterun, base
79465) after the clock moves to morning, real key and clicks through `OPENEDEN_RMLUI_INPUT_SCRIPT`, volumes 0, frames viewed):
Space opens his dialogue (10 topics), a click on "Solitude. (<Global=CarriageCost> gold)" shows Goodbye only (the line ends the
conversation), Goodbye runs `TIF__001094CF.Fragment_0`: `Travel(2)` -> `PAPYRUS item 0000000F x20 from 00000014` (gold 100 to 80,
inventory frame) -> `Game.FastTravel` -> `carriage to 000b09ff at (-73700, 99758, -10873) in 17 hours, 0 followers` and the
`TRAVEL` line; the player stands outside Solitude. Three runs: the PAPYRUS/TRAVEL/MAP lines are byte-identical (md5 b05915034dd0).
Map indoors (`NOPLAN=1`, `DISCOVER=107119,25670,-7633`, start in the Bannered Mare): M opens the map of Tamriel (12 found
markers, no player marker), a click on Dawnstar and Yes gives "You cannot fast travel from an interior." (`refused (interior)`;
the refusal IS reachable now). Morrowind unchanged: `tools/skymap_check/morrowind_check.sh` frames `1eb68236352dc472` and
`c819db116f4af8a6` equal for this build and `integ2`'s build (NOT the exact parent), `tools/worldobjects_check` 110 lines OK.
Tolerant natives: the carriage path made 11 distinct calls that were tolerant before (`Game.FastTravel`, `GlobalVariable.GetValue`,
the controls, `Actor.SetActorValue` stayed answered, `IsFurnitureInUse`, `IsInFaction`, `Say`, ...): answered now except
`Actor.IsInCombat` and the three `ImageSpaceModifier` calls (class `Form`); a run's total ("tolerant natives 13" at 2 s in a plain
session, 20 after the carriage and a 9 hour clock jump) is dominated by other scripts.

Not run: the Linux gate, GCC, clang-format 14 (by hand), clang-tidy, the Lua tests, a fresh review, a followers-with-the-cart run,
the small-town stops (fee `CarriageCostSmall`), other drivers, saving after a ride. The 9 hour clock jump of the scenario made
Civil War scripts hit the instruction budget (`CWArrowVolleyParentScript`, 4 errors): not related, not investigated.

## 6. Decisions for toddhow

1. The ride has no animation, fade or sit: the player appears at the destination marker after the script's six seconds of waits;
   the arrival faces the way the player travelled (placements carry no rotation in `PlacedEntry`), so it may face a wall.
2. `IsFurnitureInUse` answers true for every furniture (the carriage's seat check); `IsInCombat` stays tolerant false, so a
   refusal in combat after the fee keeps the fee.
3. Topic prompts with `<Global=CarriageCost>` show the raw tag (text tags are not expanded: the dialogue agent's area).
4. `GetOwningQuest` of a scripted quest is its script instance (first non-QF script), for any caller.
5. The map indoors uses the interior's location chain (`worldOfInterior`) and a fallback; with no exterior history it shows no
   player marker.
6. `OPENEDEN_PAPYRUS_NATIVE_TRACE` is a new developer aid (environment variable, not a setting).
