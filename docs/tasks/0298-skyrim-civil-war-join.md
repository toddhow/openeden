# Task 0298: Skyrim Civil War, joining a side (survey only, NOT built)

Branch: this worktree's branch from `0f4cca6e35`. Status: **blocked in the build; no pack or engine code written, no test written,
nothing run in game.** Only a survey of the data and a harness fork are committed.

## 1. What happened

The fresh build tree `I:\Projects\OpenEden-work\combat3\build` configured (CONFIGURE_EXIT=0) but the build ran at about 2 steps an
hour (41 of 3,921 steps after several hours: the first steps are the vendored glslang and Slang libraries; free virtual memory
sat at 10 to 16 GB with other agents building, so the 15 GB rule made it wait and then thrash). Without an executable the
scenario cannot be run, so nothing below was checked in game. A housecarl query with `*parent` filter on INFO also hung for 30
minutes (tool idle timeout); avoid it.

## 2. Survey (houseCARL read of `Skyrim.esm`, winners differ in this modded list; ids are Skyrim.esm local ids)

| Quest | Id | Facts |
| --- | --- | --- |
| CW00A "Imperial Introductions" | 0D3C5F | flags StartGameEnabled + RunOnce, Type Misc, priority 71. Aliases: Tullius (2, UniqueActor 01327E), Rikke (0, 0132A1), Galmar (1, 014128). One objective 1 "Join the Imperial Legion". Stages 0,1,2,5,7,10,20,21. Script `CW00AScript`, fragments `QF_CW00_000D3C5F` (8). Topics: `CW00Hello` (0D3C50, RechargeExit, 2 INFOs with 4 conditions each, the first has a script fragment), `CW00JoinTopicContinue`, `CW00ATulliusGreetNotWhyImHere`, `CW00ATulliusGreetYes`. |
| CW00B (Stormcloak intro) | 0E1B5A | topics `CW00BUlfricGreet`, `CW00BUlfricGreetYessir`, `CW00BGalmarEveryoneDoThis`, `CW00BStormcloakOathInterrupted` (shares quest 0E2D29 with CW01B oath topics). |
| CW01A "Joining the Legion" | 0D517A | RunOnce, Type CivilWar. Aliases: Tullius (27), Rikke (26) both AllowReserved with 2 package data each; `Fort` (0, location alias SpecificLocation FortHraggstadLocation 019181); Bandit1..25 (ids 1..25, Optional, reference aliases filled from the Fort location). Objectives 1 "Clear out Fort Hraggstad" (25 targets), 100 "Report to Legate Rikke", 160 "Take the oath". Stages 0, 1, 100, 150, 160, 200, 999. Script `CW01Script`, fragments `QF_CW01_000D517A` (7), 25 alias fragment entries. Oath topics `MQ102ALegionOath1..4` (099D07 to 099D04), `MQ102JoinLegionYes` (019457, "I'm ready to take the Oath."), `MQ102JoinLegionNo`, `CW01AJoinLegionBranchTopic` (019458), `CW01AGoodbye` (0C980F, Recharge). |
| CW01B "Joining the Stormcloaks" | 0E2D29 | oath topics `MQ102BStormcloakOath1..4` (099DA3 to 099DA0), `MQ102GalmarJoinTopic` (056165), `CW01BGalmarGreetSentMeToDie`, `CW01BGoodbye`. First objective is the ice wraith camp (not read yet). |

How the line can start (from the names, not yet from conditions): CW00A starts with the game and its Rikke/Tullius greeting
(`CW00Hello`) sets stages 5 to 21 and starts CW01A; the Stormcloak path is the same through CW00B and Galmar. The conditions of
`CW00Hello`'s INFOs (4 each) are what decide whether the main quest's Helgen and Whiterun parts matter: they were NOT read.

## 3. Expected gaps (unverified; to be measured with the scenario)

1. `Fort` is a location alias with a specific location and `Bandit1..25` are reference aliases filled from it (`Location`
   alias reference): the alias filler (task 0217/0226) needs `Location` references for find-matching by location.
2. Package data on the Tullius and Rikke aliases (2 each): the alias-package path (task 0271) exists behind `quest alias packages`.
3. Natives the quest scripts may call (`CW01Script`, `CW00AScript`, `QF_CW01`): `AddToFaction`, `SetPlayerResistingArrest`-like
   calls, `Game.GetPlayer().AddItem` for the Legion/Stormcloak armour, `CWScript` global state, `SetObjectiveDisplayed`: not read.
4. Oath scene: the four oath topics are a scripted exchange (`MQ102ALegionOath1..4`) forced after "I'm ready to take the Oath."

## 4. Harness

`tools/skycivilwar_check/` is a fork of `tools/skycompanions_check` (global Lua with talk/cell/go/door/furn/ids/trace/save
steps, default cell `SolitudeCastleDour`, target Rikke base 78497 (Galmar is 82216, Tullius 78462 decimal)). It is untested
(no executable). Needs an INPUT click file written after the first run's frames are viewed.

## 5. Not run

Everything: no code, no test, no red counts, no frames, no three runs, no Morrowind comparison, no Linux gate.

## 6. Ranked next steps

1. Get a build (the `integ2` build is the only other tree; the combat3 tree needs a quiet machine and the vendored glslang built).
2. Run the harness at `SolitudeCastleDour` with `story trace` and read which of CW00A/CW01A start, which conditions fail.
3. Read the INFO conditions of `CW00Hello` and the `CW01A` stage fragments (use the pack's own decoders, not the houseCARL
   `*parent` filter).
4. Build only what that run shows missing, tests first.
