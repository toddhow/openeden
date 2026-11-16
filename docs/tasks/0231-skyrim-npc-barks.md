# Task 0231: Skyrim NPCs speak unprompted (barks)

Design doc section: `docs/design/game-packs.md` (the Skyrim SE pack), milestone M-S7. Builds on tasks 0137 and 0141 (dialogue
selection), 0143 (voice), 0194 (scene speech), 0190 and 0197 (combat AI) and 0204 (crime).

Goal: Skyrim NPCs say short lines without being talked to: a hello as the player comes near, idle chatter, and in a fight
a cry when it starts, blow cries, taunts, a hurt line, a cry when fleeing, a death cry and a line when the fight is over.

Status: built, Windows only, branch `barks`. Off by default: `[extension.skyrimse] barks = true` (declared in
`extension.json`, also in the test profile).

## 1. Survey (five official masters, `SkyrimBarksRealData`, aggregates)

The occasion of a bark is the SNAM subtype of its DIAL (four characters), the category is DATA byte 1. Topics and INFOs:

| subtype (category) | topics | infos | use here |
| --- | --- | --- | --- |
| HELO (7 misc) | 405 | 6,535 | Hello (also the conversation greeting, see decisions) |
| IDLE (7) | 47 | 713 | Idle chatter |
| NOTC (5 detection) | 51 | 246 | the fight starts |
| ALTC (5) | 43 | 142 | the fight starts because an ally was attacked |
| ATCK (3 combat) | 61 | 449 | blow cries |
| POAT (3) | 29 | 69 | power attack cries (not used) |
| TAUT (3) | 66 | 754 | taunts |
| HIT_ (3) | 89 | 311 | hurt |
| FLEE (3) | 20 | 59 | flees |
| DETH (3) | 83 | 140 | death |
| COTN (5) | 31 | 119 | the fight is over |
| LOTN / LOTC / LOIL (5) | 24 / 26 / 22 | 51 / 103 / 72 | lost track (LOTN used) |
| ALIL, NOTA, ALTN (5) | 23, 31, 24 | 88, 87, 61 | alert states (not used) |
| BLOC, BASH, BLED, AVTH, ACYI, YIEL (3) | 15, 21, 25, 3, 8, 4 | 45, 46, 76, 3, 16, 3 | not used |
| ASSA, STEA, TRES, MURD, PICT, PICN, PICC (3) | 7, 7, 20, 8, 3, 7, 6 | 24, 30, 91, 29, 6, 24, 20 | crime reactions: the API exists (Assault, Steal, Trespass), nothing calls it |
| NOTI, OBCO, ACAC, ... (7) | 14, 8, 8 | 75, 32, 28 | not used |

The raw output is in `I:\Projects\OpenEden-work\ragdoll5\barks_survey.txt` (87 lines).

Timing and distance settings of Skyrim.esm (numeric GMSTs whose name has Greeting, Hello, Idle, Bark, Dialogue, Chatter,
Speech, Voice, Subtitle or Shout in it); the ones used are in bold:

- **fAIGreetingTimer 5**, fAIExclusiveGreetingTimer 2.5, fAIForceGreetingTimer 3, fAIInDialogueModeWithPlayerDistance 500,
  fAIInDialogueModewithPlayerTimer 60
- **fIdleChatterCommentTimer 10, fIdleChatterCommentTimerMax 20**
- **fCombatDialogueAttackMinElapsedTime 5, ...MaxElapsedTime 10**
- **fCombatDialogueTauntMinElapsedTime 10, ...MaxElapsedTime 30**
- fCombatDetectionDialogueIdleMinElapsedTime 10, ...MaxElapsedTime 25 (the alert idle; not used)
- **fGameplayVoiceFilePadding 0.5**
- fSubtitleSpeechDelay 8, fDialogSpeechDelaySeconds 2, fSpeechDelay 0.1, fAIDefaultSpeechMult 0.15 (not used)

**There is no GMST for a greeting distance** (`fAIMinGreetingDistance` does not exist in Skyrim.esm). The distances are OURS.
How the game uses each timer is RECALLED-VERIFY: the names say what they are for, the data gives only the numbers.

## 2. Design

- `barks.*` (pure, in the pack, `game_skyrimse_world` too): `BarkSystem` decides WHEN. Inputs once an update: the roster
  (reference, position, dead), the player's position, the combat's moments (queued by `onEvent`). Outputs through hooks: choose a
  line, speak it, trace it. No clock (the callers' seconds add up), no unseeded random number (splitmix64 over seed, actor,
  per-actor counter, kind), actors in reference order.
