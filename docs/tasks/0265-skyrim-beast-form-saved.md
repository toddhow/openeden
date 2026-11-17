# Task 0265: Skyrim werewolf beast form survives a save and a load

Design doc section: `docs/design/game-packs.md`; tasks 0259 (the werewolf form), 0260 (vampirism's chunk, the model), 0228 (the P key and the daily power cooldown).
Goal: a save made while the player is a werewolf loads, in a new process, with the player a werewolf again: body, clips, camera, claws, abilities and the remaining time; a form that had ended loads as human. Branch `beastsave` from `457bb898b5`. Same hand wrote tests and code (tests first, run red against stubs); no fresh reviewer; Windows only; local commit, not pushed.
Behind `[extension.skyrimse] werewolf` (off by default, unchanged). With the setting off the chunk is not registered, so a save has none; a save without the chunk loads as before.

## 1. What is saved, and where

| state | where | note |
| --- | --- | --- |
| the form is on, the seconds left, the corpses fed on, Beast Form granted | NEW chunk `werewolf` v1 (`werewolf.cpp`: `BeastSaved`, `encodeBeastSaved`, `decodeBeastSaved`, `makeBeastChunk`) | 10 bytes + 8 per corpse: u8 granted, u8 active, f32 seconds, u32 count, count x u64 (placement ids, sorted, no repeats; at most 4096) |
| when Beast Form was last used (once a game day) | the existing chunk `character_powers` (task 0228) | it already keys the last use by the power's form id, and the P key is the only way the power is used, so it is not repeated |
| the body, camera, claws, abilities, the HUD/inventory refusals | rebuilt from the form's state | nothing of them is stored |

- **Strict decode.** A form on needs seconds in (0, 600]; a form off needs 0 seconds and no corpses; flags 0 or 1; a fed list sorted and without repeats; exact length. A refused chunk reaches nothing (`read` returns false).
- **Load.** `beastRestore`: the grant comes back (`granted` is the test grant or the saved one), a form that is on in this process but off in the save ends quietly, and a form that was on is kept as *pending* and begun again by the next pack frame in which the race's numbers can be read (`beastBegin`, the same function a fresh change uses, with the saved seconds and fed set), so the order the chunks are read in does not matter (the player's level, for the level ability, comes from the progression chunk). A save made while the form is still pending writes the pending state.
- The warning at 15 s is not saved: it follows from the seconds (a form restored under 15 s does not warn again; over 15 s it will).
- A new save with the form on: the clock counts pack seconds (task 0259 decision 2), so the time left is the same whatever the real time between save and load.

## 2. Vampirism and the werewolf

