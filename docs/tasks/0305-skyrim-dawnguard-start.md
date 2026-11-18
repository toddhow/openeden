# Task: Skyrim Dawnguard, start of the questline (task 0305)

Status: ordered by the coordinator on 2026-11-18. Windows only. Local commit on branch `dawnguard-0305` from `ba12e02d93`, not pushed.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` (M-S7); decisions 0013, 0016, 0017; tasks 0194, 0211, 0217, 0230 (story
events, radiant aliases), 0264, 0277 (Companions), 0271, 0275 (Thieves Guild forced greeting), 0286 (actor cell moves), 0296 (Bards College).
Goal: a character of level 10 who walks into Whiterun's location is met by the Dawnguard messenger (Durak) through the game's own story
manager, quests, packages, forced greeting, dialogue and Papyrus; "Killing vampires? Where do I sign up?" starts the journal quest
"Dawnguard" ("Speak with the leader of the Dawnguard") and puts Fort Dawnguard on the map.

## 1. Survey (Dawnguard.esm and the other masters; scripts decompiled with houseCARL from `Skyrim - Misc.bsa`: this install has no `Dawnguard.bsa`, the DLC archives are merged into the Skyrim ones)

The quest called "Dawnguard" is **not** DLC1VQ01 (that is "Awakening", the Serana quest). The start is a chain of four records:

- `DLC1VQ00Node` (SMQN 0x0200D911, in the story manager tree under the CLOC event): conditions GetLevel of the player >= `DLC1VQMinLevel` (GLOB 10),
  `DLC1VQ01MiscObjective` stage < 10, the new location has the keyword LocTypeHabitation (and not LocTypeOrcStronghold, the old location is not
  a Dwelling ... 10 conditions). Its quest is `DLC1VQ00ChangeLocation` (QUST 0x0200D90B, event CLOC, priority 60).
- `DLC1VQ00ChangeLocation`: aliases `Location` (the event's NewLocation), `LocationCenterMarker` (a reference of the location with the location
  reference type `DLC1SpawnQuestStarter` 0x0200E684: an XMarker per hold, listed in the hold's location by an **ACSR** subrecord of Dawnguard.esm's
  override) and `Messenger` (the unique actor `DLC1Durak`, 0x0201541D). Stage 0 fragment (`DLC1_QF_DLC1VQ00ChangeLocatio_0100D90B.Fragment_1`):
  `DLC1VQ00.Start()`, `DLC1VQ01MiscObjective.SetStage(5)`, `ForceLocationTo` / `ForceRefTo` on `DLC1VQ00`'s `Location`, `LocationCenterMarker` and
  `Messenger` aliases, `Messenger.MoveTo(LocationCenterMarker)`, `Enable`.
- `DLC1VQ00` (QUST 0x0200D90E, start-game-enabled, priority 62): alias `Messenger` (filled only by the script above) carries two packages (ALPC):
  `DLC1VQ00Forcegreet` (ForceGreet template, topic `DLC1VQ00IntroTopic`; conditions: **GetInCurrentLocAlias(Location) of the player**, not in an
  interior, `GetStage(DLC1VQ00) < 20`) and `DLC1VQ00WaitForPlayer` (stage < 50). Its topics: `DLC1VQ00IntroTopic` (the greeting "You there. The
  Dawnguard is looking for anyone willing to fight against the growing vampire menace."), `IntroA` "What's the Dawnguard?", `IntroB`, `IntroC`
  "Killing vampires? Where do I sign up?" (INFO 0x0200D8E2: "... Go talk to him at Fort Dawnguard, southeast of Riften."), `IntroD`.
  Stage 20 displays the objective "Find out about the Dawnguard"; stage 50 (`Fragment_7`) completes it and sets `DLC1VQ01MiscObjective` to 10.
- `DLC1VQ01MiscObjective` (QUST 0x0200D06C, name "Dawnguard", Vampire type, priority 61, 16 stages): stage 5 disables the hall's normal state and moves the
  boss chest; **stage 10 (`Fragment_4`) displays objective 10 "Speak with the leader of the Dawnguard" and `DawnguardMapMarker.AddToMap`**; stages 100 and up
  are Isran's scenes at the fort (`DLC1VQ01IsranIntroScene`, `DLC1VQ01AgmaerScene`, `DLC1VQ01IsranAgmaerScene`, the crossbow and 45 bolts, `DLC1VQ01.SetStage(10)`).
- Durak also has a quest alias package in `DLC1VQ01MiscObjective` (`DLC1VQ01DurakShootCrossbow`, priority 61 against the messenger's 62).

## 2. What the first runs showed (check: `tools/skydawnguard_check/check.sh`, five masters, `player level = 10`, `actor cell moves`, `quest alias packages`)

Four faults stood between the data and the game, found one after another by the story and package traces:

1. **The story world did not know the player's level.** `DLC1VQ00Node failed fn80(...)`: GetLevel (fn_80) was answered only with a speaker and a
   conversation player; the story manager's world has neither. `SkyrimConditionWorld::setPlayerLevel` (the level only, set from the stand-in level
   or the progression's) answers fn_80 for the player there.
2. **An override that adds entries replaced the location's lists.** Dawnguard.esm's `WhiterunLocation` carries only an **ACSR** (its spawn marker) and no
   LC* list: the data shows the A* family is "added by this plugin" (Skyrim.esm has LC*, Dawnguard and HearthFires have AC*). `LocationIndex::add`
   replaced the record whole, so the marker (and every cell, persistent reference and unique NPC the earlier record listed) was lost, and
   `LocationCenterMarker` had 0 listed references. Now a record with A*/R* lists and no L* list (`LocationRecord::mAddsToLists`) keeps the earlier
   record's lists and appends its own. **This changes the lists of every location with such an override** (HearthFires and Dawnguard both have them): which
   exact earlier results move is not measured (decision 1).
3. **A script-forced alias gave its actor no packages.** `packagesFor` knew only unique-actor and specific-reference aliases. Now an alias with packages that
   no data fills (`DLC1VQ00.Messenger`) gives them to the reference a script forced into it (`ReferenceAlias.ForceRefTo`, read through
   `PapyrusHost::aliasReference`, re-resolved when `PapyrusHost::aliasRevision` moves), ordered by quest priority, ahead of the unique-actor ones; and
   a package of such an alias is evaluated with that quest's alias view and the location the player stands in, so **fn_360 GetInCurrentLocAlias**
   (new: the player's location or a parent of it against the location an alias holds) answers. Before, `DLC1VQ00Forcegreet` failed on it.
4. **A forced greeting needs the actor near.** The core's forced greeting (`forcegreet_global.lua`) opens within 3,000 units; the brain asked at once
   and Durak (at the marker 3,300 to 4,200 units from the town gate) never came. A ForceGreet package whose actor is farther than 2,600 units from the
   player now walks up to him (`kGreetApproachFrom`) and asks when nearer than 1,800 (`kGreetApproachStop`), the follower walk reused. Behind `quest alias packages`;
   it also applies to Brynjolf's and Vilkas' greetings when they stand farther than 2,600 units from the player (they did not in the earlier scenarios).

## 3. In game

`tools/skydawnguard_check/check.sh` (a copy of the Thieves Guild check with a `WATCH` list that logs where an actor is, in the cells around the player;
driver `I:/Projects/OpenEden-work/dg305/run.sh`, input `in_c.txt`): the player starts at (24700, -7700) in the Whiterun hold at level 10; the story manager
starts `DLC1VQ00ChangeLocation` (`STORY started 0200D90B ... LocationCenterMarker=0200E688 Messenger=02015C16(Durak)`), the fragment moves Durak into the marker's
cell (`actor cell moves`), `DLC1VQ01MiscObjective` goes to stage 5, Durak starts to walk to the player (`NACTGOTO goal ...`; **he is blocked 540 units from the
marker at (21972, -7687) in every run: the mover cannot cross that spot, cause not found**), so the check stands in for the walk with a harness teleport of the
player to 1,100 units from him (frame 2200); then: `FORCEGREET the actor ... wants to say topic`, `the actor opens its conversation`, the real conversation box
with Durak's greeting and the four real choices (frame viewed), a real click on "Killing vampires? Where do I sign up?" (`DLC1_TIF__0100D8E2.Fragment_0`: his
answer "Ha. Isran's going to like you. Go talk to him at Fort Dawnguard, southeast of Riften."), Goodbye, then `DLC1VQ00` stages 20, 50 (fragments 8, 7, 15),
`DLC1VQ01MiscObjective` stage 10 (`Fragment_4`), `DLC1VQ00` stage 30; `MAP discovered by a script: 33619052`; the journal (J) lists "Dawnguard: I've learned of a
group of vampire hunters known as the Dawnguard. It seems they are looking for new recruits. [ ] Speak with the leader of the Dawnguard" (frame viewed), the world map (M) draws.
Frames and logs: `I:/Projects/OpenEden-work/dg305/runs/r19` and the determinism runs listed in the stage log.

## 4. Ranked gaps (not built)

1. **Isran and Fort Dawnguard.** The conversation with Isran and Agmaer, the scenes of `DLC1VQ01MiscObjective` stages 100 to 250 and Durak's crossbow lesson were not
   reached: the fort is `DLC1DawnguardHQ01` (CELL 0x02001DBA), reached through a worldspace door southeast of Riften; no aid sets a DLC quest's stage, so the interior start needs one
   (a generic `quest test stage = <quest>,<stage>` aid is the cheap next step).
2. **Durak's walk is blocked** at (21972, -7687, -3526) on the Whiterun plain (goal `blocked`), so a real approach from the marker needs the mover fixed (the same family as task 0158's stair
   traps); the check uses a teleport of the player after the CLOC.
3. The messenger's `WaitForPlayer` package (stand at the marker until the player comes) is chosen but has no behaviour of its own; `actor.AllowPCDialogue`, `ObjectReference.MoveToNode` are tolerant no-ops.
4. The 9 other conditions of `DLC1VQ00Node` (old location keywords, other quests) pass here because the player has no history; not tested against a harder case (a Dwelling as the old location).
5. Fort Dawnguard on the map is discovered (a log line), but the dot is not told apart in the world map frame (the same as task 0296 item 5).
6. The package order is now by quest priority for the forced aliases only; the earlier unique-actor and specific-reference alias packages still go by quest id.

## 5. Stage log

- **Red/green.** New file `apps/components_tests/skyrimse/testskyrimsedawnguard.cpp` (registered in the list), 9 tests: `SkyrimDawnguardStory` 3 (fn_80 in the story world), `SkyrimDawnguardLocations` 3
  (`mAddsToLists` parse, add keeps the earlier lists, a record with its own lists still replaces), `SkyrimDawnguardAlias` 1 (fn_360), `SkyrimDawnguardGreet` 3 (the approach).
  Written first and run red against stubs: story 2 of 3, locations 2 of 3 (the third pins the old replace), and the other two groups were written AFTER the code and checked by one mutation each
  (the approach threshold made huge, the location add disabled, `inLocationAlias` returning false: 4 named tests failed with all three together, restored). **The host half
  (`aliasReference`/`aliasRevision`), `packagesForReference`'s forced list and the view's player-location glue have no unit test** (they need a full view; their evidence is the game run).
- **Existing tests edited: none.**
- Edits outside the new files: `dialogue.{hpp,cpp}` (player level, player location, fn_360), `location.{hpp,cpp}`, `quest_alias.cpp` (a trace line), `papyrus_host.{hpp,cpp}`,
  `world_view.{hpp,cpp}`, `package_brain.{hpp,cpp}`, `skyrimse_game_pack.cpp` (one lookup line), `apps/components_tests/CMakeLists.txt`.
- No new setting (everything is behind `quest alias packages`, `papyrus`, `actor cell moves`, which exist), so the test profile is unchanged.
