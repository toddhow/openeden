# Task 0297: Skyrim whole-world coverage sweep, part 2 (read only)

Status: report only, no source changed. Follow-up of `0288-skyrim-world-sweep.md`.

Build used: `I:\Projects\OpenEden-work\combatai\build\openmw.exe` (read only, not rebuilt). Five Skyrim masters. Config copied from part 1's `cfgbase` with `[Sound] master volume = 0.0`, `OPENEDEN_QUIET_WINDOW=1`, god mode on. A Lua global script teleports through a plan (12 s exterior, 9 s interior settle) and takes a screenshot. Scripts and plans: `I:\Projects\OpenEden-work\sweep2\` (`launch2.sh`, `plan*.lua`). Contact sheets and per-run summaries: `sweep2\keep\`. Run folders were deleted.

Difference from part 1: at most 2 game processes at a time (one slip: a third finished run was still alive for a few minutes and was killed as soon as the coordinator said so), and `character creation` was set to false after the first Solstheim run.

## Honest limits

- **Part 1's frames were probably spoiled by the character creation window.** Its `cfgbase/settings.cfg` has `character creation = true`, and the sol run here showed the "Create your character" window (plus stat windows) covering the whole left half of every interior frame. Part 1's "dark, sparse interiors" and "odd views" notes were judged from such frames. The 6 Solstheim places in `sol_*` were therefore re-run with it off (`sol2`, `sol3`).
- Interior spawn point is part 1's heuristic (nearest door plus 130 units into the room), so frames often show a wall or void. No missing-geometry claim is made from them.
- NPC motion is not measurable (neutral actors are not in `world.activeActors`).
- Load times are with 1 to 2 game processes plus other agents' builds on the machine: not a performance measurement.
- Not run: Dawnguard Fort interior beyond HQ, DLC1 quests, Volkihar exterior walk, any dungeon beyond its first room.
- The exit-127 cases were run as short sequences. Part 1's runs died at about 74k and 555k log lines. The sweeps here are 3 to 25 places long, not the same length.

## Findings, ranked

### Reproduction questions from part 1

1. **Irkngthand exterior freeze: does NOT repeat.** Run alone starting inside the world (`irk`): exterior arrive, interior `Irkngthand01` (970 objects) arrived after 32 s. Run again as a teleport from Whiterun (`irk2`): exterior 12.7 s, interior 34.1 s, exterior again 4.7 s; 0 errors, no freeze, no dump. Part 1's freeze was most likely the 4-process memory pressure.
2. **Silent exit 127 after `MarkarthArnleifandSonsTradingCompany`: does NOT repeat.** Short run (`mark`: Markarth, Arnleif, Markarth, Silver-Blood Inn) and the full part-1 capital sequence in one process (`caps`: Whiterun, Solitude, Windhelm, Markarth, 16 places, 4,761 log lines), Arnleif last: no exit, no dump, process still alive after the final place.
3. **Silent exit 127 after `DragonBridgeFourShieldsTavern`: does NOT repeat** (`drag`: exterior, tavern, exterior again, process alive after).
4. **`forcegreet_global.lua` nil `ticks` (part 1 item 4): gone in this build.** The resource copy in `combatai\build\resources\vfs\scripts\openeden\skyrimse\forcegreet_global.lua` has `local ticks = 0` (line 15). No `attempt to perform arithmetic` line in any of the 10 runs. The 127s with 545k error lines in part 1 are explained, not by memory, but by a build with the bug; this was not tested against that old build.
5. Overall in this sweep: **0 `E` log lines in all 10 runs**, 0 crashes, 0 freezes, 0 `SWEEP FAIL`.

### Visible breaks

