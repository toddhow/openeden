# Task 0235: Skyrim NPC reactions to the player's crimes and stance, and lip sync for barks

Design doc section: `docs/design/game-packs.md` (the Skyrim SE pack), milestone M-S7. Continues task 0231 (barks); uses tasks 0204
(crime), 0227 (sneaking), 0194 (scene speech) and 0146 (lip sync).

Goal: (1) a witness of a crime speaks its crime line, (2) an NPC reacts to the player's stance (sneaking, a drawn weapon or
spell), (3) the mouth of an NPC that barks or speaks a scene line moves with the line, as a conversation's speaker's does.

Status: built, Windows only, branch `barks2` (from `dccdca12ae`). Behind the settings that already guard the pieces: barks
(`[extension.skyrimse] barks`) and `[Game] neutral lip sync`; the sneak and crime reactions also need `sneak` and `crime`.
No new setting.

## 1. Survey (five official masters, `SkyrimBarkReactionsRealData`, aggregates)

Which dialogue INFOs are gated on the player's stance (CTDA function 263 IsWeaponOut, 101 IsWeaponMagicOut, 286 IsSneaking)
and on whom the condition runs:

| subtype | function | INFOs | run-on |
| --- | --- | --- | --- |
| HELO (hello) | IsWeaponMagicOut | 42 | target (the player) |
| HELO | IsWeaponOut | 5 | target |
| IDLE (chatter) | IsSneaking | 321 | 297 reference 0x14 (the player), 24 the speaker |
| CUST (custom), SCEN | the three | 2 + 2 + 2 | target, reference or alias |

So Skyrim's comments on a drawn weapon are HELLO lines, and its comments on a sneaking player are IDLE lines, both chosen by
their own conditions; the engine part is only "answer the condition for the player" and "give the actor an occasion to speak".
The occasion subtype for a sound that nearly woke an NPC is NOTA (NormalToAlert: the name is RECALLED-VERIFY, nothing in the data says it; 31 topics, 87 INFOs, 29 of them with no
response text, 5 say-once); crimes: STEA 30 INFOs, ASSA 24, TRES 91, MURD 29 (first survey, task 0231).

The engine already had the stance: `CreatureStats::getDrawState` (`apps/openmw/mwmechanics/drawstate.hpp`: Nothing, Weapon,
Spell) and the sneak stance of task 0227. Nothing in the data tells a fists-only stance from a weapon (IsWeaponOut 1 and 2):
the pack reports a drawn weapon as 2.

## 2. Design

- **Crime witnesses.** `CrimeSystem::setWitnessHook(hook)` (additive): once an evaluated crime counts (a witness whose faction
  collects, after the bounty is charged) the hook is called for each such witness, nearest first, with the crime kind. A trespass
  is judged again until somebody sees it: the hook is called once, with the judgement that found the witness. A throwing hook
  changes nothing. The pack maps Theft and Pickpocket to STEA, Assault to ASSA, Murder to MURD (new) and Trespass to TRES and
  queues the bark. Witnesses without a crime faction are not counted (as before: no bounty, no remark).
