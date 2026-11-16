# Task: Skyrim scenes and more story events (milestone M-S6)

Status: IN PROGRESS. Windows only, local commits on branch `scenes` from `5b886a0bf8`, not pushed. toddhow chose "Scenes and more
story events" for M-S6 on 2026-11-15. Everything is behind `[extension.skyrimse] papyrus` (off by default).
Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.20 (dialogue and quests); decision 0014; tasks 0187 (the base
of this one), 0139, 0141, 0160, 0176, 0181.
Goal: a quest scene plays: its actors say their lines in order (voice and subtitle), its phases advance, its fragments run; the
story manager starts scenes from the events the data uses most.
Inherited code touched: none. Small additive edits: `vmad.*` (a scene fragment parser), `quest.*` (ALFD decode),
`quest_alias.*` (event aliases), `dialogue_runtime.*` (a line entry that does not touch an open conversation),
`dialogue.cpp` (run-on 7), `package_brain.*` (one override function, see 3), the SDK `actor_rules.hpp`-free speech interface.
Sources used for meanings (decision 0014's amendment): xEdit's `wbSCEN` definitions (named from memory, RECALLED-VERIFY; the
Creation Kit wiki could not be fetched) and the installed data (aggregates only). CommonLibSSE was not consulted. No Havok.

## 1. Survey (read at `5b886a0bf8`; numbers are from a Python walker of the five masters, not committed)

- **Nothing decoded SCEN before this task**: `pack_core.cpp` only counts the type; `Scene` was not a Papyrus class.
- **SCEN layout, settled by data (2,141 records, 10,753 phases, 16,435 actions, 717 VMAD):** see `scene.hpp`. Phase conditions sit
  between two NEXT markers: the list before the first NEXT is the phase's START conditions (1,104), the list between the NEXT
  markers its COMPLETION conditions (2,175); the CTDA after VNAM are the scene's own (59). Action types 0 dialogue (11,991),
  1 package (3,549), 2 timer (895; two SNAM, the second a float of seconds). Dialogue DATA is the topic (DIAL) in 8,870 and 0 in
  3,121. The scene VMAD fragment section: u8 2, u8 flags (bit 0 begin, bit 1 end), file name, per set bit a script and function,
  then u16 phase fragments of 5 header bytes (u8 flags 1 start / 2 completion, u8 phase, i16, u8), u8, script, function: all 717
  end exactly.
- **Scene flags (FNAM):** 4 (673), 7 (619), 0 (453), 3 (205), 5 (58), 1 (50): bit 0 begin on quest start, 1 stop quest on end,
  2 show all text, 3 repeat conditions, 4 interruptible (xEdit names, RECALLED-VERIFY).
- **Functions in scene conditions** (3,338): fn_550 IsSceneActionComplete 920, GetStageDone 569, GetDistance 377, GetVMQuestVariable
  323, GetStage 310, IsInCombat 135, GetDead 83. fn_550 needs the runner's state: added to the scene evaluator's extra table.
- **The Mare scenes.** `WhiterunBanneredMareScene1` (0x17F33, quest 0x17F32, Hulda and Mikael, 6 phases, 6 dialogue actions, no
  conditions, flags 7) and Scene3 (Saadia and Hulda). They are started by the story manager: SMQN 0x18955 under ADIA (not CLOC).
  The `DialogueWhiterunArgueScene` of task 0187 waits in phase 0 for `::SceneTrigger_var == 1` (a script variable set by a trigger
  the player must enter), so it is NOT used.
- **Story events by quests under them (all five masters):** ADIA 919 (902 distinct), SCPT 361, CLOC 258, KILL 15, CRFT 11, LEVL 9,
  CAST 6, REMP/ASSU/AIPL 5 each. ADIA is what the data uses most (almost all of it is the ambient two-actor dialogue scenes).
  ADIA's node conditions run on the event's references (run-on 7, reference field 'R1'), and the quests' event aliases (ALFE/ALFD
  'R1', 'R2') carry GetIsID conditions that name the pair.
- **LEVL:** the stand-in player never levels (task 0187), so LEVL is not raised.

## 2. Design

- `scene.*` (pure): `parseSceneRecords`, `SceneIndex` (merged over the load order, by quest); `vmad::parseScene` (the scene
  fragment section). `scene_runner.*` (pure): `SceneRunner` over a `SceneEnvironment` (aliases, conditions, lines, speech,
  packages, fragments); every rule is a named option, RECALLED-VERIFY, listed in the header. Lines last an ESTIMATED time (the
  pack decodes no audio): 15 characters a second, at least 1.5 s, plus 0.4 s, so the run is deterministic (decision 1).
- Speech: a NEW SDK interface `openeden/game/scene_speech.hpp` (`SceneSpeechSource`, found by `dynamic_cast` on the registered
  `ActorRules`), not a change to `ActorRequestWriter`: the core takes the pending lines once a frame and plays them (voice
  through the sound manager, text as a message box). Pack: `SkyrimCellSource` implements it with a bounded queue.
- Package actions use one new function of the package brain, `forcePackage(reference, package)` (null clears), honoured by one
  line in `evaluate`; the actors of a scene are held still with the brain's existing `onConversation`.
- Host: class `Scene` (Start, ForceStart, Stop, IsPlaying), scene fragments run through the host like quest fragments (the SCEN
  is the attach form), a quest start tells the runner (`questStarted`), a quest stop ends its scenes. fn_550
  IsSceneActionComplete is an extra condition function (like fn_576).
- Story events: ADIA raised by the pack on a seeded timer for pairs of nearby actors of the player's cell (R1, R2), run-on 7
  conditions ('R1', 'R2'), event aliases (ALFE/ALFD) filled and checked so a quest whose pair does not match does not start.
