# Task 0155: Skyrim speech checks and service stubs (milestone M-S4)

Status: built under the coordinator's brief of 2026-11-12 (toddhow's blanket approval of the Skyrim plan). Full loop (D13)
in form only: the same hand wrote tests and code, no fresh reviewer.

Design doc section: `docs/design/skyrim-se-implementation-plan.md` M-S4 ("speech checks; training/trade hooks stubbed");
tasks 0137 (dialogue records), 0139 (quests), 0141 (the neutral conversation), 0143 (voice), 0122 (conditions engine),
0154 (actor numbers).

Goal: a persuade, intimidate or bribe line passes or fails by the numbers of a stand-in player and the speaker, as the
data's conditions say; a line that opens a barter or training menu (or another service) is recognised, logged as "service
not implemented" and ends the conversation cleanly, instead of doing nothing.

## 1. What the data says (aggregates, the five official masters; an independent Python walker, not the pack's code)

- 41,188 INFO and 19,536 DIAL records. INFO `DNAM` (5,049 of them) is a **form id** (4 bytes: "response data"), not a
  speech-challenge enum; `CNAM` (favor level) is 0 in every INFO looked at; `ENAM` is the response flags. There is **no
  speech-challenge subrecord**: a persuade, intimidate or bribe line is an ordinary INFO gated by conditions.
- 245 INFOs carry a speech gate: 123 compare **fn_14 GetActorValue, actor value 17 (Speechcraft)**, 65 use **fn_655
  GetIntimidateSuccess**, 57 use **fn_654 GetBribeSuccess**. Of the 123 speech comparisons, 101 are `>=` against a global
  (the use-global flag; the global holds the difficulty), run on the TARGET (run-on 1, the player), 6 on a reference, 4
  on the speaker; `GetEquipped` (fn_182, the Amulet of Articulation) is or'ed in in 103 of the 245 INFOs.
- Functions beside the gates in those 245 INFOs (INFOs holding them): GetIsID 161, GetActorValue 130, GetEquipped 103,
  GetIsAliasRef 71, GetStage 67, GetInFaction 36, GetVMQuestVariable 25, GetStageDone 14, GetQuestRunning 14, then a
  tail (IsBribedbyPlayer 11, IsTrespassing 8, GetCrimeGoldViolent 8, GetGlobalValue 7 ...).
- fn_14 is used with 12 actor value indexes the survey saw (top: 95 x154, 17 x123, 74 x117, 73 x113, 68 x65); the
  skill index range 6 to 23 and 24 to 26 for the vitals are RECALLED-VERIFY (17 = Speechcraft is the one the data
  supports: it is compared with difficulty globals).
- Services: 7,972 INFOs have a VMAD, 6,207 name a fragment script `TIF__<8 hex>`. The pack reads that name
  (`findFragmentScriptName`) and the script's compiled form (`Skyrim - Misc.bsa`, `scripts/tif__*.pex`; all 5,937
  distinct scripts of the masters read, 0 missing, 0 failed). The pex string tables name what a fragment calls:
  `ShowBarterMenu` 44 scripts, `ShowTrainingMenu` 48, `ShowGiftMenu` 13, `RentRoomScript` 4 (the inn rooms), plus
  `OpenInventory` 22 and `SetRestrained` 5 (not classified). 110 INFOs are classified as a service.
- The room-renting topics (`RentRoomTopic` and kin): INFOs gated by `GetItemCount(Gold001)` run on the player, `>=` the
  global `RoomCost`, and `GetActorValue 76` on the speaker (0 for "not renting yet"); the INFO that passes has the
  `RentRoomScript` fragment, the others are refusal lines without a script.

## 2. Built

- `game_packs/skyrimse` `speech.{hpp,cpp}`: `PlayerStandIn` (level, 18 skills, gold, vitals), actor value to skill slot,
  `intimidateSucceeds`, `bribeSucceeds`, `classifyService`.
