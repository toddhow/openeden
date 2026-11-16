# Task 0209: Skyrim SE music (milestone M-S7)

Design doc section: `docs/design/skyrim-se-implementation-plan.md` (M-S7, music); decision 0013 (game packs).
Goal: a Skyrim session plays Skyrim's music: the type that fits the place and the fight, a track of it chosen by a seed, played through the engine's music channel with the type's fade.
Status: built on branch `music` (local, not pushed). Off by default behind `[extension.skyrimse] music`.
Inherited code touched: `apps/openmw/mwsound/soundmanagerimp.cpp` (`streamMusic`, `updateMusic`), `apps/openmw/mwbase/soundmanager.hpp` (one enum value), `apps/openmw/mwlua/soundbindings.cpp` (`ambient.stopMusic`), `apps/openmw/mwworld/worldimp.cpp` (`World::update`, `World::clear`), at the parent commit `af535f742c`.

## 1. Survey (what exists, with citations)

**Who streams music today.** Not C++: `MWBase::SoundManager::streamMusic(path, type, fade)` is called by the Lua binding `ambient.streamMusic` (`apps/openmw/mwlua/soundbindings.cpp`), by the title screen (`mwstate/menu.cpp`) and by the level-up window. The Morrowind playlists are the Lua player script `files/data-mw/scripts/omw/music/music.lua` (registered in `files/data-mw/builtin.omwscripts.in`, shipped as `vfs-mw`), which registers an `explore` and a `battle` playlist built by `scripts/omw/music/helpers.lua` from the VFS folders `music/explore` and `music/battle`. **In a Skyrim session with `Skyrim - Sounds.bsa` mounted that script already plays Skyrim files from the same folder names** (the parent plays `music/explore/forestfall/palette/day/b/mus_palette_forestfall_day_b_05.xwm` in the Bannered Mare; evidence below), by folder and not by the game's rules. So with the setting on the Skyrim music must own the channel.

**Sound manager** (`soundmanagerimp.cpp`): `streamMusic` gives the old stream a fade out (`setFadeout`) and starts the new one when it has faded (`advanceMusic`, `updateMusic`); there is no fade in, no ducking and no layers. `MusicType` was `Normal` and `MWScript`; `updateMusic` reset the type to `Normal` whenever nothing played (so a type alone cannot hold the channel). The files come from the VFS through ffmpeg (`.xwm` is xWMA, the same reader as the `.fuz` voices).

**Records** (measured with an independent Python reader over the five installed masters, `I:\Projects\OpenEden-work\papyrus\music\s1.py` to `s10.py`):

| Record | Count | Fields (VERIFIED sizes) |
| --- | --- | --- |
| MUSC (music type) | 74 | EDID; FNAM 4 flags; PNAM 4 (u16 priority, u16 ducking); WNAM 4 float fade seconds; TNAM n x 4 MUST ids (1 to 47; an id repeats: a weight) |
| MUST (music track) | 352 | EDID; CNAM 4 kind (283 single `0x6ED7E048`, 21 palette `0x23F678C3`, 5 silent `0xA1A9C4D5`); FLTV 4 float (silent length, palette duration); DNAM 4 float fade out; ANAM zstring file (330, all `.wav`); BNAM zstring finale; FNAM float array cue points (11 to 25); LNAM 12 (loop start, end, count; 1 record); CITC and CTDA 32 each; SNAM palette members (n x 4, zero separates groups) |
| CELL XCMO | 992 cells | 4 byte MUSC id |
| LCTN NAM1 | 8 locations | 4 byte MUSC id (PNAM is the parent location) |
| REGN RDMO | 53 uses | 4 byte MUSC id |
| WRLD ZNAM | 62 worlds | 4 byte MUSC id (`WhiterunWorld` = `MUSTownTest`, `Tamriel` = `_NONE`, so open-world music comes from regions) |
| DOBJ DNAM | entries of 8 bytes (code, form id) | code `BTMS` is the battle music: `MUSCombat` (`0x3418e`); `DTMS` death, `SCMS` success, `DCMS` dungeon cleared are not used here |

The conditions of the 352 tracks (220 CTDA) use three functions only: 18 GetCurrentTime (162; compare `>`, `>=`, `<`, `<=` with hours 5, 6, 8, 18, 20, 22), 149 GetIsCurrentWeather (48; parameter a WTHR, the value 0 in 42 and 1 in 6: `DLC1Eclipse`), 707 GetCombatTargetHasKeyword (10; `ActorTypeDragon`, value 0 or 1, run-on 2). Of the 330 file names 320 resolve to an `.xwm` of the same path in `Skyrim - Sounds.bsa` (the data says `Data\Music\Tavern\MUS_Tavern_01.wav`, the archive holds `music\tavern\mus_tavern_01.xwm`); the other 10 are test tracks.

