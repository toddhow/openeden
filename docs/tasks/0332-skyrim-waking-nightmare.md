# Task 0332: Skyrim Daedric quest "Waking Nightmare" (Vaermina, Dawnstar, DA16), first slice

Branch `nightmare-0332` from `e400a9f4ec`, rebased onto local `main` `abcd0bb723`. Same hand wrote tests and code, tests first and run red against the old behaviour
(counts below); no fresh reviewer; Windows only; local commits, not pushed. Behind the existing
`[extension.skyrimse] daedric quests` (task 0310) and, for the one engine change, the existing `[Game] neutral actors`.
**No new setting.** Follows tasks 0310 (Barbas, escort-as-follow) and 0305/0319 (the same story-manager and package
machinery).

## 1. Survey (tools: housecarl records for QUST DA16 0x0242AF, `.pex` of `Skyrim - Misc.bsa` decompiled with housecarl)

- DA16 is `StartGameEnabled`, `RunOnce`, 34 stages (0 to 200), 24 aliases (Erandur `ErandurAlias` 0 with 20 alias packages,
  Thorek, Veren, the skull, Fruki, Irgnir, the barrier gem ...), 24 objectives, 36 script fragments (`QF_DA16_000242AF`).
- **Start.** A box trigger in the Windpeak Inn (`DA16SceneTriggerScript`, ref 0x058C57 at (1847, -492, 109)) sets stage 5
  when the player enters it; stage 5 starts `DA16InitScene` (8 phases: the patrons argue about nightmares, Erandur: "Listen
  to your friend, Fruki..."), whose end fragment sets stage 10.
- **Erandur's talk.** His intro branch (topics `DA16ErandurIntroBranch*`, from "What's wrong with everyone around here?"
  to "I trust you. How can I help?") runs `TIF__0002371D`, which sets **stage 25**; its fragment `Fragment_10` makes him the
  player's teammate (`SetPlayerTeammate`), adds him to `DA16ErandurFaction`, sets the global `DA16Walk` and enables the
  "present" markers.
- **The walk.** At stage 25 his package `DA16ErandurEscortToTemple` (0x035068) is an **Escort** package (target: the player,
  destination: the reference `DA16ErandurMeetingMarker` 0x024301 at (40383, 100230, -7189) in the open country, 11,000 units
  from the inn door); its end fragment (`PF_DA16ErandurEscortToTemple`) sets **stage 28**.
- **At the temple door.** Stage 28: `DA16ErandurWaitsOutsideTemple`; his "outside" topics (`DA16ErandurOutsideBranch*`,
  `TIF__00099D5A`) set **stage 30**. Then the escort packages `EscortFromOutsideToStoneDoor` / `EscortFromStoneDoorToBarrier`
  (their end fragments set stage 40), the stone door scene (`DA16StoneDoorTriggerScript`: Erandur enters the trigger), the
  barrier intro (`TIF__00023729` sets stage 50), the library trigger (stage 60), the lab trigger (100), the torpor (120)
  and the dream sequence (stages 140 to 170) follow; the ending sets 190 to 200.

## 2. What runs in game (frames and logs `I:\Projects\OpenEden-work\followers\n332\runs`, scripts `r8.sh`, `in_h*.txt`)

New game in `DawnstarWindpeakInn`, level 10, `daedric quests` with `quest alias packages`, `actor cell moves`, `services`, real
mouse clicks on the conversation box (`OPENEDEN_RMLUI_INPUT_SCRIPT`), quiet window, master volume 0, fixed frame time:

1. The scene trigger sets **stage 5** and the **init scene** plays with the real voiced lines; its end sets **stage 10**
   (run `a3`: the whole chain with no test aid).
2. Erandur's own intro conversation, nine clicks, to **stage 25** (`Fragment_10` runs).
3. The player leaves by the inn door (real activation, `skyrimse:b77489`); **Erandur is carried through the door** and
   follows him in the open country.
4. Erandur, leading, arrives at the meeting marker; the package's end fragment runs **stage 28** (`Fragment_124`).
5. At the temple door Erandur's "Before we enter, I must warn you..." conversation (frame `screenshot007.png`), two clicks,
   **stage 30** (`Fragment_11`).

