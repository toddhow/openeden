# Task 0288: Skyrim whole-world coverage sweep (read only)

Status: report only. No source changed. Build used: `I:\Projects\OpenEden-work\n6saves\build\openmw.exe` (main `19d7dd9d38`), five Skyrim masters, a copy of the `skyplay` config (sound 0, rmlui debug panel off, character creation off), `OPENEDEN_QUIET_WINDOW=1`, crash catcher off, god mode on. A Lua global script teleported the player through a plan of places, waited (12 s exterior, 9 s interior), took a screenshot and logged `SWEEP arrive/result` lines. Logs and screenshots were under `I:\Projects\OpenEden-work\sweep\run1..6` (run folders deleted after this report; contact sheets and the old logs are kept in `sweep\`).

## Honest limits of this sweep

- **Partial coverage.** 88 places got a result line (of about 130 planned). Not visited: Highpoint Tower, Nchardak, Temple of Miraak (Solstheim remainder); Fort Dawnguard, Dawnguard HQ, Castle Volkihar, the Volkihar guild hall; the eight wilderness spots (snow, tundra, forest, marsh, volcanic); the second half of run6's dungeons; the Irkngthand pair (retry). Wilderness regions are therefore **not covered at all**.
- **Parallel runs.** Up to 4 game processes ran at once on a 31 GB machine (2 to 5 GB each). Load times below are inflated by memory pressure and are NOT a performance measurement. Two runs hit allocation failures (`BSA Error: Bad compressed BSA entry ... data is cut short` on voice files, `disk I/O error` in `navmesh.db`); these are artefacts of the pressure, not data faults.
- **Interior spawn point** is a heuristic (nearest object to the median x,y plus 100 z). Interior screenshots often show a void or a wall, so no "missing geometry" claim is made for interiors.
- **NPC movement could not be measured.** Neutral actors are not in `world.activeActors`, so `actors=0 moved=0` on every result line is that limit, not "no NPCs". Actor presence comes from the log line `Neutral cell '' inserted into the scene: N objects (... Actor k ...)`.
- Screenshots were viewed as contact sheets (`sweep\s1a-c`, `s3a-b`, `s4a-c`); about 60 frames were looked at, not all 107.

## Ranked breaks and missing systems

### Crashes and freezes

1. **Silent process exit, no dump, exit code 127.** Run1 (after `MarkarthArnleifandSonsTradingCompany`, about 74k log lines) and run4 (after `DragonBridgeFourShieldsTavern`, about 555k log lines) ended with `exit 127`, no crash dump, no error line; a run of the first test set also vanished at `RiftenMistveilKeep`. The 127 may come from the harness shell rather than the game: **unproven**, needs a single-process rerun. Size: unknown.
2. **Freeze at the Irkngthand exterior** (run2): more than 5 minutes, CPU busy, no log output; log saved as `sweep\old\log2_hang.txt`, killed by hand. Not retried alone, so it may be memory pressure. Size: unknown (a terrain or navmesh build loop is a guess).
3. **Main-thread stalls of 45 to 135 s on loads** (`maxgap`, inflated by the parallel runs): MarkarthUnderStoneKeep 135 s, Folgunthur01 133 s, YsgramorsTomb01 129 s, CidhnaMine01 99 s, Mzinchaleft01 99 s, Forelhost 86 s, Fort Greymoor 70 s, Blue Palace 53 s, Solitude 48 s, Hrothmund's Barrow 47 s, Hjerim 45 s. Large interiors (1,500 to 2,600 objects) are consistently worst. With the 5 s freeze monitor on, each would be killed. Size: medium (spread the build over frames as the incremental exterior build does).

### Errors in the logs

4. **`forcegreet_global.lua` line 75: `ticks` is a nil global** (`attempt to perform arithmetic on global 'ticks'`). 17,238 errors in run1 and 136,326 in run4 (each is 4 log lines with the traceback), starting the first frame a forced-greet request is pending (seen from the Markarth and Riften starts) and repeating every frame for the rest of the session: run4's log is 555k lines, 545k of them errors. Source: `files/data/scripts/openeden/skyrimse/forcegreet_global.lua`, `onUpdate`: `ticks = ticks + 1`, never initialised. Evidence line: `[19:30:09.576 E] Global[scripts/openeden/skyrimse/forcegreet_global.lua] onUpdate failed ... :75`. Size: a one line fix, but it probably disables the forced-greet timeout (Companions, Thieves Guild, guard flows) for the whole session. **Best value for effort.**
5. **`menus_menu.lua` fails every frame when the pack is gone:** `core.skyrimse.menus.ready: Refused: the game pack that declared this function is no longer loaded`, about 19,900 times in run3 (at shutdown). The menu script calls the pack function with no guard. Size: small.
6. **Papyrus:** `CWArrowVolleyParentScript.startFiring@21 [budget exceeded]` (Civil War arrow volley; 5 to 11 per run), `CompanionsBladeFragmentTracking.Setup@27 [none receiver]` (2 per run), and natives "declared and not registered", one line each: `weather.GetCurrentWeather`, `quest.UpdateCurrentInstanceGlobal`, `quest.FailAllObjectives`, `objectreference.setDestroyed`, and others. Each run loaded 790 to 880 scripts (1 missing `.pex`), 1,200 to 1,670 instances. Size: small each.
7. **Skeleton and clip read failures (actors keep the rest pose):** `character assets/skeletonbeast...` (3 to 5 per run: Khajiit and Argonian animation), `dwarvensteamcenturion/animations/male/mt...` and `dwarvenspherecenturion/animations/male/mt...` (run5: the centurions do not animate), `dlc2/boarriekling` (Solstheim boars). Size: medium (animation sets for beast races and Dwarven machines).
8. **Missing texture and model fallbacks:** `textures/dlc2/build/pc/data/textures/dlc2/plants/spikygrassash...` (a build-folder path not covered by the engine's path fix), `meshes/plants/mountainflower*purple.nif` read error (run1), a few `furniture`, `dungeons` and `lands` models fell back to the fallback object (run3, run4). Size: small.

### Visible problems in the frames

9. **Solstheim:** the Raven Rock exterior shows magenta (missing texture) surfaces; the Solstheim interiors Morvayn Manor (170 objects), Abandoned Building (63) and the Tel Mithryn steward's house (104) are dark and nearly empty, against 300 to 700 objects for mainland interiors. Size: medium; needs a single-process check.
10. **Odd or blurry exterior views from the spawn point** in several towns (a yellow translucent box at Kynesgrove; flat white haze at Winterhold, Dawnstar, Saarthal). Cause not established.
11. **Factions fight at once on arrival** in Kynesgrove, Helgen, Karthwasten, Darkwater Crossing, Shor's Stone, Ivarstead, Rorikstead: Imperial guards, Stormcloak soldiers and bandits attack each other, some hit the player at the spawn (mainland relations apply, the story state that keeps them apart is missing). Size: a design question (a world-state default).

### Absent game systems (inferred from the places visited)

12. **Traps, levers, pressure plates, puzzle doors and pillars** in the Nordic and Dwarven dungeons (Saarthal01, Ustengrav01, Folgunthur01, Forelhost01, YsgramorsTomb01, Mzinchaleft01): objects load as statics, no trap or activator events appear in the logs. Size: large.
13. **Swimming** at the harbours and canals (Windhelm, Solitude, Riften, Raven Rock docks, Skaal): water is drawn, no swim or drowning logic (task 0129 said so). Size: large.
14. **Not exercised:** mounted NPCs, carriages in motion, dragons, bosses and set pieces (no NPC motion was measurable here); see tasks 0255, 0274, 0280.
15. **Dawnguard and Dragonborn** (Castle Volkihar, Fort Dawnguard, Skaal, Temple of Miraak, Tel Mithryn): cells load for the places visited; quest scripts and set pieces were not tested.

## Per-place results

`Load` is seconds from the teleport call until the cell was usable, measured with 2 to 4 processes sharing the machine (see limits). `Objects` is the count the neutral cell inserted (empty for exteriors: the exterior line has none). No place had a teleport failure (0 `SWEEP FAIL` lines in all runs).

| Place | Kind | Load s | Objects | Crash/freeze | Notes |
|---|---|---:|---:|---|---|
| Whiterun | capital ext | 5.7 | | no | start city |
| WhiterunDragonsreach | palace | 35.8 | 1441 | no | |
| WhiterunBanneredMare | inn | 20.9 | 697 | no | |
| WhiterunBelethorsGeneralGoods | shop | 3.6 | 322 | no | |
| Solitude | capital ext | 48.3 | | no | |
| SolitudeBluePalace | palace | 52.9 | 2120 | no | |
| SolitudeWinkingSkeever | inn | 12.3 | 982 | no | |
| SolitudeRadiantRaiment | shop | 13.7 | 613 | no | |
| Windhelm | capital ext | 12.8 | | no | |
| WindhelmPalaceoftheKings | palace | 34.8 | 592 | no | |
| WindhelmNewGnisisCornerclub | inn | 8.7 | 579 | no | |
| WindhelmBlacksmith | shop | 5.1 | 393 | no | |
| WindhelmHjerim | house | 44.8 | 1599 | no | |
| Markarth | capital ext | 11.9 | | no | |
| MarkarthUnderStoneKeep | palace | 135.5 | 2614 | no | slowest load |
| MarkarthSilverBloodInn | inn | 12.9 | 802 | no | |
| MarkarthArnleifandSonsTradingCompany | shop | 4.4 | 585 | exit 127 after | run1 ended here |
| Riften | capital ext | 10.8 | | no | |
| RiftenMistveilKeep | palace | 6.5 | 733 | no | |
| RiftenBeeandBarb | inn | 6.8 | 625 | no | |
| RiftenElgrimsElixirs | shop | 7.6 | 270 | no | |
| RiftenRatway01 | dungeon (city) | 37.1 | 791 | no | |
| Morthal | capital ext | 36.4 | | no | |
| MorthalHighmoonHall | palace | 28.3 | 393 | no | |
| MorthalMoorsideInn | inn | 8.0 | 350 | no | |
| MorthalThaumaturgistsHut | house | 1.0 | 229 | no | |
| Dawnstar | capital ext | 27.7 | | no | |
| DawnstarTheWhiteHall | palace | 12.8 | 443 | no | |
| DawnstarWindpeakInn | inn | 5.1 | 363 | no | |
| DawnstarRustleifsHouse | house | 0.8 | 135 | no | player z -45925 (spawned far below) |
| Winterhold | capital ext | 13.3 | | no | hazy frame |
| WinterholdJarlsLonghouse | palace | 17.8 | 335 | no | |
| WinterholdTheFrozenHearth | inn | 8.3 | 331 | no | |
| WinterholdCollegeArcanaeum | college | 6.7 | 330 | no | |
| Falkreath | capital ext | 19.7 | | no | |
| FalkreathJarlsLonghouse | palace | 7.8 | 342 | no | |
| FalkreathDeadMansDrink | inn | 9.4 | 352 | no | |
| FalkreathGrayPineGoods | shop | 5.1 | 208 | no | |
| Riverwood | town | 22.8 | | no | |
| RiverwoodSleepingGiantInn | inn | 9.0 | 484 | no | |
| Ivarstead | town | 19.7 | | no | fighting at arrival |
| IvarsteadVilemyrInn | inn | 3.0 | 322 | no | |
| Rorikstead | town | 8.6 | | no | fighting at arrival |
| RoriksteadFrostfruitInn | inn | 2.6 | 347 | no | |
| Dragon Bridge | town | 22.2 | | no | |
| DragonBridgeFourShieldsTavern | inn | 11.1 | 341 | exit 127 after | run4 ended here |
| Kynesgrove | town | 11.5 | | no | fighting; yellow box |
| Helgen | town | 25.9 | | no | fighting |
| Karthwasten | town | 15.5 | | no | fighting |
| Darkwater Crossing | town | 15.6 | | no | fighting |
| Shor's Stone | town | 17.1 | | no | fighting |
| Old Hroldan | town | 9.8 | | no | |
| Hag Rock Redoubt / HagRockRedoubt01 | dungeon (fort) | 18.3 / 14.2 | 508 | no | |
| Saarthal / Saarthal01 | dungeon (nordic ruin) | 8.7 / 39.9 | 1276 | no | puzzle systems absent |
| Ysgramor's Tomb / YsgramorsTomb01 | dungeon (tomb) | 14.2 / 129.2 | 2158 | no | |
| Fort Greymoor | fort | 70.1 | | no | |
| Fort Sungard | fort | 12.1 | | no | |
| Mzinchaleft / Mzinchaleft01 | dungeon (dwarven) | 11.6 / 98.6 | 1496 | no | centurion animations fail |
| BlackreachZcell01 | cave | 23.7 | 148 | no | |
| CidhnaMine01 | mine/prison | 98.5 | 839 | no | |
| WhiterunHalloftheDead | crypt | 11.4 | 313 | no | |
| SolitudeHalloftheDeadCatacombs | crypt | 27.2 | 559 | no | |
| Ustengrav / Ustengrav01 | dungeon | 10.0 / 23.8 | 1042 | no | |
| Folgunthur / Folgunthur01 | dungeon | 6.4 / 133.0 | 1839 | no | |
| Forelhost / Forelhost01 | dungeon | 86.1 / 76.1 | 1578 | no | |
| Shroud Hearth Barrow | barrow | 37.4 | | no | |
| Raven Rock | Solstheim town | 0.0 | | no | magenta surfaces |
| DLC2RRMorvaynManor | house | 31.7 | 170 | no | dark, sparse |
| DLC2RRTemple | temple | 10.9 | 364 | no | |
| DLC2RRAbandonedBuilding | house | 4.8 | 63 | no | dark, sparse |
| Fort Frostmoth | fort | 30.7 | | no | |
| Skaal Village | town | 43.0 | | no | |
| Tel Mithryn / DLC2TelMithrynStewardsHouse | tower | 33.7 / 12.0 | 104 | no | |
| Hrothmund's Barrow / DLC2HrothmundsBarrow01 | barrow | 13.2 / 47.1 | 529 | no | |
| Bloodskal Barrow | barrow | 42.2 | | no | last place of run3 |

Per-run log error counts: run1 68,973 (17,238 forcegreet errors, 4 lines each), run3 139,523 (menu script at shutdown), run4 545,379 (136,326 forcegreet errors), run5 8 and run6 32 (the Papyrus lines and a disk I/O error on `navmesh.db`).

## Suggested next steps

1. Fix the `ticks` global in `forcegreet_global.lua` (item 4) and guard `menus_menu.lua` against a missing pack (item 5): both tiny.
2. Re-run the 11 slowest places and the Irkngthand pair alone, one process, freeze monitor on, to settle items 1 to 3.
3. Cover the unvisited places (Solstheim remainder, Dawnguard, wilderness regions) one process at a time, with an interior spawn next to a real door so the screenshots are usable.