**Flags and priority (PNAM, FNAM).** Priorities seen: 1, 2, 3, 5, 47 to 50, 54, 55, 100; the combat type is 2. Every type with a ducking value has the flag `0x20`. 14 distinct flag words. The bit meanings (0x01 plays one selection, 0x02 abrupt transition, 0x04 cycle tracks, 0x08 keep order, 0x20 ducks the current track, 0x40 does not queue) are RECALLED-VERIFY.

**Palette SNAM.** 21 palettes list member tracks, several of them silences (`_MUSExploreSILENT10`), and zero ids between runs. The groups-between-zeros reading is RECALLED-VERIFY (nothing in the data settles it).

## 2. Design

* **SDK** `sdk/include/openeden/game/music_source.hpp` (DRAFT 0.1, additive, SDK stays 4.0, found by `dynamic_cast` on the registered cell source like `WeatherSource`): `musicEnabled()`, `musicReady()`, `musicType(context)` and `musicPlan(type, context)`; plain values only (a type description with priority, ducking, fade, flags, reason; a plan of `File` and `Silence` items with VFS paths).
* **Pack** (`game_packs/skyrimse/src/music.cpp`, `music.hpp`, `music_data.hpp`): one walker reads MUSC, MUST, the music links of CELL, LCTN, REGN, WRLD and the DOBJ battle entry on a worker thread (`MusicRuntime`, started by the first ask, never blocks the frame); `MusicIndex` merges a load order; `MusicChooser` is pure. **Place rule (RECALLED-VERIFY, the data cannot show the order):** the cell's XCMO, then its location's NAM1 and the parents', then the best region's RDMO (the lowest priority number; ties by the first listed), then the worldspace's ZNAM. In a fight the DOBJ BTMS type replaces the place type. **Track rule:** the type's tracks whose conditions pass (the existing conditions evaluator with a small world: game hour, weather unknown, no combat-target keyword), chosen by `splitmix64(seed, type, pick)`; a type with the cycle or keep-order flag goes round by the pick counter. A repeated TNAM id is a weight. Palettes expand to one group, silences to waits; a track whose file does not exist in the VFS is skipped for the next candidate. No clock anywhere.
* **Combat:** `SkyrimCombat::playerInCombat()` is the one read the music makes: an engaged AI fighter whose target is the player and that has not given up (needs `[extension.skyrimse] combat ai`).
* **Engine** (`apps/openmw/mwworld/neutralmusic.*`, `neutralmusicdriver.*`): `NeutralMusicDirector` (pure, tested with a fake source and sink) asks the type every 0.25 s of dt, enters a new type with its fade, streams the plan's files one after the other (a file has ended when the sink says it stopped, after a 0.5 s grace), waits out silences by dt, counts the pick, treats three quick ends as a failure and waits 5 s, and plays nothing more of a "plays one selection" type until the type changes. `NeutralMusicDriver` finds the pack's source, the player's neutral cell and the sound manager. The channel is claimed with the new `MusicType::Neutral`: while it is the type a script's `ambient.streamMusic` and `ambient.stopMusic` do nothing (`soundmanagerimp.cpp`, `soundbindings.cpp`), and `updateMusic` keeps the type between tracks.
* **Setting:** `[extension.skyrimse] music` (bool, off) and `music seed` (int, 1), declared in `extension.json`. Off: no runtime is made, `musicEnabled()` is false, the engine makes no driver and calls nothing.
* **Saving:** nothing is saved. A load starts a fresh director (pick counter 0), so the first track of a place after a load is the same as in a new game; whatever was playing restarts. `World::clear()` drops the driver.

## 3. Stages

1. Survey and this doc. 2. Pack records, index, chooser (tests first). 3. SDK interface, runtime, cell source. 4. Director and driver (tests), sound manager ownership. 5. In game: inn, Whiterun street, a fight in and out; setting off equals the parent; Morrowind unchanged.

## 4. Acceptance

See the stage log below.

## 5. Out of scope and not built (decisions for toddhow in the stage log)

Ducking is carried and traced but not applied (the sound manager cannot attenuate a music stream); the finale (BNAM) and cue points are decoded and traced, not played; there is no fade in (the sound manager has none); boss, death, discovery, success and dungeon-cleared music (the DOBJ entries other than BTMS, and per-NPC boss types) are not driven; the current weather is not known to the pack (a track "weather is X" fails, "weather is not X" passes); the Morrowind music script keeps playing in a session where the setting is off.

## Stage log

**Built** (same hand wrote code first, then tests; local commits on branch `music` from `af535f742c`, not pushed; Windows only).

