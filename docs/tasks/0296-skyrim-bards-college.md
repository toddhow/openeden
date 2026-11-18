# Task: Skyrim Bards College, start of the questline (task 0296)

Status: ordered by the coordinator on 2026-11-17. Windows only. Local commits on the worktree branch from `0f4cca6e35`, not pushed.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` (M-S7); decisions 0013, 0016, 0017; tasks 0137, 0187, 0194, 0211, 0217
(dialogue, quests, story manager), 0264 and 0277 (Companions), 0271, 0275, 0287 (Thieves Guild), 0261, 0269 (houses), 0270, 0276 (marriage).
Goal: from a new game inside the Bards College in Solitude, Viarmo's dialogue starts "Tending the Flames" (MS05) through the game's own quests,
dialogue and Papyrus, the journal shows it, and the first leg (find King Olaf's Verse in Dead Men's Respite) has its objective and map marker.

## 1. Survey (Skyrim.esm; scripts decompiled with houseCARL from `Skyrim - Misc.bsa`; INFO ids from an independent Python walker)

Quests (all five masters agree; USSEP only overrides MS05 and MS05KingOlafsFestival):

- `MS05Start` (QUST 0x42868, StartGameEnabled, RunOnce, priority 60, alias 0 `Viarmo` = unique actor NPC_ 0x132AD): objective 10
  "Investigate the Bards College" (displayed by `Fragment_0` at stage 10; stage 0's fragment is empty). Its only topic is
  `MS05GenericJoinTopic` (DIAL 0x534E4, "What do you do around here?"); INFOs 0x534F2, 0x534F3 and 0xBB964 (15, 10 and 3 conditions) each run a
  fragment `GetOwningQuest().SetStage(10)`; the other three INFOs have no fragment.
- `MS05` "Tending the Flames" (QUST 0x53511, SideQuest, StartGameEnabled, RunOnce, script `ms05questscript` with `Patronage`, `Verse2Choice`,
  `Verse4Choice`; 20 stages: 0, 10, 75, 90, 100, 125, 135, 145, 150, 175, 200, 225, 250, 253, 254, 255, 260, 265, 290, 300; 13 objectives, 17
  fragments). Aliases: Viarmo, Giraud, Jorn, Elisif, Tullius, Una, Falk, Erikur, Odar, Sybelle, Meleran, Bolgeir, Heimvar, Erdi, the Book of King Olaf,
  the Effigy, the festival and court markers and the Dead Men's Respite map marker.
- The first leg's dialogue (DIAL, quest MS05): `MS05ViarmoApplicationTopic` (0x534E2, "I'm looking to apply to the college."; INFO 0x53504),
  then `MS05ViarmoApplicationTopic2` (0x6035B, "And that's where I come in?"; INFOs 0x6035D and 0x57A4D). **INFO 0x6035D: `SetStage(75)`**;
  INFO 0x57A4D (the version after the player has the verse) `SetStage(90)`.
  `Fragment_4` (stage 75): `MS05Rumor.SetStage(20)`, `MS05Start.setObjectiveCompleted(10)`, objective 50 completed, objective 75 "Find King Olaf's
  Verse" displayed, `Alias_MS05DeadMansRespiteMapmarker.GetRef().AddToMap(false)`. Stage 75 has one log entry (the journal text).
- `MS05Rumor` (QUST 0xD789C, Rumors): `Fragment_0` displays objective 10, `Fragment_1` completes it; the topic `MS05RumorBranchTopic` INFO 0xD788C
  `Setstage(10)`; MS05's stage 75 sets it to stage 20.
- Later legs (not in this slice): `MS05_dunDeadMensRespiteQST` (QUST 0x16C8D, the dungeon with the ghost bards), `MS05KingOlafsFestival`, the
  poem choices (`TIF__000534E5` ... set `Verse2Choice`/`Verse4Choice`/`Patronage` on `ms05questscript`), the court scene `MS05CourtScene`
  (SF_MS05CourtScene_0005351e), stage 300's reward and relationship ranks.
- Cell: `SolitudeBardsCollege` (CELL 0x16A0C, the college interior; `SBardsCollege` is a package, not a cell). Viarmo is NPC_ 0x132AD (`skyrimse:b78509`).

## 2. What the first runs showed (check: `tools/skybards_check/check.sh`, five masters, `quest alias packages` and `map` on)

The first leg ran with NO new code in the dialogue and quest machinery: Viarmo (`NTALK Viarmo`) greets with "I'm all ears.", offers
"I'm looking to apply to the college." (voiced: `ms05_ms05viarmoapplication_00053504_*.fuz`), then "What do you need me to do?" and, after the
task text, "And that's where I come in?" (INFO 0x6035D). **`PAPYRUS dialogue line 0006035D runs TIF__0006035D.Fragment_0`, then after Goodbye
`quest 00053511 set to stage 75`, `Fragment_4`, `MS05Rumor` stage 20; the journal lists "Tending the Flames", the log text ("I have applied at the
Bards College ...") and the objective `[ ] Find King Olaf's Verse`** (frame viewed).

