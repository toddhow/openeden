# Task 0218: Skyrim fast travel and the carriage

Milestone M-S7. Branch `fasttravel` (from `c0f6514c15`). Same hand wrote tests and code; no fresh reviewer. Windows only.
Behind the new pack setting `[extension.skyrimse] fast travel` (default false; needs `map`).

## 1. Goal

The map (task 0201) shows discovered markers but the player could not travel. Now: click a discovered marker on the world
map, answer "Travel to X?", and the player arrives at the marker's landing place, the game clock moves by the distance, and
followers come along. Refused in combat, with a hostile engaged and from an interior. The carriage drivers are surveyed.

## 2. Survey (Skyrim.esm, aggregates; scripts in `I:\Projects\OpenEden-work\ragdoll5\ft`)

Map marker REFRs (397 in `Skyrim.esm`):

- 353 carry an `XLKR` of exactly 8 bytes: keyword (null in all 353) then a reference. The reference is a static landing
  place next to the marker (editor ids such as `MerryfairFarmMapLandingMarker`); distance from the marker: minimum 25,
  median 1,873, maximum 8,770 units; every landing is a placed REFR with a position. 44 markers have no `XLKR`.
- `FNAM` flags in the file: 0 (377), 1 (9), 3 (11). Bit 1 ("can travel to") is set in 11 only, so the rule used is: a
  marker may be travelled to when it is discovered or carries that bit. RECALLED-VERIFY (xEdit, UESP): Skyrim sets "visible"
  and "can travel to" when the player discovers a marker.
- RECALLED-VERIFY: that Skyrim's own arrival is the marker's linked reference. The data supports it (the names), the
  engine's behaviour was not read. Where there is no usable link the arrival is the marker's own position.
- Time (ours): one game hour per 9,000 units of straight line distance, at least 1 and at most 48 hours. Whiterun to
  Windhelm (about 120 km) takes 14 hours.

Carriage (Skyrim.esm, `Skyrim - Misc.bsa` scripts, string tables of the `.pex` only):

- Quest `DialogueCarriageSystem` (0x17F01) with the script `CarriageSystemScript` (14 KB) and per driver
  `CarriageDriverScript`; 42 `DialogueCarriage*` topics (one per destination plus system lines); destination markers
  `<Town>CarriageDestinationMarker` (Whiterun, Windhelm, Riften, Solitude, Markarth, Dawnstar, Winterhold, Falkreath,
  Morthal, plus the small-town stops); fees are the globals `CarriageCost` = 20 and `CarriageCostSmall` = 50 (FLTV).
- The script's `Travel` uses `Game.FastTravel`, `RemoveItem(Gold, cost)` (`payForRide`), fade image space modifiers
  (`FadeToBlackImod`...), `DisablePlayerControls`, the player sits in the cart (`PlayerRider`, `SetVehicle`,
  `IsFurnitureInUse`, `OnSit` of the driver, `WaitForPlayerToSit`), passengers (`CartRider1..3`), follower handling, wilderness
  encounters off while riding.

Verdict: **not built.** The travel itself is the same `FastTravel`, but the driver's lines run through quest fragments that
need `Game.FastTravel`, `DisablePlayerControls`, `SetVehicle`, sitting on furniture (the driver waits for the player to sit)
and the image space fades; none of those natives exists in the Papyrus host and sitting needs furniture use for the player.
What it would take: (1) a native `Game.FastTravel(ObjectReference)` in the host over this task's `plan`/`commit` (a
`PapyrusWorldHooks` entry plus a global-script handoff, since only a global script can teleport); (2) `RemoveItem(Gold001,
cost)` on the player's inventory (the host's `RemoveItem` already works, task 0198); (3) the cart sit: either skip it (answer
`OnSit` directly) or build player furniture use; (4) natives `DisablePlayerControls`, `EnablePlayerControls`, `SetVehicle`,
`SetRestrained` as no-ops. Estimated 2 to 3 days with the sit skipped. Left for toddhow.

## 3. Design

