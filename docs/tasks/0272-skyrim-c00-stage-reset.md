# Task 0272: Skyrim, C00 set back to stage 1 (the Vilkas greeting race)

Branch `c00race` from `48f73269af`. Same hand found and fixed it; no fresh reviewer; Windows only; local commit, not pushed.

## Symptom
`tools/skycompanions_check` (Jorrvaskr basement, `companions` and `companions test training`): C00 reaches stage 20 and quest
0xA3EBC stage 150, but in most runs Vilkas' forced greeting (topic 0xA3E4C) logs "no line of the forced greeting's topic passes
for this speaker", and C00 is back at stage 1.

## Cause
Found with `OPENEDEN_PAPYRUS_NATIVE_TRACE=1`: the second stage set is `quest.setcurrentstageid self 0004B2D9` called from
`CompanionsHousekeepingScript.SetUpCompanions` (C00 stage 0 fragment `Fragment_0`): `utility.Wait(60)`, cycle the radiant quests twice,
then `SetStage(1)`. It is the game's own script and is run as shipped. Stage 0 runs when the Papyrus host starts; the Wait counts
unpaused script time, so stage 1 comes 60 s of unpaused play after that (b1: stage 0 at 22:50:42, stage 20 at 22:51:53, stage 1 at
22:51:55: 73 s real, the open conversation boxes being paused time).

Both INFOs of topic 0xA3E4C need `GetStage(C00) == 20` (condition op 0, fn 58, value 20). `SetStage(1)` after 20 sets the current
stage to 1 (`QuestState::setStage`), so no INFO passes. In a real game the 60 s are over long before the player reaches the
Companions; the scenario talks to Kodlak at update 1800, so the argue scene (about 30 s unpaused) and the dismiss scene end near the
host start + 60 s. Why it differs between runs: the host starts when the records worker is ready (real time, a different frame
each run) while the scenario is frame driven, so the two clocks drift against each other. (A first set of runs used a stale build whose
pack dll predated the Thieves Guild merge; rebuilt at `48f73269af` the failure reproduces at once.)

## Fix
No engine change: the engine ran the quest script correctly, and whether the real game lowers the current stage on a lower SetStage
could not be checked (CK wiki fetch refused, RECALLED-VERIFY), so the semantics are left alone. The scenario now waits until the
start-up wait is over: `tools/skycompanions_check/training_input.txt` (the old input shifted by 4000 frames) and a PLAN shifted the
same, documented in `check.sh`'s header.

## Test
None added: the cause is a 60 s script wait against a frame-driven scenario, reachable only in the running game; the engine behaviour
(a lower SetStage sets the current stage) is unchanged and already pinned by `QuestState` tests.

## Runs after the fix
Five runs (`I:\Projects\OpenEden-work\combat5\c00un_f1..f5`), each: C00 stage 1 first, then 20, then the training quest 0xA3EBC stage 150, no "no line of the forced greeting's topic" message. Before the fix, three runs (`run_b1..b3`): stage 20 then stage 1 1.6 s later, the message, every time.