Read in the CK sources (`Scripts.zip` in the game's Data folder): `PlayerWerewolfChangeScript`, `WerewolfChangeEffectScript`, `PlayerVampireQuestScript`, `VampireDiseaseEffectScript`, `CompanionsHousekeepingScript`, `DLC1PlayerVampireChangeScript`. **No shipped script forbids a vampire from changing into the werewolf, or the reverse**: `PlayerVampireQuestScript` never mentions lycanthropy, `PlayerWerewolfChangeScript` never asks for a vampire, and `CompanionsHousekeepingScript` even stores a vampire race as the original race (`NordRaceVampire` -> `NordRace`) so that a vampire's change and change back keep his race. Whatever keeps the two apart in the game lives in dialogue conditions of the Companions quest line (not read here) and in the disease resistance of `AbWerewolf` (the ability the form carries; resist disease 100 reaches the diseases path through task 0241's ability sums, so a werewolf does not catch Sanguinare Vampiris while it lasts).
**What this task enforces: nothing new.** Both states are saved by chunks of their own (`vampirism`, `werewolf`) and restore independently; a test saves and restores each alone and both together. Whether a vampire may use Beast Form is a decision for toddhow (section 7).

## 3. Tests

`testskyrimsewerewolfsave.cpp` (new, in the components-tests list): 12 tests: the layout by hand, round trips, the canonical rules (sorted, no repeats, clamped, a form with no time is off), the refusals (every truncation, a longer chunk, flags, counts, NaN, infinity, negative, over the cap, zero while on, seconds while off, unsorted, repeated) and the handler (name, version, a good read reaches `set`, a bad one does not, write-then-read).
RED: against stubs of `encodeBeastSaved`, `decodeBeastSaved` and `makeBeastChunk` (every call refusing / empty / null) **12 of 12 failed** (components-tests built with the stubs and the glue in place; the glue is not unit tested, see section 6).

## 4. Edits outside the new files

`game_packs/skyrimse/include/game_skyrimse/werewolf.hpp` and `src/werewolf.cpp` (the codec and the chunk), `src/skyrimse_game_pack.cpp` (`startWerewolf` registers the chunk; `beastStart` is now `beastBegin`; `beastSaved`, `beastRestore`, `beastApplyPending`; `beastEnd` takes a quiet flag; two members), `apps/components_tests/CMakeLists.txt` (one test file). No SDK, engine or Lua file changed; no virtual added to `CombatRequestWriter`; no existing test edited.

## 5. Evidence

**Tests.** `SkyrimWerewolfSave*` 12 of 12 pass after the code; `SkyrimWerewolf*:CombatWerewolf*:SkyrimVampirism*` 67 pass (4 gated skips); the wider `*Skyrim*:*Creature*:*Combat*:*Extension*:*Neutral*:*Frozen*` filter 3,471 pass, 0 fail. No existing test edited.

**In game** (`tools/werewolf_check/check.sh`, now with `SAVE_AT` and `PHASE=run|load`; Bannered Mare, quiet window, volumes 0, fixed frame time, `neutral saves`; the run creates the character by a real click, presses the REAL P key twice and the menu script saves once; the load starts a NEW process with `--load-savegame` and the test grant OFF, so the power comes only from the chunk). Logs in `I:\Projects\OpenEden-work\papyrus\ww265a` to `ww265f` (`.load` folders for the loads). Frames kept in `I:\Projects\OpenEden-work\papyrus\ww265_keep` and viewed:
- **A. saved 116 s into a 120 s form** (`ww265a`): the load logs `WEREWOLF restored: the beast form, 115 s left, 0 corpses fed on, Beast Form granted`, the race numbers are not read for a few frames (warned once), then `transformed: 115 s, claws 20 reach 150`, `COMBAT player beast form on`, abilities `vitals 150 0 200 carry 2000`, health maximum 400, `PLAYERBODY built revision 3: 1 parts` (the werewolf), third person (frames `01` and `02`: the werewolf standing, and seen from the side), the clock runs down (`left=115, 114, ...`). Claws after the load: with the player in front of the living Hulda four swings of `werewolfbeast::ww_attackrightfast` give `COMBAT damage player -> Hulda ... 20.000 ... 75 -> 55 -> 35 -> 15 -> 0`, then `COMBAT death` and `WEREWOLF fed on a person, 117 s left, healed`.
- **B. the form ended before the save** (`ww265e`: 20 s form, `back to human (the time ran out)`, saved afterwards): the load logs `restored: human, Beast Form granted`, the body is the human one (`PLAYERBODY built revision 2: 8 parts`), the abilities are the human ones (vitals 0), the power is listed but NOT ready (`WerewolfChange:works:false`: the last use is in the `character_powers` chunk) and it is granted by the chunk alone.
- **C. fed corpses** (`ww265f`: Hulda killed and fed on, saved 54 s left): the load logs `49 s left, 1 corpses fed on` and counts down 48, 47, ...; the corpse stays dead after a load (neutral saves), so swings after the load miss it (`COMBAT miss`), as they should.
- **D. a form with 4 s left** (`ww265b`, `ww265c`, `ww265d`: my first attempts to save after the end saved in the last 4 s, because the world stops while the scripted Space press opens Hulda's conversation): the load restores 4 s and `WEREWOLF back to human (the time ran out)` follows after 261 frames.
- **Three loads of save A** (`ww265a_load_run1..3.log`, the claw scenario): the WEREWOLF, COMBAT and PLAYERBODY lines match as a set in two of three runs; they are NOT byte identical: **the form is begun again in the first frame in which the records worker has read the race numbers, which differs from run to run (the form's first frame was f3 or f22, the warning line repeated, one feed read 117 or 118 s left).** Everything that does not depend on that frame (damage numbers, death, restored seconds and fed count) is the same in all three. The repeated warning is now said once (rebuilt, one warning in the last load).

**Morrowind unchanged:** `tools/worldobjects_check` 110 lines OK against the `integ2` build of `main` (not a build of the exact parent); the fixed Balmora frame (`--start Balmora --random-seed 7`, `OPENEDEN_FIXED_FRAME_TIME`, frame 600) SHA-256 `1c5d47ad0b5dc32d` with the settings off and with `neutral world`, `neutral actors`, `neutral combat`, `neutral saves` and `werewolf` on (the same hash as the earlier rows). With `werewolf` off the chunk is not registered, so a save has none and an older save loads as before (no read is called).

## 6. Not run, limits

Not run: the Linux gate and GCC `-Werror` (traps checked by hand), clang-format 14 (formatted by hand to 120 columns), clang-tidy, the Lua tests, `openmw-tests`, a fresh review, a load in the SAME process while the form is on (the glue ends it quietly, by reading only: `beastRestore`), a save made while a loaded form still waits for the race numbers (it writes the pending state, by reading only), a vampire werewolf in game, an old save without the chunk (no read is called by construction; not loaded in game).
Limits: the glue (`beastSaved`, `beastRestore`, `beastBegin`) has no unit test (it lives in `skyrimse_game_pack.cpp`, like the other pack glue); the clock counts pack seconds, so a saved form does not run down while the game is off; the `character_powers` chunk keeps the last use per power for ALL powers (task 0228), and the character service is required for the P key.

## 7. Decisions for toddhow

1. **A vampire and the werewolf.** Nothing in the shipped scripts forbids either state while the other is on (section 2); nothing is enforced here. If vanilla's exclusion is wanted (it is probably in the Companions dialogue conditions, which are not built), the cheapest rule is a refusal in `beastStart` while `mVampirism->vampire()`.
2. **The grant is saved in the chunk** (`mGranted`), so a character that got Beast Form by the test setting keeps it in a later session without the setting. The real grant (the Companions quest line) is not built.
3. **A loaded form restarts when the race numbers can be read** (the records worker), so its first frame varies by a few frames from run to run (above).
4. A form restored under 15 s does not warn again; one over 15 s will.
