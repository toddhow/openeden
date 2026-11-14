# Task 0165: `--auto-quit` never fires when the new game's player dies

Design doc section: none (test-harness behaviour of `apps/openmw/vsgengine.cpp`).
Goal: a run with `--auto-screenshot N --auto-quit M` quits at frame M even when the player has died.
Inherited code touched: `apps/openmw/vsgengine.cpp` (the frame counting in the loop callback).

## Stage log (2026-11-13, Windows, own build `I:\Projects\OpenEden-work\autoquit\build`, branch `autoquit` from `main` at `ad277e5225`)

**Symptom reproduced.** `tools/ui_check/check.sh <name> off` (defaults: screenshot at running frame 1200, quit at 1500, new game, `--random-seed 1`, fixed frame time 1/60) with the executable of an earlier build: 12 of 12 runs never quit (killed by a 45 s timeout; 10,000 to 12,600 frames drawn, `PS: 59.9999` lines kept coming). In 11 of the 12 the auto-screenshot line was never logged either; one logged it at frame 1200 and never reached 1500. Short runs (`--shot 300 --quit 400`) always quit: 3 of 3.

**Cause (traced).** `runningFrame` is only counted while the state manager is `State_Running` (the guard in the loop callback). A temporary trace of the state each frame showed `running=1042 mgrState=1 type=MWState::Menu` for the rest of the run: `State_Ended`, the main menu. `MWMechanics::Actors::killDeadActors` calls `StateManager::endGame()` when the player's death animation starts. The harness's new game starts the stub player in the exterior at (4093, 4096, 880) with 35 health and he is attacked (`CreatureStats::getAttacked()` was 1 from about frame 180; health fell 35, 22.2, 11.5, 3.0 in steps); a Resist Fire ability is the only active spell, so it is no spell or fall damage. Death came between running frames 1042 and about 1250 in the runs traced, so a quit frame above about 1000 is never reached, while 400 is. Rendering carries on in the menu, which is why frames kept being drawn and the counter froze. Nothing to do with screenshots, the capture queue, the fixed frame time or `[GUI] rmlui` (shown with it off, and with two different executables).

**Fix.** `MWState::countsAsAutoFrame(state, requiresScene, updateLua)` (`apps/openmw/mwstate/autoframes.hpp`) counts `State_Ended` like `State_Running`; loading screens, operations, videos and `State_NoGame` (the menu before a game exists) still do not. The loop uses it. A death no longer stops the run; the auto-screenshot then shows the menu if the player died before it.

**Tests** (`apps/openmw_tests/mwstate/testautoframes.cpp`, 4): run against a stub that counts nothing, 2 failed (the running and the ended case) and the 2 absence tests passed; with the implementation all 4 pass. No existing test edited.

**After.** Same harness, defaults, 12 runs with the fixed executable: Auto-quit logged and the process ended in 12 of 12 (before: 0 of 12).

**Not run.** The Linux gate, clang-format 14, clang-tidy, the Lua tests, the whole `openmw-tests`, a frame comparison (a run that is not cut short counts the same frames as before). Not fixed: why a new-game player with this start is attacked within about 10 s (not investigated: some creature or NPC near (4093, 4096) in the Morrowind exterior; god mode would hide it).
