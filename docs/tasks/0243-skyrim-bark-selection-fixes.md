# Task 0243: Skyrim bark line selection fixes

Design doc section: `docs/design/game-packs.md` (the Skyrim SE pack), milestone M-S7. Continues task 0235 (reactions and lip sync
for barks), whose section 6 listed these as decisions; toddhow took the recommendations.

Goal: (1) a bark never picks an INFO that has no response, (2) a line whose voice file is missing is not marked said, (3) a
caught pickpocket speaks from the data's own pickpocket subtype, (4) a fight cry or taunt that finds no line backs off, (5) the
conversation's mouth still moves after the 0235 refactor.

Status: built, Windows only, branch `barks3` (from `d861a5334f`). Behind the settings that already guard the pieces (`barks`,
`crime`, `[Game] neutral lip sync`). No new setting. The Linux gate was not run.

## 1. Survey (five official masters, `SkyrimBarkSelectionRealData`, aggregates; topic editor ids only)

| subtype | topics | INFOs | no response | category | what the data says |
| --- | --- | --- | --- | --- | --- |
| PICN | 7 | 24 | 8 | 3 (the crime category, as STEA, ASSA, TRES) | quests `DialogueGeneric`, `DarkBrotherhood`, the DLC dialogue blocks; IsActorAVictim (fn_314, subject) `== 1` in 10 INFOs and `== 0` in 2 |
| PICC | 6 | 20 | 6 | 3 | the same quests plus `MQ101`; fn_314 `== 1` in 12 and `== 0` in 1 |
| PICT | 3 | 6 | 1 | 7 (miscellaneous) | topics `DialogueGenericPickPocket` and `CommandedPickpocket`: the topic of the pickpocket menu and of a commanded follower, not a reaction |

So PICN and PICC are the detection-category reactions to a caught pickpocket, PICT is the menu topic. **Which of PICN and PICC is
the non-hostile and which the hostile one is RECALLED-VERIFY (my memory of xEdit and the Creation Kit's subtype names:
PickpocketNonHostile and PickpocketCombat); nothing in the data says it.** The pack's caught pickpocket (task 0208) does not make
the victim hostile, so it is PICN; PICC exists as a kind (`barks::Kind::PickpocketCombat`) and nothing raises it.

The same survey for NOTA: 87 INFOs, 58 with a response; of those 51 have no voice type condition (said by every voice type, gated by
quest, faction and similar conditions) and 4 a random roll; the voice-typed ones are 14 types (FemaleSultry, MaleDrunk, MaleCoward,
FemaleUniqueDelphine ...). Hulda is `FemaleNord`, Mikael `MaleYoungEager`, Sinmir `MaleNordCommander`: none of them has a voice-typed
NOTA line, so what they can say is only the generic ones, and none of those passed for them in the Bannered Mare (not traced one by
one). Saadia (`FemaleSultry`) and Brenuin (`MaleDrunk`) have one each.

## 2. Design

1. **No response, no line.** `DialogueRuntime::barkLine` no longer calls the shared `pickFromTopics`. It uses its own
   `selectBarkInfo` (`dialogue_runtime.cpp`), which repeats `selectInfo`'s rules (topics by priority, quest running, say-once,
   ANAM, the quest's own conditions, the INFO's conditions, the Random rule) and, after the conditions pass, skips an INFO that has
   no response with text (`hasResponseText`: the same text lookup as `fillTurn`) or, when a voice probe is set, no voice file
   (`hasVoiceFile`). `selectInfo`, `pickFromTopics` and `SelectionOptions` are not touched, so a conversation chooses as before
   (a test pins that a greeting INFO with no response is still the greeting). **The duplication is a decision for toddhow** (section 6).
2. **Only a line that can be spoken is marked said.** `selectBarkInfo` decides without side effects (the probe's voice counts are
   restored), and `fillTurn`, which marks say-once and runs the INFO's fragments, runs only on the INFO it returns. A line with no
   voice file is therefore never chosen, never marked and never runs its fragment. A line the core then refuses to play
   (`Hooks::mSpeak` false) is still marked said: a known limit.
3. **Pickpocket.** `barks::Kind::Pickpocket` (PICN) and `PickpocketCombat` (PICC); `barks::kindOfCrime(crime::Kind)` is the one
   mapping (theft STEA, assault ASSA, murder MURD, trespass TRES, a caught pickpocket PICN), used by the witness hook in
   `skyrimse_game_pack.cpp`. A pickpocket is a reaction like the others (several tries).