- **Sneaking.** `SneakSystem::setAlertHook(hook)` (additive): told every alert change of an observer with whether the player
  sneaks. The pack turns "hidden to caution while the player sneaks" into the bark kind `Notice` (NOTA, new): "What was that?".
  A notice is not said by an actor that is fighting, within `mNoticeGap` (20 s, ours) of its last one, or while it still
  speaks. While the player sneaks no actor greets him (an actor's hello waits until he stands up): `onPlayerStance(drawn,
  sneaking)`; the IDLE lines written for a sneaking player run unchanged. A reaction line whose voice file is missing is skipped
  for another pick (up to 6 tries; the calm barks keep their retry after a second).
- **Drawn weapon or spell.** SDK `SubjectState::drawState` (trailing field, 0 nothing, 1 weapon, 2 spell, default 0, version
  unchanged), filled by the engine's player hook (`NeutralPlayerState::mDrawState`, `worldimp.cpp`, `physicssystem.cpp`). The pack
  reads it once a combat update (`SkyrimCellSource::setStanceSink`) and tells the conditions world
  (`SkyrimConditionWorld::setPlayerStance`: atomics, so the dialogue worker may read them) and the bark system. The edge "nothing to
  drawn" arms the hello of every actor within the hello distance (400 units, 200 high), once each `mStanceGap` (30 s, ours);
  the hello line is then chosen by its conditions, so a line written for a drawn weapon can be picked. The conditions world now
  answers IsSneaking for the player, IsWeaponOut and IsWeaponMagicOut, and counts the three functions as answered; without the
  barks setting nothing sets the stance and the answers are the old ones (nobody sneaks, nothing drawn).
- **Lip sync for barks and scenes.** The mouth step of task 0146 became `MWWorld::NeutralMouthDriver` (pure: `step(saying, file,
  seconds, loudness, loader)` returns the visemes to send or nothing; one driver per speaker); `driveNeutralMouth` is the
  shared glue (sound manager playback, `.lip` read once per file through the VFS, mouth sink). The conversation's session uses
  one driver as before; `World` keeps one driver per scene-speech actor (`mNeutralMouths`), added when a line is queued, dropped
  when the line is over and the mouth is closed (`NeutralSceneSpeech::busy`). Only with `[Game] neutral lip sync`.

## 3. Edits outside the new files

New: `apps/components_tests/skyrimse/{testskyrimsebarkreactions,testskyrimsebarkreactionsreal,testskyrimsecrimewitness,
testskyrimsesneaknotice,testskyrimsestancecond}.cpp`, `apps/openmw/mwworld/neutralmouth.{hpp,cpp}`,
`apps/openmw_tests/mwworld/testneutralmouth.cpp`, this document.

Edited (all additive unless said): `game_packs/skyrimse`: `barks.{hpp,cpp}` (Notice and Murder kinds, `onPlayerStance`, settings
`mNoticeGap` `mStanceGap`, reaction retries, trace lines for a reaction that cannot be said and for a stance edge),
`crime.{hpp,cpp}` (witness hook, 14 lines in `evaluate`), `sneak.{hpp,cpp}` (alert hook, 11 lines in `judge`),
`dialogue.{hpp,cpp}` (stance atomics, `isSneaking`, `weaponOrMagicOut`, `weaponOutState`, `answers`), `dialogue_runtime.hpp`,
`world_view.{hpp,cpp}` (`PapyrusAccess::mSetStance`), `neutral_world.hpp` (stance sink, one call in `combatUpdate`),
`skyrimse_game_pack.cpp` (wiring). `combat.cpp` and the contract's `CombatRequestWriter` are NOT touched. SDK:
`sdk/include/openeden/game/combat_rules.hpp` (`SubjectState::drawState`, trailing). Engine: `apps/openmw/mwphysics/physicssystem.{hpp,cpp}`
(carry `mDrawState`), `apps/openmw/mwworld/worldimp.{hpp,cpp}` (fill it; the scene speech's mouths),
`neutraldialogue.{hpp,cpp}` (the mouth step moved out unchanged into `neutralmouth.*`; the session uses it),
`neutralscenespeech.{hpp,cpp}` (`busy`), the CMake lists of `apps/openmw`, `apps/components_tests`, `apps/openmw_tests`. No existing
test was edited.

## 4. Stage log

**Tests first.** 55 new tests in six files (the pure rules over fakes; no game data); each group was written against a stub
(declarations only, no behaviour) and run red before the code: `SkyrimBarkReactions` (20) 7 of the first 12 failed, then 2 of 3
(sneaking: no hello), 2 of 3 (reaction retry) and 1 of 2 (a review finding: no failure trace every frame for a fighter whose
blow cries find no line) failed; `WitnessHookTest` (9) 4 failed; `SkyrimSneakAlertHook` (7) 3 failed; `SkyrimStanceCondition`
(8) 4 failed; `NeutralMouth` (9) 6 failed and `NeutralSceneSpeechBusy` (2) 1 failed (the other tests of each group test absence or
refusal and pass against a stub): **30 red of 55**. After the code: `components-tests` 12,501 pass (43 skipped, data gated;
without the Slang, complement, property and real-data groups) and `openmw-tests` 2,882 pass (2 skipped, data gated). The gated real-data tests (`OPENEDEN_SKYRIMSE_DATA` set: `*Skyrim*Real*`, `SkyrimSeConditionsReal`, the dialogue, bark and
condition-state ones, which use `answers`, `isSneaking` and the other changed functions): 77 pass, 6 skipped (the mod-list ones and
other opt-in dumps), none fails; `SkyrimBarkReactionsRealData` printed the survey above. No existing test was edited or needs editing.

**In game** (Bannered Mare, the five masters and `Skyrim - Voices_en0.bsa`, `OPENEDEN_QUIET_WINDOW=1`, fixed frame time, all volumes
0, Lua and physics on the main thread, `barks test start = 100` so that the bark clock does not depend on how fast the dialogue
data was read; a run's timeline is the traces below). Traces compared with timestamps removed, three runs each:

* **A witnessed theft** (`n8crime`, the player 100 units in front of Hulda takes 7 Ale from the owned chest at script time 115;
  `crime`, `combat ai`, `barks`, `neutral lip sync`): `CRIME theft witnessed by hulda faction 000267ea bounty +18`, then
  `BARK steal 108142 .../dialoguegeneric__000d403f_1.fuz "By Ysmir, a thief!"` in the same frame; subtitle box in the frame
  `evidence\bark_steal_hulda.png`; **three runs give byte-identical traces (4 lines, md5 `0af5a1291b`)**.
* **Lip sync of that bark:** `NLIP lip data of .../dialoguegeneric__000d403f_1.fuz: 86 frames, first -22` and about 90 mouth
  uploads while the line plays (`OPENEDEN_NEUTRAL_LIP_TRACE`); the montage `evidence\bark_lipsync_montage.png` (Hulda's mouth
  before the line, then parted, then open with the teeth showing) shows the mouth moving. The weights follow the real playback
  position, so the NLIP lines are probably not identical between runs (not compared; only the BARK and CRIME lines were). Idle barks with a one-space text
  move the mouth too (one file of the four had a lip part that the reader refuses, as in conversations).
* **A sneaking player noticed** (`tools/sneak_check` with the REAL sneak button of a controller and the player teleported 150
  units in front of each actor): `SNEAK saadia hidden -> caution`, then `BARK notice 881925 .../dialoguegeneric__0001c1e7_1.fuz
  "Hello? Who's there?"`; frame `evidence\notice_saadia.png` (box "Saadia: Hello? Who's there?", the eye shows Detected). Hulda,
  Mikael, Brenuin and Sinmir noticed too (`hidden -> caution`) and **said nothing**: the NOTA INFO the data picks for them has no
  response (29 of the 87 NOTA INFOs have none), it is marked said, and the retries find `no line passes` (the trace says so,
  `BARK notice 108142 no voice file for ""` three times, then `no line passes`). **Three runs: identical, 54 lines (md5
  `aea9d06133`).** While he sneaks no actor greeted him (the arrivals say nothing; the idle lines run).
* **A drawn weapon** (the player 250 units from Hulda, the REAL toggle-weapon key, F; R is another action): `BARK stance: a weapon
  or spell was drawn, 3 actor(s) near will remark on it`, then three hellos 5 s apart, nearest first (`fAIGreetingTimer`):
  "Need something?" (Sinmir, the nearest), "Yes?" (Hulda), "Yes?" (Brenuin). They are ordinary hello lines: which HELO INFO passes (the 47
  weapon-aware ones among 6,535) was not looked at. **Three runs: identical, 27 lines (md5 `a5a3513f4c`).**
* **Morrowind unchanged** against a build of the parent commit (`dccdca12ae`, same tree; the parent executable ran with the new
  build's resources and pack libraries, so only the executable differs): the `tools/worldobjects_check` dump
  (111 lines) is identical for the parent and the new executable, and the fixed Balmora frame (`--start Balmora --random-seed 7`,
  frame 600) has the same SHA-256 prefix `1c5d47ad0b5dc32d` for both, with the defaults and with `neutral world`, `neutral actors`,
  `neutral combat` and ragdolls on.

`CI/check_tests_registered.sh`, `check_layer_rules.sh` (104 exceptions, no new one), `check_extension_sdk.sh` and `check_game_conventions.sh` pass.

Evidence kept: `I:\Projects\OpenEden-work\ragdoll5\evidence` (the three frames, the mouth montage, the three trace files). Layer rules:
no `components/` -> `apps/` include added; `neutralmouth.*` is host code.

## 5. Not run

* The Linux gate and GCC `-Werror` (hand-checked: braced if/else bodies, no shadowing, `[[nodiscard]]` handled), clang-format 14
  (formatted by hand to 120 columns), clang-tidy, the Lua tests, a fresh review, sanitizers.
* Task 0231's recorded combat run against this build: `n5ai` was run again with settings reconstructed from that log (combat ai,
  test hostile Hulda, iron sword, a player swing every 2 s, `barks test start = 90`): 17 BARK lines (engage, attack, lost, idle), no
  failure trace line; it was not diffed line by line against `evidence/combat_bark_lines.txt` because the original run's exact
  settings were not recorded and the timelines differ.
* A Skyrim trace with barks off against the parent (by reading: the stance sink, the witness and alert hooks and the stance
  atomics are only set under the barks setting); the pack wiring itself (the crime-to-bark mapping, the hidden-to-caution
  filter, the stance sink) has no unit test, only the scenarios above; the `Ptr` of a scene speaker kept in `World::mNeutralMouths`
  while its cell unloads mid-bark (`Scene::setNeutralMouth` reads the Ptr's cell; the existing subtitle path of task 0194 keeps
  the same Ptr: not checked).
* A conversation's mouth after the refactor in game (`n5lip` did not reach a conversation in the time the scenario waits; the
  conversation's drive is covered by the unit tests of the shared step and is the old code moved).
* A weapon-aware hello or a sneaking-aware idle line picked in game (not looked for); a spell drawn (only the weapon key was
  pressed); the lip sync of task 0194's scene lines in a frame (the log shows lip data read for them, `witavern__...`, but no
  frame was taken); exteriors; a female or creature voice type with no voice folder; a pickpocket witness (mapped to STEA, not run).
* The sneak and draw scenarios are slow (4 minutes each, the harness limits to 60 frames a second).

## 6. Decisions for toddhow

1. **NOTA lines with no response.** 29 of 87 NOTA INFOs (and a third of NOTC) have no response; the selection can pick one,
   mark it said and leave the actor silent (Hulda, Mikael, Brenuin, Sinmir above). Skyrim probably does not count such an INFO
   as a line. Fix: skip INFOs with no response in `barkLine` (the shared `selectInfo` is the dialogue's, so I did not touch it).
2. A witness is only an actor whose faction collects (a crime faction); a civilian or animal without one stays silent (as it
   collects no bounty). A witness that is the victim of an assault speaks the assault line.
3. No hello while the player sneaks (ours; the data has no such rule, the idle lines carry the sneaking comments).
4. The drawn state reaches the pack as 0, 1 or 2; IsWeaponOut answers 2 for any drawn weapon (a fist stance cannot be told
   apart by the engine's draw state), IsWeaponMagicOut 1 for a weapon or a spell.
5. A draw arms the hello of every actor within 400 units, 30 s apart per actor (ours), one hello at a time as before.
6. The stance is set on the conversation conditions only when barks are on: conversations of other sessions are unchanged. With
   barks on, a conversation with a sneaking or armed player also sees the stance (IDLE and HELO conditions are shared).
7. Scene lines of task 0194 (quests) get the same mouth under `[Game] neutral lip sync`.
8. `SubjectState::drawState` is a trailing SDK field (version unchanged).
9. **Retries spend lines.** A reaction tries up to 6 lines and each line chosen is marked said (say-once and the INFO's fragments run
   at selection), also when its voice file does not exist; the sneak trace shows three empty NOTA picks spent and then `no line
   passes`. A fix that leaves `selectInfo` alone: `barkLine` passes a said-check that also skips INFOs with no response.
10. A caught pickpocket is mapped to STEA, but the data has PICN, PICC and PICT subtypes for it (not read).
11. The periodic blow cries and taunts still retry on every update while their timer is due and no line is found (task 0231's
    rule, unchanged); this task's failure trace is only for reactions, so those retries leave no log line.

