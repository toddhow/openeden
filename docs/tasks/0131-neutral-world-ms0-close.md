# Task: M-S0 close-out, Lua and mechanics safety over a neutral cell (N3a-6) and the acceptance pass (N3a-7)

Status: **APPROVED** under toddhow's blanket "go with your recommendations" of 2026-11-11 (decision D13: full loop for engine work). Branch `ms0close`.
Design doc section: `docs/design/neutral-world-model.md` 10.4 (R6, R10), 10.6 (rows N3a-6 and N3a-7); `docs/design/skyrim-se-implementation-plan.md` 5.2 and 8 item 1 (milestone M-S0).
Goal: (1) a neutral Skyrim session does not crash or corrupt state when Lua, mwscript, the console or the mechanics touch a `MWClass::Neutral` object or a neutral cell; every `MWWorld::Class` virtual that a generic engine path can call on a non-actor `Ptr` answers safely instead of throwing; (2) a written, repeatable acceptance list for M-S0/M-S1 that toddhow can run himself, run once here with frames viewed, with Morrowind shown unchanged.
Inherited code touched: `apps/openmw/mwclass/neutral.{hpp,cpp}` (the class; new overrides only, existing ones unchanged); `tools/neutral_session_check/` (a new scenario, `ms0sweep`); `docs/toddhow-checks.md`. Nothing under `apps/openmw/mwlua`, `mwscript` or `mwmechanics` is changed unless the sweep shows a crash there (then the finding and the fix are logged here).
Inputs / outputs: no SDK change, no new setting. New test file `apps/openmw_tests/mwworld/testneutralsafety.cpp`.
Constraints: main thread only; no allocation per frame; the neutral path stays behind `[Game] neutral world`; Morrowind behaviour unchanged (`tools/worldobjects_check` dump and the fixed-frame Balmora frame equal to a parent build).

## What N3a-6 covers (from design 10.4 and 10.6)

- **R10, Lua:** every `Class` virtual the Lua object API calls on a neutral `Ptr` must not throw; "N3a-6 runs the Lua object API over every kind".
- **R6, content files:** every reader of `ESM::RefNum::mContentFile` must be shown never to mis-handle a neutral ref (reserved file `kNeutralRefNumFile`, negative, so `hasContentFile()` is false). The audit table is in the stage log.
- **The incremental queue** (task 0060): the neutral `Ptr`s go through it unchanged; shown with `[Cells] incremental cell build` on in the exterior hop scenario.
- **Mechanics:** a neutral actor is an inert reference (`isActor()` false), so `Actors`, AI, combat and spells never see one; shown with `[Game] neutral actors` on (the objects are drawn) while the sweep and a Lua pass over `types.Actor` run.

## Method

1. Read the `Class` interface and list every virtual whose default throws (`apps/openmw/mwworld/class.cpp`); for each, find whether a generic path (not guarded by `isActor()`, `isItem()`, `isDoor()` or a Lua `objectIsInstance`) can reach it with a neutral `Ptr`. Those get a safe override in `MWClass::Neutral`.
2. A unit test (`testneutralsafety.cpp`) calls those virtuals on one neutral `Ptr` of every `ObjectKind` and expects no throw and the safe value; written first, run red against the unmodified class.
3. An in-game sweep, `tools/neutral_session_check/check.sh ... ms0sweep`: a global Lua script and a player Lua script call a broad set of object, cell, world, nearby, `types.*` and `mwscript` APIs on every object of the Bannered Mare (read-only first, then mutations: scale, teleport, scripts, activate, move into the player's inventory, remove), each in `pcall`, and print per-API ok/error counts. The run must exit 0, with no crash dump and no engine error line other than the known save refusal and refusals the sweep itself provokes.
4. mwscript/console commands on neutral references through `--script-run` (`tools/neutral_session_check/mwscript`).

## Acceptance criteria

- [ ] Each virtual on the list answers without throwing for every kind (unit test, red first).
- [ ] The sweep runs to its last line in the Bannered Mare with `neutral actors` off and on; exit 0; no crash dump; the list of errors it saw is in the stage log and each is a refusal (Lua error text) not a crash.
- [ ] The exterior hop scenario with `incremental cell build` on runs to the end.
- [ ] N3a-7: the written steps (below, and in `docs/toddhow-checks.md`) were run through the check tool; frames viewed.
- [ ] With `[Game] neutral world` off, `tools/worldobjects_check` dump equals the parent build's and the fixed-frame Balmora frame has the same SHA-256.
Out of scope: saving (N6), actors' behaviour (N5), Skyrim scripting, Papyrus, lock picking.

## Stage log

(filled in as the work goes)