- `travel.*` (pack, pure plus a service): `decideTravel` (NotReady, Interior, UnknownMarker, NotDiscovered, InCombat,
  EnemiesNear, NoDestination, in that order), `travelHours`, `yawToward` (the engine's heading: 0 faces +Y, clockwise),
  `chooseDestination` (the link when it stands in the marker's world outdoors, else the marker; the arrival faces the
  marker), `TravelService` over the map service (markers, discovered state, placed refs, cell worlds) and the combat
  (`playerInCombat`, `enemiesNear`, the same as `rest`). Namespace `skyrimse.travel` 1.0: Read `ready`, `marker_ref`
  (hex ids in the map's order), `refusal`, `plan` (x, y, z, yaw, hours, distance), `follower_count`; Mutate `commit` (counts,
  logs `TRAVEL`, discovers around the arrival).
- `map_markers.*`: `Marker::mLinked` from the null-keyword `XLKR` (a keyword link is ignored, another size is odd).
- Lua: `worldmap.lua` makes marker frames clickable when given `env.travel`; a click on a found marker opens the document
  `skyrim-travel-prompt` ("Travel to X?", Yes, No); Yes calls `env.requestTravel`; `notice` shows a refusal in the status
  line. `map_player.lua` passes the travel package and the event handlers; `travel_global.lua` (global) checks the pack's
  `refusal`, plans, commits, advances the clock and teleports the player (`onGround`).
- Followers: the engine already carries followers through a teleport DOOR (`mwworld/neutralfollowers.*`). A Lua teleport of
  the player did not; `objectbindings.cpp` now calls `carryNeutralFollowersWithPlayer` before the player moves (no effect
  unless the player stands in a neutral cell and the game's `FollowerRules` names followers).

## 4. Edits outside new files

- `game_packs/skyrimse/include/game_skyrimse/map_markers.hpp` (a trailing field `mLinked`), `src/map_markers.cpp` (XLKR),
  `src/skyrimse_game_pack.cpp` (`startTravel`, member), `extension.json` (the setting), `CMakeLists.txt` (travel.cpp,
  travel.hpp).
- `apps/components_tests/CMakeLists.txt` (three lines: two test files, travel.cpp in the pack sources).
- `files/data/openeden/skyrimse/worldmap.lua` (travel; the existing window is unchanged without `env.travel`),
  `files/data/scripts/openeden/skyrimse/map_player.lua`, `files/data/builtin.omwscripts` (one line), `files/data/CMakeLists.txt`
  (one line), `tools/testprofile/profile.cfg` (a comment: a pack setting, like `rest`).
- `apps/openmw/mwlua/objectbindings.cpp` (the player teleport lambda), `apps/openmw/mwworld/neutralfollowers.{hpp,cpp}`
  (`carryNeutralFollowersWithPlayer`).
- No existing test edited.

## 5. Stage log

Tests first (same hand), run red against stubs (`travel.cpp` answering "nothing", no worldmap changes, `Marker::mLinked`
declared but not parsed): **30 of 32 new tests failed** (the 2 that passed test refusal and absence: an unknown marker has
no plan, and a keyword link is ignored). After the code: all 32 pass, and the existing `SkyrimMap*`,
`MapServiceFixture*` and `SkyrimMapLua*` tests still pass unedited (88 in the filter). Files: `skyrimse/testskyrimsetravel.cpp`
(24 tests: rules, destination, service, bindings, XLKR), `ui/testskyrimsetravelwindows.cpp` (8 tests: the prompt, Yes, No,
undiscovered marker, replacing the prompt, notice, no travel package, refusal texts).

In game (`tools/skytravel_check/check.sh`, Whiterun's street `@WhiterunWorld,25670,-7633`, `[GUI] rmlui`, `map` and
`fast travel` on, real clicks through `OPENEDEN_RMLUI_INPUT_SCRIPT`, fixed frame time, master and music volume 0, frames viewed;
run folders under `I:\Projects\OpenEden-work\ragdoll5` were deleted afterwards):

- The map opens with M: a click on the discovered marker at (357, 332) shows "Travel to Honningbrew Meadery?" with Yes and No.
- Yes: `travel requested`, `TRAVEL to Honningbrew Meadery at (30960, -17767, -4336) in 2 hours, 0 followers, from the link`
  (the landing is 67 units from the marker's (30930, -17711)), the player stands at (30960, -17767, -4356) and the game clock
  goes from hour 0.50 to 2.51; the compass shows the name.
- Refusal (the real attack key casting Firebolt with `combat test player spell`): `refused (combat)`, the window stays open
  with "You cannot fast travel while in combat." in its status line, the player does not move (the status line pushes the
  map down by one text line).
- Followers (the cow `EncCow` recruited by `follower test recruit` and put beside the player by the check script):
  `Neutral player teleport: 1 followers go with the player`, `Neutral follower 1108911 carried`, `TRAVEL ... 1 followers`.
  The follower's arrival position was NOT looked at in a frame. Guards recruited by editor id were out of range (more than
  1,500 units) and were not carried, as the rule says.
- Determinism: three runs give identical lines for the scripted steps, the discovery, the request, the pack's `plan` and
  `commit` (md5 `0420588d2722` of the 7 lines) and a clock jump of 2.0 hours (2.01, 2.01, 2.02). The hour of day at a given beat
  is NOT the same run to run, because the pack needs real time to read its data and the game clock runs meanwhile (the
  existing map scenario has the same property). Screenshots are not byte-identical.
- Morrowind unchanged: `tools/worldobjects_check` 110 lines OK and the two Balmora frames of
  `tools/skymap_check/morrowind_check.sh` (plain, and with the map window open) have the same SHA-256 prefixes
  (`1eb68236352dc472`, `c819db116f4af8a6`) as the reference. The reference is `integ2`'s `main` build of 14 Nov (another
  agent's build, NOT a build of this commit's parent). The `1c5d47ad0b5dc32d` frame of other rows came from a different base
  config and was not reproduced here.

Not run: the Linux gate and GCC, clang-format 14 (formatted by hand), clang-tidy, the Lua integration tests, a fresh review,
a refusal from an interior or with `enemies` in game (unit tested only; the world map does not open in an interior, so that
refusal cannot be reached from the window), travel across worldspaces (the markers of other worlds are not on the map), a
carriage ride, saving after a travel.

## 6. Decisions for toddhow

1. Travel time is 1 game hour per 9,000 units, capped at 48 hours (ours, `travel.hpp`). Skyrim's own rate is not read.
2. A marker may be travelled to when discovered or flagged "can travel to"; undiscovered markers are not drawn on the map at
   all, so the window never offers them.
3. Travel from an interior is refused by the pack, but the map window does not open in an interior at all (it needs the
   player's worldspace). Skyrim lets you open the map indoors; showing it there needs the world map to take the last
   exterior world.
4. A Lua `teleport` of the player now takes the followers along (engine, `objectbindings.cpp`). It affects any script in a
   neutral session that has followers, not only fast travel.
5. The carriage is not built (section 2).
6. The pack setting needs `map`; the player script needs `[GUI] rmlui` (the world map is a RmlUi window).