Three runs of this whole scenario (`r8.sh`, `in_h2.txt`, run in parallel after the rebase) give 30 identical trace lines (stages, talks,
door carries, Erandur's goals; md5 `b2025b704b48`); the real 0 -> 5 -> 10 chain (`r2.sh`) gives 26 identical lines in two runs (md5
`062a6bb8acbb`). Morrowind unchanged: `worldobjects_check` dump (110 objects) byte-identical to the build before the change, Balmora frame
`1c5d47ad0b5dc32d`.

## 3. Faults found and fixed (one mechanism each)

| Fault | Cause | Fix |
| --- | --- | --- |
| Erandur stood on the inn's hearth rim for ever (`blocked edge`) | the mover climbed the 42 high rim of the hearth (the navmesh leaves it out) and may step down only 24 | `NeutralPath::pathStepDown`: while following a path the mover may step down as far as the next two waypoints go down (at most 64): `physicssystem.cpp` `walkNeutralActor`, pure function in `neutralpathfollower.*` (behind `[Game] neutral actors`, no new setting) |
| an Escort package was only "follow the player" (task 0310) and its destination and end fragment never ran; the actor did not follow through doors | `PackageBrain::mPackageFollow` ignored the package location; followers by package were not listed for the door carry | `PackageBrain::runEscort`: an escort leads the player to its destination (walks when the destination can be reached and is within 6,000 units, waits when the player is more than 1,500 units behind, arrives within the package radius (at least 96), runs the package's end fragment once); every package follower is a traveller (`travellerRefs`, carried through the doors the player uses) |
| the escort never started its walk in the open country: the exterior navmesh of an NPC-sized agent is not available (task 0134) | `NoPath` after 20 s of `NotReady` | an escort leg that ends `NoPath` is walked straight at the destination (`mEscortDirect`, walk speed 82.45) |

## 4. Tests (written first, run red)

- `testskyrimsedaedricescort.cpp` (`SkyrimDaedricEscort`, 9 tests): the first group of 6 failed 4 of 6 against the brain of task
  0310 (the 2 that passed pin the setting off and a destination far away); after the first implementation one more test
  (a destination beyond the loaded country) failed 1 of 7, and two (direct walk after no path, stop on arrival) failed 2 of 9
  before their code.
- `testneutralpathdrop.cpp` (`NeutralPathDrop`, 7 tests): 3 of 7 failed against a stub (the other 4 pin the unchanged cases).
- No existing test edited. `components-tests` filter `SkyrimDaedric*:*Companions*:*College*:*Travel*:*Package*` 200 pass;
  `openmw-tests` `NeutralPath*:NeutralMover*` 104 pass.

## 5. Stand-ins (hand-written, named)

- `story test quest = 0242AF:10@1` puts the quest at stage 10 in the runs that start from the conversation (the real
  chain 0 -> 5 -> 10 is run 1 above and in `a3`).
- **The 11,000 unit walk from the inn door to the meeting marker is replaced by a teleport of the player to a spot 130 units
  behind the marker** (the player's door arrival carries Erandur along: the Lua `abs` step, then a second `same` step at the
  real ground height). The marker is 3,700 units above the door and the straight way to it is a cliff (`blocked edge`), the
  exterior navmesh for an NPC-sized agent does not exist (task 0134), so neither the navigator nor the direct walk can bring
  him there; the escort's arrival rule and end fragment run for real once he is there.
- Real `activateBy` of the Lua test script stands for the key press at the inn door (as in task 0310).

## 6. Not reached

Stage 40 and later (Erandur's walk through the temple door and to the stone door, the stone door scene, the barrier intro,
the library, the Torpor, the lab, Thorek and Veren, the dream sequence, the skull, the ending); Erandur's follower AI inside
the temple; the Windpeak Inn's Fruki/Irgnir lines (the scene waits 5 s each for them: not loaded in that cell).

## 7. Decisions for toddhow

1. **A package follower is a door traveller** (task 0310's Barbas now follows through doors too).
2. The escort rules (1,500 wait, 6,000 reach, 96 arrival, 82.45 direct speed) are estimates, named constants in
   `package_brain.hpp`.
3. The step-down rule applies to every neutral actor that follows a path (not only the daedric quests): the navmesh's own
   waypoints vouch for the drop.
4. The exterior navmesh problem (task 0134) is what keeps this slice from walking the whole way: a baked or tile-limited
   navmesh for NPC agents would make the escort real.

## 8. Not run

The Linux gate, clang-format 14, clang-tidy, the Lua tests, a fresh review.