- Rules (the numbers of `barks::Settings`; the first group is the game's, the rest ours):
  - Hello: the edge from beyond to within 400 units (and 200 in height) of the player. An actor already near when first seen
    says nothing. One hello at a time, the nearest first, `fAIGreetingTimer` between two hellos, the same actor again after
    300 s. A hello that finds no line or no voice is tried again after 1 s while the actor is still near.
  - Idle: `fIdleChatterCommentTimer` to `...Max` after first seen, then again, within 800 units of the player; one calm bark
    at a time (the line's assumed length: 1.5 s or 0.07 s a character, plus `fGameplayVoiceFilePadding`).
  - Fight: an engagement cries once (NOTC, or ALTC when an ally's fight woke the actor); an engaged actor within 2,500 units
    cries ATCK every `fCombatDialogueAttack*` seconds and TAUT every `fCombatDialogueTaunt*`; HIT_ when hurt and alive (3 s
    between), FLEE once per flight, DETH at death (it may interrupt), LOTN when it gave up the chase ("no way to the target",
    "flee over or target gone"), COTN when the target died or the actor left the player. After any combat bark no other starts
    for 0.75 s.
  - An actor never starts a line while its last one still runs; a line with no voice file is not said (counted).
- WHICH line: `DialogueRuntime::barkLine(subtype, speaker, pick)`: the topics of the subtype by priority, the first INFO that
  passes (conditions, quest running, say-once), a Random one chosen by `pick`, with the INFO's script fragments as for any
  shown line. Reached through the new `PapyrusAccess::mBark` (a copy of `mLine`'s plumbing).
- HOW it is played: the scene speech path of task 0194 (`SkyrimCellSource::sceneSpeak`, the engine's `NeutralSceneSpeech`):
  voice through `SoundManager::say` from the actor, text as a message box "Name: text". No conversation box. A text of one
  space (Skyrim's unsubtitled ambient lines) shows no box. `barks subtitles` (default true) turns the box off.
- The fight's moments: `SkyrimCombat::setBarkSink` (hook calls in `aiBegin`, `aiRelease`, the flee case of `runAi`, and
  `AiDamageWriter::reportDamage` / `reportDeath`). No virtual was added to `CombatRequestWriter`.
- The roster is built once an update in `SkyrimCellSource::update` after the package brain (a vector per update, only with the
  setting on).
- Until the dialogue data is read (about 25 s after start) nothing runs and the bark clock stands still (`Hooks::mReady`):
  events before that are dropped. Settings from the GMSTs are applied by the first `choose` after the combat records are read.
- Test aid `[extension.skyrimse] barks test start = S`: nothing runs before S seconds of the pack's own update time, and the
  bark clock starts at `sum of dt - S`, so a run's timeline does not depend on the worker's speed.

## 3. Edits outside the new files

New files: `game_packs/skyrimse/{include/game_skyrimse/barks.hpp,src/barks.cpp}`, `apps/components_tests/skyrimse/
testskyrimsebarks.cpp`, `testskyrimsebarkline.cpp`, `testskyrimsebarksreal.cpp`, `tools/neutral_session_check/luatest/n5bark.omwscripts`
and `scripts/n5/barkglobal.lua`, this document.

Edited (all additive): `game_packs/skyrimse/CMakeLists.txt` (barks.cpp in both lists, the header), `extension.json` (barks, barks
subtitles, barks test start), `combat.hpp` (include, `BarkSink` and `barkNoteDamage`/`barkNoteDeath`, `AiActor::mFleeBarked`, the two
calls in `AiDamageWriter`), `combat_ai_run.cpp` (three hook sites), `dialogue_runtime.{hpp,cpp}` (new `barkLine`), `world_view.{hpp,cpp}`
(`PapyrusAccess::mBark` and its lambda, `gameSettings()`), `neutral_world.{hpp,cpp}` (`setBarks`, `updateBarks`, one call in
`update`), `skyrimse_game_pack.cpp` (`startBarks`, four members), `apps/components_tests/CMakeLists.txt`, `tools/neutral_session_check/
check.sh` (the `n5bark` scenario), `tools/testprofile/{profile.cfg,README.md}`, `game_packs/skyrimse/CLAUDE.md`, the changelog row in
the root `CLAUDE.md`. No existing test was edited. No engine, SDK or contract file changed.

## 4. Stage log

Tests first. `testskyrimsebarks.cpp` (29 tests, the pure rules over a fake world) was written against a stub `barks.cpp`: **22 of 26
failed red, the 4 that passed test absence** (nothing without a player, no hello for an actor already near, a dead actor, an engaged
actor). Two tests added later were red first (the ready gate and the clock-free trace: 2 of 2 failed; the start time: 1 of 1).
`testskyrimsebarkline.cpp` (5 tests, `DialogueRuntime::barkLine` over generated plugins) against a stub: **5 of 5 red**.
All pass: `SkyrimBark*`, `SkyrimDialogue*`, `*Combat*`, `*Extension*`, `*Frozen*`: 691 tests.

In game (Bannered Mare, five masters, `fallback-archive=Skyrim - Voices_en0.bsa`, `OPENEDEN_QUIET_WINDOW=1`, fixed frame time, all
volumes 0, Lua and physics on the main thread):

- **Hello** (`n5bark`: the player is placed 900 units from an actor, moved to 250 units over 1 s, held; `barks test start = 70`,
  scenario start 80 s): Hulda "Hmm?", Mikael "What can I do for you?", Saadia "Need something?" (Sinmir, the fourth actor visited,
  said none: its HELO had no passing INFO or voice file; not investigated). Frame `evidence\hello_hulda.png` (Hulda in view, box
  "Hulda: Hmm?"). Idle chatter between (3 lines, all of one-space text: no box). **Three runs: identical BARK traces** (md5
  `9c7e28036a5f`, 6 lines, 6 of 6 voice files decoded by the sound manager, "Voice file ...: decoded N s of audio with wmav2").
  A first run after a cold start (`g1`) had 3 idle lines against 5 in the next two: its dialogue data was read after the start
  time. Until the data is read the bark clock stands still, so without `barks test start` a timeline starts at readiness.
- **Combat** (`n5ai`, Hulda hostile with an iron sword, the player 110 units away swinging every 2 s; `combat ai`, `barks test
  start = 90`, scenario start 100 s): engage "By Ysmir, you won't leave here alive!", attack "Aggghh!", hit "Argh!", "Agh!",
  "Weergh!", death "Gah...", lost "No sign of 'em." (before the player is placed she has no path to him and gives up); taunts
  appear in the longer fights of earlier runs. Frame `evidence\combat_attack_hulda.png` (box "Hulda: Aggghh!"). **Three runs:
  identical BARK traces** (md5 `afd3de7b72`, 16 lines, 15 voice lines, 14 distinct files decoded); the BARK and NCOMBAT
  damage/death lines together hash the same (`7996571ebbdd`) in these runs and in the three runs made before the line-length
  refactor of `barks.cpp` (`f1` to `f3`).
  The fight itself is deterministic with the barks off or on (the `NCOMBAT damage` lines of two runs with them off equal those
  with them on). Fleeing was seen in an earlier run (`c3b`, "Enough!") but not in these three; calm (COTN) and the alerted
  engagement (ALTC) were not seen in game, only in the unit tests.
- Earlier runs with Lua and physics on worker threads or without `barks test start` differed run to run: the bark clock began
  at the dialogue worker's readiness, which moves relative to the fight by a few frames, and gaps (hit, combat, busy) then
  suppressed different hits. That is why the test aid exists.
- Morrowind: `tools/worldobjects_check` 110 lines OK and `morrowind_check.sh` frames identical to the reference executable
  (`integ2` build of `main`; not a build of the exact parent). No engine file changed.
- Evidence kept: `I:\Projects\OpenEden-work
agdoll5\evidence` (two frames, a hello and a combat log, the BARK lines of both),
  `barks_survey.txt`.

## 5. Not built, not run

- Reactions to a drawn weapon, to the player sneaking and to crimes: the SDK tells the pack nothing about a drawn weapon or
  sneaking, and `CrimeSystem` has no witness hook; the kinds Assault (ASSA), Steal (STEA) and Trespass (TRES) exist in
  `barks::Kind` and `BarkSystem::onEvent` speaks them (a unit test), but nothing calls it.
- Lip sync: the scene speech path (task 0194) has none; the face morphs of task 0146 are driven only by the conversation box.
- Power attack (POAT), block, bash and bleedout cries, the alert subtypes, shouts.
- The Linux gate, GCC `-Werror`, clang-format 14 and clang-tidy (formatted by hand), the Lua tests, a fresh review, the real audio
  device (nobody has heard it), exteriors (Whiterun's street), a female or creature voice type with no voice folder, Papyrus fragments
  of a bark INFO in game, a save and load.

## 6. Decisions for toddhow

1. **Subtitles.** The engine has `[GUI] subtitles` (default false); the scene speech sink always shows a box. The barks follow their
   own pack setting `barks subtitles` (default true). Making the sink honour `[GUI] subtitles` would change task 0194's scenes too.
2. **Hello and the conversation greeting share the subtype HELO.** A bark can say a line that was written as the opening of a
   conversation. Skyrim does the same (the data has no other hello subtype), but the lines that need `GetIsInDialogueWithPlayer`
   rely on the condition being answered false.
3. **The distances are ours** (400, 800, 2,500): no setting gives them.
4. **A bark with no voice file is not said.** (Most ambient lines have a text of one space and no subtitle.)
5. Crime reactions need a hook in the crime system (a witness callback) and a drawn-weapon/sneak state from the core.
