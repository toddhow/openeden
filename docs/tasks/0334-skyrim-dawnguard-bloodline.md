# Task: Skyrim Dawnguard, after "Awakening": Serana follows, "Bloodline" to Castle Volkihar's hall (task 0334)

Status: ordered by the coordinator on 2026-11-18, follow-up of task 0330 (and 0319, 0305). Windows only. Local branch `dawnguard4-0334` from
`abcd0bb723`, not pushed. Behind the existing `[extension.skyrimse] dawnguard` (the same switch as 0330), no new setting.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` (M-S7); tasks 0330 (the crypt), 0310 (Follow packages that name the player),
0196/0239 (followers), 0194 (scenes), 0298 (alias package order by quest priority).
Goal: from Serana's conversation in Dimhollow Crypt (DLC1VQ02 "Bloodline" at stage 5) to her following the player out of the crypt through the
game's own data and scripts, and on toward Castle Volkihar.

## 1. What the data says

DLC1VQ02's stage 5 fragment (`Fragment_16`) does three things: `MM.EngageFollowBehavior(false)` (the quest script of DLC1NPCMentalModel,
`0x02002B6E`: sets `IsFollowing`, `CanFollow`, `SetPlayerTeammate(true)`, starts the monitoring quest), starts DLC1VQ02RNPC and shows objective 10
"Lead Serana to her home". **Nothing adds her to CurrentFollowerFaction and nothing is a "recruit" dialogue: stage 10 is NOT the recruit** (it is
`Fragment_2`, `DisengageFollowBehavior`, set when the player arrives at the castle). She follows only through packages.

Her package list is built from two quests' aliases:

| quest | priority | alias | packages |
| --- | --- | --- | --- |
| DLC1NPCMentalModel `0x02002B6E` | 80 | `ResponsiveNPC`, filled with the placement `DLC1SeranaRef` (ALFR) | `DLC1RNPCFGPostQuestline`, `DLC1NPCStayVQ04`, `DLC1NPCStayVQ02` (stay while the player is at Fort Dawnguard), `DLC1NPCFollowSuperFar`, `DLC1NPCSandboxAroundPlayer600`, `DLC1NPCFollowFar`, `DLC1NPCFollowMedium`, `DLC1NPCFollowClose`, `DLC1NPCStayHome` |
| DLC1VQ02 `0x02002F65` | 70 | `Serana`, an EXTERNAL alias (ALEQ/ALEA) of the first | `DLC1VQ02SeranaAbandoned` (when stage 5 is done, stage 10 is not and she is not in CurrentFollowerFaction), `DLC1VQ02SeranaReactToHarkon`, `DLC1SeranaCastleSit` |

The follow packages are on `GetVMQuestVariable` conditions of the MentalModel script (`FollowDistanceFar/Medium/Close`, `IsFollowing`, ...) and use
the template `FollowPlayer` (`0x0750BE`): a **Follow** branch (WaitingForPlayer 0) and a **Sandbox** branch (WaitingForPlayer 1, "stay near").

DLC1VQ02 stages (fragments): 5 follow starts; 6 `DLC1VQ01GargoyleTriggerScript` (a box in `DLC1DimHollowCrypt02`, REFR `0x02003EDC`, center
(6168, 4108, -440)): the gargoyle ambush (`Fragment_26`: `Serana.StartCombat(gargoyle)`); 8 `DLC1VQ02TrigSetStage8` (a box in `DLC1VampireCastleGuildhall`,
center (2840.6, -5915.2, 7054), prerequisite stage 5): `Fragment_23`, `DLC1VQ02HarkonAndSeranaScene` (14 phases); 9 the scene's first phase fragment;
10 `DLC1VQ02TrigSetStage10` (another box in the hall): follow ends. The way TO the castle (Garan Marethi's boat, the `Boat` and `MainCastleDoor` aliases)
is a boat ride that is not built (see 6).

## 2. What was missing, found by running it

Three runs of `tools/skydawnguard_check/bloodline.sh` and a short form (`bloodline_quick.sh`) with the new developer trace `PKCHOSEN` (below):

1. **The wrong quest's packages came first.** `SkyrimWorldView::packagesForReference` put the packages of aliases a script or an external alias filled
   ("forced", DLC1VQ02, priority 70) in front of those of an alias filled with the placement itself (DLC1NPCMentalModel, priority 80), whatever
   the quests' priorities (task 0298 had ordered only the forced ones among themselves). `DLC1VQ02SeranaAbandoned` was chosen and she stood.
   Now ONE order for both kinds: the quest of the higher priority first, a forced alias before a specific one at the same priority (the
   behaviour of before), then the quest id (`orderAliasPackageGroups`, `quest_priority.hpp`; the pack's trace line says each package's quest priority).
2. **`DLC1NPCFollowMedium` was classified as a Sandbox package** (`PKCHOSEN ... behavior 2 activity 0`): the `FollowPlayer` template's procedure names hold
   `Follow` and `Sandbox`, `classifyProcedures` took "sandbox" first and `classifyActivity` returned none for it, so task 0310's follow-the-player
   packages (which need activity Follow and a target naming the player) never applied; she wandered round her place. A Follow with a Sandbox branch is a
   Follow now (not with a Patrol or Wander in it; a sandbox, sleep, eat and a patrol keep their answers).
3. **Package followers were not carried by the player's doors and teleports** (`SkyrimActorTable::followers` listed only the brain's followers, which only the DialogueFollower
   alias makes, and the scene travellers). `PackageBrain::packageFollowerRefs()` lists the placements whose chosen package is a Follow/Escort of the player
   (Serana, Barbas); the table adds them to the carried set and to `isFollower` (the side in a fight). They are no brain followers (not saved, the packages decide).
   Side effect for toddhow: Barbas (task 0310) is carried through doors and takes the player's side now.
4. Diagnostics worth keeping: `OPENEDEN_PACKAGE_TRACE=<base id>` also logs `PKCHOSEN <ref> <package> behavior B activity A target T/V [follows the player]` on each
   package switch (`PackageSource::packageChosen`, called only when the source says it traces: no allocation otherwise).

## 3. In game (`tools/skydawnguard_check/bloodline.sh`, real clicks on Serana's conversation, then the steps below; frames in `I:\Projects\OpenEden-work\dg334\keep`)

`bloodline.sh` (awaken.sh's chain to stage 200, Serana's 25 real-click turns and the real Goodbye sweep, then): the harness lifts her out of the pit (`place`), the player walks
south by the real S key, the player is teleported into the gargoyle box, to the exit door, through the door, and to the hall's stage 8 box. The game's own data and scripts do the rest:

- Stage 5 (`Fragment_16`) runs; `PKCHOSEN ref 33565556 DLC1NPCFollowMedium behavior 6 activity 2 target 0/20 follows the player` (before: `DLC1VQ02SeranaAbandoned`, then
  `DLC1NPCFollowMedium behavior 2 activity 0 target 0/20`, a sandbox that wandered round her place).
- The S walk: Serana (put at 4255,1010) walks 4248,991 to 4251,878 behind the player, who is stopped by the room at about y 673 (seen in the short runs), and stands 205 units from him: nearer than her follow distance of 260, so she stops there (the same samples in `l1`, `l2` and `l3`).
- The gargoyle box fires for the player: `quest 02002F65 set to stage 6` and `Fragment_26` run by the game's own `DLC1VQ01GargoyleTriggerScript`.
- Teleports and the exit door: `Neutral follower 33565556 carried to DLC1DimHollowCrypt02` (and to the wilderness at 14883,50999,-1148 beside the player, and to `DLC1VampireCastleGuildhall`).
- The hall's box sets **stage 8** by the game's own trigger; `Fragment_23` starts `DLC1VQ02HarkonAndSeranaScene` (14 phases, 49 actions): Serana, Vingalmo and Harkon get their scene packages, the real voiced
  lines "How dare you trespass here!" and "Wait... Serana? Is that truly you? I cannot believe my eyes!" play with subtitles, phase 1 times out after the runner's 60 s package wait and phase 2 starts (`SCENEWAIT` lines; stage 9 and the Harkon conversation are not reached).
- Three runs (`l1`, `l2`, `l3`, run at once) give the same 405 trace lines (PKCHOSEN, quest stages and starts, SCENE, MQ steps and watches without the distance column, FORCEGREET, carried followers, NTALK, scene speech; md5 prefix `c135aec4c4d9`,
  `keep/trace_l1_l2_l3_identical.txt`), exit 0, 0 error lines, 0 dumps. An earlier set (`j1` to `j3`) without the walk gave the same trace in all three. Frames viewed: Serana's conversation
  (`keep/serana_talk.png`) and the snow forest in Tamriel after the exit door (`keep/exterior.png`; Serana stands behind the camera).
- The short form (`bloodline_quick.sh`, 3 minutes; `story test quest` sets the VQ01 stages instead of the puzzle): the real S walk shows `NACTGOTO goal ... speed 82.45` towards the player from her place (a walk at 82 units a second, the run
  at 350 beyond 700 units), her positions 4273,1083 to 4256,867 to 4208,702, 53 units from the player. **Three runs of it are NOT identical**: the height the lifted actor rests at after `place` varies (-1015, -976, -952, -967 on the coffin's rim) and with it whether she is
  blocked, so its numbers are shown for the first run only.

## 4. Stand-ins (named)

- All of 0330's: the key press (the harness activates doors, button, braziers, coffin and Serana by `activateBy`), teleports into the boxes and between places, the vampires' death by the aid.
- **`place` step (new, in the harness's global script): Serana is lifted out of the coffin's pit.** Her exit (`SeranaExitScene`, `bed_leftexit`) ends at the furniture marker's floor (z -1169.6), where the
  floor she stands on after the fall is at -1121 and the room is 114 higher: she is "blocked edge" for ever. The coffin's lid has
  `coffin.PlayAnimation("open")` and the coffin model has no animation (`NANIM ... has no animation`), so nothing opens (the same gap as 0330 item 2). The harness puts her on the room floor beside the player.
- The player's walk in the crypt is the real S key (five seconds) in `bloodline.sh`; the gargoyle box, the exit door and the hall box are reached by teleports (Lua `teleport` carries
  every follower, wherever it stands: so the long walk through the crypt is NOT shown by them).
- The castle is entered by a teleport (`cell` step) to the hall's stage 8 box; the boat ride and the door are not built.

## 5. Not built or not reached, in rank order

1. **The scene `DLC1VQ02HarkonAndSeranaScene` does not finish in the run** (stage 8 starts it with the game's own trigger box, Vingalmo's real lines "How dare you trespass here!" and "Wait... Serana?
   Is that truly you?" play and phase 1 holds): its package actions end only when Vingalmo's package finishes (his path is about 5,000 units) or after the scene runner's 60 second package timeout
   (`SCENEWAIT` shows `actions pending`). Harkon's own ForceGreet package (`DLC1VQ02HarkonStage7BlockingTopic1`, whose INFO needs `GetStageDone(VQ02, 9)`) says "no greeting passes for this speaker" every 6 seconds, as it should until stage 9.
   The run ended before the timeout; NOT followed to stage 9 and Harkon's conversation (stage 10 and the objective "Speak to Harkon").
2. Serana's exit from the coffin without the stand-in (the lid, 0330 item 2), her fight with the gargoyle (stage 6 sets and `StartCombat` is sent; the ambusher alias is a location alias that nothing filled: no fight was seen), the boat to the island and the main castle door.
3. Serana's real walk THROUGH the crypt to the door: Lua teleports carry her. The short walk in one room is shown (the S key), but the end positions of that walk vary between runs (the lifted actor rests on the coffin or beside it at
   different heights after `place`), so only the first run of it is quoted as a picture of the behaviour.
4. `Faction.SendAssaultAlarm` is still unregistered (0330 item 4); the monitoring script's updates were not counted.

## 6. Stage log and tests

- Tests: `apps/components_tests/skyrimse/testskyrimsedawnguardbloodline.cpp`, 8 new. `SkyrimAliasPackageOrder` (4): the alias group order (priority, forced first at a tie, the quest id, a group's own order kept) and the brain's
  `packageChosen` call: **2 of the 4 failed red against a stub that put the forced groups first** (the other two pin ties and the group order, the call was written with the code); `SkyrimFollowTemplate` (2): **1 of 2 red** (a Follow with a sandbox
  branch; the second pins that a sandbox, sleep, patrol and plain Follow keep their answers); `SkyrimPackageFollowers` (2, a brain with a Follow package that names the player: listed, carried, not a brain follower, nothing when the package fails or the
  setting is off): written after the code, compile-red against the old brain only, no mutation run. No existing test edited.
- Wide runs: `components-tests` filter `*Skyrim*:*Papyrus*:*Scene*:*Package*:*Quest*:*Dialogue*:*Extension*:*Frozen*:*Death*:*Dawnguard*` 4,361 tests, 4,230 pass, 131 skipped (data-gated), 0 failed;
  `openmw-tests` filter `*Neutral*:*Skyrim*:*Extension*` 1,162, 1,160 pass, 2 skipped, 0 failed.
- No new setting (everything behind `[extension.skyrimse] dawnguard`, which `daedric quests` also turns on in the same code path, so Barbas changes too): `tools/testprofile/coverage.sh` OK.
- Morrowind unchanged against `integ2`'s build (a build of `main`, not this exact parent): `tools/worldobjects_check` 110 objects OK (111 result lines each), the fixed Balmora frame `1c5d47ad0b5dc32d` for both.
- Not run: the Linux gate, clang-format 14 (formatted by hand), clang-tidy, the Lua tests, a fresh review, saves in the middle of the quest (the package follower set is not saved; a load rebuilds it at the first package evaluation), the packages of a *saved* Serana.
- Decisions for toddhow: (1) one order for forced and specific alias packages by quest priority (a tie keeps forced first); (2) a Follow with a Sandbox branch is a Follow (the template's sandbox is the "wait" state, not modelled: the actor value
  `WaitingForPlayer` is read only as `FollowerState` waiting for brain followers); (3) package followers are carried through doors and take the player's side (also Barbas); (4) `place` and the `bloodline_quick.sh` form are harness stand-ins.