**Tests.** The tests were written AFTER the code, so they were not run red against stubs. Instead eight one-line mutations were applied together and both test binaries rebuilt: (1) `++mQuickEnds` removed, (2) a new type's fade ignored, (3) the "plays one selection" flag ignored, (4) a silence not counting down, (5) the location step of the place rule skipped, (6) track conditions not evaluated, (7) the cycle and keep-order flags ignored, (8) the stored file name tried before the `.xwm`. Result: 5 of 20 `SkyrimMusic*` tests failed (`aLocationsMusicComesFromItsParent`, `theXwmNameIsTriedFirstThenTheStoredName`, `aSingleTrackIsOneFileItem`, `conditionsOnTheGameHourSelectTheTrack`, `aTypeThatKeepsItsOrderGoesRoundByThePickCounter`) and 6 of 14 `NeutralMusic*` tests failed; all eight mutations were caught. Reverted, rebuilt: 20 of 20 and 14 of 14 pass. Two pack tests failed on the first run, both faults of the tests (a cycle-flag type used for a seed test; a runtime test plugin not named like its load order entry); fixed in the tests. Wider runs: `components-tests` `Skyrim*:Frozen*:Extension*:Neutral*` 1,673 pass, `openmw-tests` `Neutral*:ExtensionHost*:Weather*:Sound*` 629 pass. **No existing test was edited.**

**In game** (`tools/neutral_session_check/check.sh`, five masters plus `fallback-archive=Skyrim - Sounds.bsa`, `[Sound] master volume = 0.0` and `music volume = 0.0`, `OPENEDEN_QUIET_WINDOW=1`, `OPENEDEN_FIXED_FRAME_TIME`, `set gamehour to 12` through `--script-run` unless stated; the trace lines are `Neutral music:` and the sound manager's own `Playing "..."`). Three runs each, byte-identical traces after the hour was printed as a whole number (the hour of the first plan depends on how soon the worker has the records, so a decimal hour differed by 0.01 in one run of four):

* Bannered Mare (900 frames): `type MUSTavernB (cell, priority 55, fade 0.00 s, ducking 0, flags 4)`, `track MUSTavern03`, `stream music/tavern/mus_tavern_03.xwm`, `Playing "music/tavern/mus_tavern_03.xwm"`.
* Whiterun street `@WhiterunWorld,25670,-7633` (1,500 frames): `type MUSTownTest (world, priority 48, fade 6.00 s)`, `track MUSTownDay01`, `Playing "music/town/mus_town_day_01.xwm"`; without the hour script (the session starts at hour 0) `MUSExploreNight01`, so the hour condition is read.
* A fight (6,000 frames, `neutral actors`, `neutral actor idles`, `neutral combat`, pack `combat ai`, `combat test weapon = DaedricGreatsword`, `combat test player swing = 1.5`, scenario `n5ai` with `AI_DIST=100 AI_CAM='-300, 0, 150'`): tavern (`MUSTavern03`), then `type MUSCombat (combat, priority 2, fade 9.00 s, ducking 10000, flags 36)` with `MUSCombat02` when Hulda is hit, three blows kill her (`COMBAT death`), then `MUSTavernB` and `MUSTavern04` again (pick 2). 14 trace lines.
* Setting off: no `Neutral music` line in the Mare, the street or Balmora; the Morrowind script plays its own file as in the parent. Morrowind unchanged: `worldobjects_check` result 111 lines identical to the parent build of this worktree; the fixed Balmora frame (seed 7, frame 600) `1c5d47ad0b5dc32d` for the parent and this build. A Morrowind track still plays (`music/explore/mx_explore_4.mp3` in the parent run, `music/explore/morrowind title.mp3` in the new one: the Lua script picks at random).
* Layer rules 104 exceptions; `check_tests_registered`, `check_extension_sdk`, `check_mwbase_fakes`, `check_game_conventions`, `check_no_osg` pass.

**Decisions for toddhow.**

1. **The Morrowind music script.** `music.lua` plays Skyrim files from `music/explore` and `music/battle` in any Skyrim session that mounts `Skyrim - Sounds.bsa`, setting or not. With the setting on the Skyrim music owns the channel (`MusicType::Neutral`: a script's `streamMusic` and `stopMusic` do nothing); with it off nothing changed. Should the Morrowind script be switched off in a Skyrim session whatever the setting?
2. **Ducking, finale, cue points, fade in** are decoded and traced, not played (the sound manager has none of them). Ducking would need a music gain on the manager; the finale (BNAM) would play when a fight ends.
3. **Priority** is carried and used only to order regions; a lower number is taken as more important (data: combat 2, exploration 48 to 55: RECALLED-VERIFY).
4. **The place rule** (cell, location, region, world) and **palette groups between zeros** are RECALLED-VERIFY.
5. **The weather** is unknown to the pack, so "weather is X" tracks never play and "is not X" ones do (the data uses only `DLC1Eclipse`).
6. **Combat music needs `combat ai`.** Without it nothing marks the player as in a fight.
7. **The first track after a load or new game** is picked when the worker has the records (a few seconds); the very first seconds of the session are silent (the script's music is stopped at once).
8. A silence waits by seconds of dt, so it pauses while the world is paused (the director is not updated then).

**Listening check for toddhow.** `[extension.skyrimse] music = true` and `fallback-archive=Skyrim - Sounds.bsa` in the config; start the Bannered Mare (`--start WhiterunBanneredMare`): a tavern track should play; walk out to the street by day and at night; with `combat ai = true` hit an NPC: the combat music starts after the type's 9 second fade and the place music returns when the fight ends. Set the volumes back up first (the runs above were at 0.0).