- `SkyrimConditionWorld` (dialogue.hpp): `setPlayer`; answers fn_14, fn_277, fn_80 (the player and the speaker),
  fn_48 (the player's gold), fn_655, fn_654; `answers()` counts them as answered. Without `setPlayer` nothing changes.
- `DialogueRuntime`: a stand-in player by default (`setPlayer`), `setScriptProbe` to name services, `Stats::mServices`.
  A service turn sets `DialogueTurn::service` and `endsConversation`.
- `DialogInfo::mFragmentScript` (from the VMAD). SDK `dialogue_source.hpp` 0.3: `DialogueTurn::service` (trailing field,
  additive; SDK stays 4.0).
- Pack settings (declared in `extension.json`): `player speech` (default 15), `player gold` (100), with the existing
  `player level` (1).
- Also answered for the stand-in player: fn_47 GetItemCount (his gold for Gold001, nothing else) and fn_182 GetEquipped
  (he wears nothing: the Amulet of Articulation test is false). The service names are `barter`, `gift`, `training`,
  `rent a room`.
- Engine: `neutralServiceNote` and one `Log(Debug::Warning)` line in `NeutralDialogueSession::update`.

## 3. Decisions for toddhow

1. **The stand-in rules are ours, not Skyrim's.** Player: level 1, every skill 15, 100 gold, 100 health/magicka/stamina.
   Intimidate succeeds when the player level >= the speaker's level; a bribe succeeds with 20 gold per speaker level.
   Skyrim computes both from game settings and perk entry points that the pack does not read.
2. A service line ends the conversation after showing its response text (one Goodbye button), as the menu would have
   replaced the dialogue. Alternative: return to the topic list.
3. Persuade has no condition function in the data (the check is the Speechcraft comparison): nothing to stand in for.

## Stage log

2026-11-12, Windows only, local commit on branch `skyspeech` (from `main` at `4622d46414`), not pushed. The same hand wrote
tests and code; no fresh reviewer; an advisor was NOT consulted. Own build tree `I:\Projects\OpenEden-work\skyspeech`
(RelWithDebInfo without debug info, because the disk had 23 GB free; it fetched its dependencies by itself at configure).

- **Tests.** `testskyrimsespeech.cpp` (21 tests: actor value slots, stand-in values, the two rules, the classifier, the VMAD
  script name, persuade at 24/25/90 speech, the default player, intimidate by level, bribe by gold, a level line, a gold line
  and the rent service, the Amulet condition answered, barter and ordinary scripted lines, no probe, one probe per script)
  and `testneutralservice.cpp` (4 tests, openmw-tests). **Not run red against stubs first**: the code was written beside the
  tests. Instead three single-line mutations were run and each failed named tests: `>=` to `>` in `intimidateSucceeds` (2
  tests), `mWorld.setPlayer` commented out in the runtime constructor (the first test set did NOT catch it; the test
  `theDefaultStandInPlayerIsWiredIntoTheConditionsWithoutASetter` was added and then does), and the rest are guarded by the
  no-probe test. All restored. `components-tests` (without Slang, complement, property) 9,953 pass, `Skyrim*` 855 pass,
  `openmw-tests` 2,394 pass. No existing test was edited.
- **Real data** (`SkyrimSpeechRealData`, gated; aggregates and official identifiers only). Gates: 242 INFOs (the Python walker
  said 245; its filter also counted INFOs in groups the pack's walk assigns differently) are gated by Speechcraft, fn_654 or
  fn_655; with the stand-in 48 are fully answered, 393 conditions are left, led by fn_182 x102 (answered after the last edit),
  fn_58 x82 and fn_59, fn_56, fn_566 x76, fn_629 x35 (quest state: unanswered in that test only because it sets no quests; the
  runtime does), fn_402, fn_375, fn_145, fn_376, fn_459 (crime and bribe state: NOT answered). Services: see section 1.
- **In game** (`tools/neutral_session_check/check.sh ... WhiterunBanneredMare ... n5talk`, a wrapper `run_talk.sh` in the work
  folder, Hulda, buttons pressed with `OPENEDEN_NEUTRAL_DIALOGUE_PRESS`; frames viewed in `I:\Projects\OpenEden-work\skyspeech\{t4,d1,d3,d4}`):
  - Rent a room (button 3): the response box "Sure thing. It's yours for a day." with a single Goodbye button, and the log
    line `NTALK service not implemented: rent a room with Hulda`; pressing Goodbye closes ("NTALK closed after 2 turns").
    With `[extension.skyrimse] player gold = 0` the same press gives the refusal line, no service line (5 buttons).
  - A generated plugin (`speechdemo.esp`, written by the test `SkyrimSpeechDemo.writesThePluginWhenAsked`) adds three
    topics gated exactly like the data's: with the defaults (speech 15, level 1, gold 100) Persuade fails, Intimidate fails
    ("You do not scare me."), Bribe passes ("Fine, take it."); with speech 40, level 30, gold 0 Persuade passes ("You make a
    fair point."), Intimidate passes ("Please, no violence!"), Bribe fails. The REAL Skyrim persuade topic "(Persuade)"
    (a shakedown line) is offered to Hulda at speech 40 and not at 15 (8 topics against 7 with the demo plugin) and its
    response plays; at level 30 a second real topic appears.
- **Morrowind unchanged.** The fixed Balmora frame (`OPENEDEN_FIXED_FRAME_TIME=0.0166667`, `--random-seed 7`, frame 600) has
  SHA-256 `1c5d47ad0b5dc32d5c1e...`; `tools/worldobjects_check` 111 lines OK against a reference executable of ANOTHER agent's
  tree (`skyactivate`, no parent build of my own exists). Nothing under `apps/openmw` but the dialogue log line changed.
- **Not run.** The Linux gate and GCC `-Werror`, clang-format 14, clang-tidy, the Lua tests, sanitizers, a fresh review, a
  failed Persuade line (the data hides the topic instead; no failure INFO exists for the real lines I looked at), the
  intimidate and bribe lines of the REAL data (not reachable from the Mare; only the demo plugin), a barter or training line in
  game (only the rent line is reachable from Hulda; the barter classification is shown by the gated scripts count and unit
  tests), `check_file_names` lists two files from main (not mine).