**One gap was found and fixed: `ObjectReference.AddToMap`.** Stage 75's fragment calls it on the Dead Men's Respite map marker alias; the host
logged "declared and not registered" and returned. Now: `PapyrusWorldHooks::addToMap(reference)` (trailing virtual, default false),
`PackWorldEnv::addToMap`, the native, `Discovery::add(index, marker)` (a known marker, once, sorted, revision bump) and `MapService::addMarker`.
Needs `[extension.skyrimse] map` (else the hook answers false and nothing changes). In game: `MAP discovered by a script: 90670` after the
fragment; the world map (M) draws.

## 3. Ranked gaps (not built)

1. **MS05Start's stage 10** ("Investigate the Bards College", INFOs 0x534F2/0x534F3/0xBB964 of `MS05GenericJoinTopic`) is not offered by Viarmo
   (his topics are the apply/dragons/war lines) or by Giraud (his topics are another quest's); it is offered to other people (not found which, the
   conditions are 15 and 10 INFO conditions). So the first objective never displays and `MS05Start.setObjectiveCompleted(10)` completes nothing.
   Not harmful: the journal entry comes from MS05 stage 75.
2. The apply topic (`MS05ViarmoApplicationTopic`) is still offered after stage 75 (its INFO conditions were not read to see whether the game
   hides it); only the repeat of the task text.
3. **The poem and the later legs**: Dead Men's Respite itself (`MS05_dunDeadMensRespiteQST` 0x16C8D with ghost bard scenes
   `SF_MS05_BardGhost*`, the Book of King Olaf as a quest item (alias `MS05BookOfKingOlaf`), the dungeon's doors, traps and draugr), giving the
   verse (stage 90 INFO 0x57A4D), the poem choices (`ms05questscript`, `TIF__000534E5` ... on Verse2Choice, Verse4Choice, Patronage, Persuade
   checks), the court scene (`MS05CourtScene`), the coat theft from Tullius, the festival and stage 300's reward. Their dialogue and fragments
   run on the same machinery; the dungeon needs the player to walk there (no fast travel from an interior) and fight.
4. The two Papyrus errors `CompanionsBladeFragmentTracking.Setup@27` (none array) at start-up are the Companions quest's, not this one's.
5. Dead Men's Respite on the map: the marker is discovered (log line) but its dot was not told apart in the world map frame (many markers are
   `Visible` by flag); no "Dead Men's Respite" label was read.

## 4. Stage log

- **Tests (new, 6)**: `apps/components_tests/skyrimse/testskyrimsebards.cpp`: `SkyrimBardsMap` 3 (Discovery::add), `SkyrimBardsHooks` 1,
  `SkyrimBardsPapyrus` 2. Written before the code and run against stubs (`Discovery::add` returning false, `addToMap` returning false, the native
  missing): **5 of 6 red** (the sixth pins that a script with no hooks is harmless). All 6 green after.
- **In game**: `tools/skybards_check/check.sh` with `gen_input.py` (real clicks on the conversation box, M, J), frames in
  `I:\Projects\OpenEden-work\combat5/bards\keep`: the apply line, the task text, "And that's where I come in?", the world map after stage 75 and the journal.
- Edits outside the new files: `papyrus_host.{hpp,cpp}` (hook + native), `papyrus_world.hpp`, `map.{hpp,cpp}`, `map_markers.{hpp,cpp}`,
  `skyrimse_game_pack.cpp` (one env line), `apps/components_tests/CMakeLists.txt`.