6. **Raven Rock magenta surfaces: cause found.** Meshes of DLC2 and DLC1 name textures under a build folder that exists in no archive: `textures/dlc02/build/pc/data/textures/dlc02/plants/spikygrassash01.dds`, `.../plants/dlc2scathecraw01.dds`, `.../landscape/grass/volcanicashgrass02.dds` and `03`, and `textures/dlc01/build/pc/data/textures/dlc01/armor/dawnguard/dawnguardboots1_d.dds`. The real files exist at `textures/dlc02/plants/...` etc. (grep of `Skyrim - Textures4.bsa` finds the four plain names; the DLC content is merged into the `Skyrim - *.bsa` files in this install). The engine's path fix handles only the `skyrimhd\build\pc\data\textures` form, so these draw the magenta fallback: seen in the Raven Rock frame, Highpoint Tower frame (`sol2_sheet.png`, `sol3_sheet.png`). Counts: `textures N loaded / 4 missing` at Raven Rock, 8 to 12 elsewhere. Fix idea: also cut everything up to `\data\textures\` in `FaceTint::normaliseTexturePath`'s family (small).
7. **Interior water plane at height 0 floods interiors whose floor is below 0.** Log lines `NWATER physics plane at 0 for <cell>`: `EmbershardMine01`, `Labyrinthian01`, `Mzulft01`, `LostProspectMine01`; the plane is off for `BleakFallsBarrow01`, `DustmansCairn01`, `Alftand01`, `FellglowKeep01`, `IlinaltasDeep01`, `FrostmereCrypt01`, `Raldbthar01`, `SkyHavenTemple`. The player stands at z -1073 (`Labyrinthian01`) and -134 (`Mzulft01`), under the plane: the breath bar shows and the frame is the underwater tint (`dung1_a.png` frames 8 and 12; `Mzulft01` also shows water-surface strips). Looks like a default of 0 where the cell has no XCLW water height. `Alftand01` (plane off) also draws a full teal wash at z 567 (frame 10): a drawn water surface or fog with no physics plane. Not traced further.
8. **Dawnguard HQ interior is tinted solid green** (`wild_sheet.png` frame 10) and the Temple of Miraak interior is a dark green void (`sol3_sheet.png` frame 6). Likely the cell lighting colours or a fog colour; not established.
9. **Hostile actors act at arrival and the player takes damage** (Throat of the World: `EncDragonSnowNoScript` Lv 10 attacks; fights at Frostmere Crypt, Fellglow Keep (flame atronach sentry), Embershard, Nchardak bandit (Lv 25), Castle Volkihar watchman, a "Watchman / Goodbye" speech box is open at Castle Volkihar and the Guildhall). Mainland faction relations apply with no story state. Design question, as part 1 item 11.
10. **Interior frames are mostly black or near black** in the dungeons (Dustman's Cairn01 is pure black, Highpoint Tower 01, Nchardak 01). Either the spawn heuristic puts the camera in an unlit corner or interior lights are too weak (Morvayn Manor has 4 lights, Abandoned Building 2, Tel Mithryn house 8). Part 1's "sparse" interiors are small cells, not missing content: Morvayn 170 objects, Abandoned Building 63, Tel Mithryn house 104 (counts equal the cell contents the pack inserted, none left out). Tel Mithryn house looks right (`sol2_sheet.png` frame 3), Morvayn Manor shows a guard and a name label.

### Missing systems and assets

11. **Many creatures keep the rest pose because their clips are not found.** Counts of distinct failing creature folders in the log (idle, walk, run clips): mudcrab, hare, chicken, cow, horker, slaughterfish, icewraith, riekling, scrib, netch, boar-riekling (`skeletonboar.hkx`), dragonpriest, vampirelord, vampirebrute, atronachflame, dwarven centurion variants (sphere, steam, ballista), dwarven spider, plus every Khajiit and Argonian actor (`skeletonbeast.hkx`, `skeletonbeast_female.hkx`, 14 lines). Draugr bed and sleep clips are missing too. No `mt_idle` in those folders: they need creature profile entries (tasks 0212, 0216, 0246 built a table of 17 creatures).
12. **Sea floor / Pilgrim's Trench:** the player is at z -17044 and the frame is the underwater tint with a breath bar, so deep water is drawn and the underwater state works there. No swimming controls were tried (part 1 item 13 stands).

## Per-place results

`Load` is seconds from the teleport call until the cell was usable (1 to 2 processes). `Objs` is the object count the neutral cell inserted. Notes come from the frames in `keep\*.png`.

| Place | Run | Kind | Load s | Objs | Crash/freeze | Notes |
|---|---|---|---:|---:|---|---|
| Whiterun Stables | wild | tundra ext | 0.0 | | no | start spot; frame is a wooden wall |
| Evergreen Grove | wild | forest ext | 15.2 | | no | good: rocks, pines, ground clutter |
| Stonehills | wild | marsh ext | 22.5 | | no | good: frosted trees, camp |
| Throat of the World | wild | snow ext | 30.7 | | no | snow and cliffs, a dragon attacks |
| Reachwater Rock | wild | reach ext | 16.5 | | no | rain, rocks, trees |
| Steamcrag Camp | wild | volcanic ext | 13.7 | | no | good: camp, trees, blue sky |
| Bonechill Passage | wild | ice ext | 18.8 | | no | good: snow, Nordic arch, pines |
| Pilgrim's Trench | wild | sea ext | 53.7 | | no | underwater tint, breath bar (z -17044) |
| Fort Dawnguard | wild | fort ext | 14.5 | | no | rain, ground, trees |
| DLC1DawnguardHQ01 | wild | fort int | 65.3 | 1614 | no | solid green frame (item 8) |
| Castle Volkihar | wild | castle ext | 44.4 | | no | snow, dark stone; watchman dialogue box open |
| DLC1VampireCastleGuildhall | wild | castle int | 53.3 | 3236 | no | very dark; dialogue box |
| Raven Rock (sol2) | sol2 | Solstheim ext | 10.3 | | no | magenta plants (item 6) |
| DLC2RRMorvaynManor | sol2 | house | 10.4 | 170 | no | dark, guard, small cell |
| DLC2RRAbandonedBuilding | sol2 | house | 1.4 | 63 | no | near black, small cell |
| DLC2TelMithrynStewardsHouse | sol2 | tower | 3.4 | 104 | no | fine: mushroom walls, bed |
| Highpoint Tower | sol3 | ext | 5.2 | | no | ash sky, trees, some magenta |
| DLC2HighpointTower01 | sol3 | int | 111 | 2759 | no | near black frame |
| Nchardak | sol3 | ext | 8.8 | | no | Dwemer ruin, a Lv 25 bandit attacks |
| DLC2Nchardak01 | sol3 | int | 74.8 | 798 | no | dark Dwemer wall |
| Temple of Miraak | sol3 | ext | 15 | | no | timber and snow |
| DLC2TempleOfMiraak01 | sol3 | int | 60.9 | 2016 | no | dark green void (item 8) |
| Bleak Falls Barrow | dung1 | ext | 15.0 | | no | cairn on the hill |
| BleakFallsBarrow01 | dung1 | dungeon | 134.9 | 2512 | no | dark, bandit speech line |
| Embershard Mine | dung1 | ext | 18.8 | | no | good pines and sky |
| EmbershardMine01 | dung1 | mine | 40.6 | 1128 | no | dark, bandit Lv 5; water plane 0 |
| Dustman's Cairn | dung1 | ext | 19.4 | | no | rain, cliff |
| DustmansCairn01 | dung1 | dungeon | 92.4 | 2349 | no | pure black frame |
| Labyrinthian | dung1 | ext | 57.8 | | no | snowy mound |
| Labyrinthian01 | dung1 | dungeon | 65.9 | 720 | no | underwater (item 7) |
| Alftand | dung1 | ext | 42.2 | | no | snow, wooden scaffold |
| Alftand01 | dung1 | dungeon | 139.1 | 2329 | no | teal wash (item 7) |
| Mzulft | dung1 | ext | 24.8 | | no | rock face |
| Mzulft01 | dung1 | dungeon | 99.2 | 2464 | no | underwater, water strips (item 7) |
| Fellglow Keep | dung1 | ext | 59.5 | | no | rain, flame sentry attacks |
| FellglowKeep01 | dung1 | dungeon | 89.5 | 2236 | no | dark; vampire jailer speech |
| Ilinalta's Deep | dung1 | ext | 13.7 | | no | good, stream and ruin |
| IlinaltasDeep01 | dung1 | dungeon | 71.2 | 1717 | no | dark |
| Frostmere Crypt | dung1 | ext | 35.0 | | no | fight: Eisa Blackthorn vs pursuers |
| FrostmereCrypt01 | dung1 | dungeon | 54.5 | 1714 | no | dark, berserker fights |
| Raldbthar | dung1 | ext | 37.0 | | no | frosted trees |
| Raldbthar01 | dung1 | dungeon | 48.1 | 1638 | no | dark red-brown panels |
| Sky Haven Temple | dung1 | ext | 21.6 | | no | good: sky, plateau |
| SkyHavenTemple | dung1 | temple | 43.0 | 422 | no | dark stair |
| Lost Prospect Mine | dung1 | ext | 22.6 | | no | cave mouth, snow |
| LostProspectMine01 | dung1 | mine | 22.9 | 698 | no | dark; water plane 0 |
| Irkngthand | irk, irk2 | ext | 12.7 / 4.7 | | no | repeats clean (items 1) |
| Irkngthand01 | irk, irk2 | dungeon | 32.1 / 34.1 | 970 | no | |
| Markarth, Arnleif, Silver-Blood | mark | | 12.5 / 11.8 / 20.6 | 585 / 802 | no | no exit |
| Dragon Bridge, Four Shields | drag | | 9.4 / 6.9 | 341 | no | no exit |
| Capital chain (16 places) | caps | | see `keep\caps_summary.txt` | | no | MarkarthUnderStoneKeep 151 s, SolitudeBluePalace 38.9 s |

## What was not covered

- Unexamined by eye: the interior frames are mostly dark or spawn in empty corners, so lighting, props and navigation inside the dungeons are only counted (objects inserted), not judged.
- Castle Volkihar, Fort Dawnguard and Solstheim quests and set pieces; dragons in flight beyond the one seen at Throat of the World; swimming controls.
- No NPC movement data; no performance numbers (shared machine).
- `CLAUDE.md` changelog row not added: the worktree branch is based on an old upstream commit with no `docs/tasks` or project `CLAUDE.md`, so the row belongs on `main` when this is merged.