4. **Victims.** Most PICN and PICC lines are written for the victim (`IsActorAVictim == 1`) and the conditions world answered 0
   for fn_314 for everybody, so they could never pass. `CrimeSystem::witnessVictim()` names the victim of the crime while the
   witness hook runs; `BarkSystem::onEvent(kind, actor, subjectIsVictim)` carries it and `BarkSystem::speakerIsVictim()` says it while
   the line is chosen; `DialogueRuntime::barkLine(..., speakerIsVictim)` tells `SkyrimConditionWorld::setCrimeVictim` for the one
   selection, and the world's `crimeVictim()` now answers (fn_314 was already counted as answered, with 0).
5. **Back off.** `Settings::mNoLineGap` (5 s, OURS, named): a blow cry or taunt that finds no line, or whose line the core refuses,
   is asked for again after the gap and not on the next update while its timer is due. It does not draw a random number, so a said line
   keeps its timers. The calm barks keep their own one-second retry.
6. **Reactions ask again.** With the selection skipping unusable INFOs, a pick that finds no line is common (a random roll gates a
   line, or no line has a voice file for this voice type), and the old six-try loop only retried a line with no voice file. A reaction
   now asks again on a new pick when no line passes, up to the same six tries, and counts and traces `no line passes` once, after the
   last. (Found in game: the first version ended Saadia's notice at the first pick: "no line passes"; with the retry she says her line.)

## 3. Edits outside the new files

New: `apps/components_tests/skyrimse/{testskyrimsebarkselection,testskyrimsebarkbackoff,testskyrimsebarkvictim,
testskyrimsebarkselectionreal}.cpp`, this document.

Edited (all additive unless said): `game_packs/skyrimse`: `barks.{hpp,cpp}` (two kinds, `kindOfCrime`, `mNoLineGap`, the victim flag,
the retry), `crime.{hpp,cpp}` (`witnessVictim`, set around the hook loop), `dialogue_runtime.{hpp,cpp}` (**the only edits there are
`barkLine`, its new trailing parameter and its three private helpers**), `world_view.{hpp,cpp}` and `skyrimse_game_pack.cpp` (the
`mBark` function gains a `bool speakerIsVictim` parameter; the witness hook uses `kindOfCrime`).
**`dialogue.{hpp,cpp}` (the files the bounty-dialogue work edits): `SkyrimConditionWorld::setCrimeVictim` and the `crimeVictim()`
override, 7 lines, additive; no function there is changed.** `apps/components_tests/CMakeLists.txt` (four lines),
`tools/neutral_session_check/{check.sh,luatest/scripts/n5/lip.lua}` (`LIP_PLACE` and `LIP_ACTIVATE`: the n5lip scenario placed the
player at 6 s and activated Hulda at 9 s of real time, before the dialogue data is read, so it never reached a conversation).
`game_packs/skyrimse/CLAUDE.md`. No existing test was edited.

## 4. Stage log

**Tests first.** 27 new tests (`SkyrimBarkSelection` 14, `SkyrimBarkBackoff` 8, `SkyrimBarkCrimeKinds` 3, `SkyrimBarkVictim` 2; a gated
real-data survey besides). Written against stubs (declarations only) and run red: the first 21 (selection, backoff, crime kinds)
**14 of 21 failed**; with the victim tests added **18 of 25 failed**; the two reaction-retry tests, written after the game run found
the problem, **2 of 2 failed** with the retry disabled and pass with it. 20 of 27 were red; the other 7 pin absence or controls (a
conversation's greeting is unchanged, a one-space text is still a line, no voice probe means a line is still chosen, the normal timers
of a said line, the gap setting exists). After the code: the new tests pass, and the Skyrim, bark, crime, dialogue, Papyrus,
extension, frozen and neutral `components-tests` **2,983 pass, 0 fail** (data-gated tests skip; the Slang, complement and property
groups were not run).

**In game** (Bannered Mare, the five masters and `Skyrim - Voices_en0.bsa`, `OPENEDEN_QUIET_WINDOW=1`, fixed frame time, master, voice
and music volume 0, Lua and physics on the main thread, `barks test start = 100`; traces compared with timestamps removed):

* **A sneaking player noticed** (`tools/sneak_check` through `sn.sh`, the real sneak control, the player placed 150 units in front of
  each actor): Saadia `BARK notice 881925 ... "Hello? Who's there?"`; **Brenuin `BARK notice 182543 ...dialoguegeneric__0006cb21_1.fuz
  "Is... is someone there?"` (silent before this task)**; frame `ev3\notice_brenuin.png` (his box and the Detected eye). Hulda, Mikael and Sinmir
  noticed (`hidden -> caution`) and say nothing: `BARK notice <ref> no line passes` once each, after six picks, no line spent (section 1
  says why). **Three runs: identical, 39 lines (md5 prefix `7fff23c17c`).**
* **A caught pickpocket** (`pp3.sh`, n9pick: the player sneaks, the real activate key opens Hulda's list, a real click on Gold):
  `PICKPOCKET 0001a66e caught trying for 0000000f x1 at 31%`, `CRIME pickpocket witnessed by hulda faction 000267ea bounty +25`, then
  `BARK pickpocket 108142 voice/skyrim.esm/femalenord/dialoguegeneric__00039c36_1.fuz "I guess I can look the other way, this time."`
  voiced and subtitled; frame `ev3\pickpocket_caught_hulda.png`. **Three runs: identical, 10 lines (md5 prefix `8a54ba907b`).** The line is a
  PICN line of `DialogueGeneric`; whether it is one of the victim-gated ones was not looked at.
* **The conversation's mouth after the 0235 refactor** (`lip3.sh`, `n5lip` with `LIP_PLACE=40 LIP_ACTIVATE=45`, a conversation with
  Hulda through `OPENEDEN_NEUTRAL_DIALOGUE_PRESS`): `NTALK Hulda: greeting`, `NLIP lip data of ...dialoguege_dialoguegeneric_000142c0_1.fuz: 32
  frames`, a second turn with `mg01_..._0008ec38_1.fuz: 213 frames`, 449 mouth uploads; the montage `ev3\conversation_mouth_montage.png` shows the
  mouth closed, parted and open across the turn. (One run; the weights follow the real playback position, so the NLIP lines are not
  expected to repeat.)
* **Morrowind unchanged:** `tools/worldobjects_check` 111 lines OK against the `integ2` build of `main`, and the fixed Balmora frame (`--start
  Balmora --random-seed 7`, frame 600) has the SHA-256 prefix `1c5d47ad0b5dc32d`, the value recorded in tasks 0235 and before, with the
  defaults and with `neutral world`, `neutral actors`, `neutral combat` and ragdolls on. The parent executable (`openmw_parent.exe`, of an
  older commit) no longer starts with these resources (exit 1), so it was not a comparison partner.

## 5. Not run

* The Linux gate and GCC `-Werror` (hand-checked: braced if/else bodies, no `nodiscard` results dropped, no shadowing), clang-format 14
  (formatted by hand to 120 columns), clang-tidy, the Lua tests, `openmw-tests`, the Slang, complement and property test groups, a fresh
  review, sanitizers.
* No mutation checks (the red runs above are the evidence).
* A PICC bark (nothing raises it), a bystander's pickpocket line (`IsActorAVictim == 0`), an assault witness that is the victim, a
  voice-typed notice for a voice type other than the five actors', the `mSpeak` refusal marking a line said (a known limit).
* Why Hulda, Mikael and Sinmir have no notice line was explained from the survey, not by evaluating each generic INFO's conditions.
* `n5ai` (the combat barks) against this build: the back-off only changes how often a failed cry asks; it was covered by unit tests only.

## 6. Decisions for toddhow

1. **`selectBarkInfo` repeats `selectInfo`'s loop** (about 60 lines) because the task said not to touch the shared selection, which
   conversations use. The alternative is one optional `std::function<bool(const DialogInfo&)> mSkip` in `SelectionOptions` and two
   lines in `selectInfo` (null for conversations: nothing else changes), and `barkLine` would call `pickFromTopics` again. That removes
   the copy and any chance that the two drift apart when the bounty-dialogue work changes `selectInfo`. Recommended once the
   other work has landed; say if you want it.
2. PICN is taken as "the victim noticed and did not fight" and PICC as "and fought" (RECALLED-VERIFY, section 1); a pickpocket the
   victim answers with a fight (nothing does today) should raise PICC.
3. `IsActorAVictim` is now answered for a bark of the witness that is the victim. Conversations and every other selection still
   see 0 (the default), as before.
4. A line the core refuses to play is still marked said (the choice and the marking happen before the play call). Fix, if wanted: a
   commit step in `DialogueRuntime` that `mSpeak` success calls.
5. `Settings::mNoLineGap` is 5 s (OURS).
6. For Hulda, Mikael and Sinmir nothing is said when they notice a sneaking player: the data has no line for them that passes in
   this cell. Skyrim probably is as quiet for generic voice types.
