# Task: Skyrim SE conditions engine v1 (framework and the five named functions)

Status: built (same hand wrote tests and code, no fresh reviewer; Windows only; see the stage log).
Design doc section: `docs/design/skyrim-se-implementation-plan.md` section 8 item 9; `docs/research/0055-skyrim-rules-survey.md` sections 2 and 6.1.
Goal: evaluate a decoded CTDA condition list against a world the caller supplies, so dialogue, quests and packages can later be gated on it.
Inherited code touched: none. New: `game_packs/skyrimse/{include/game_skyrimse,src}/conditions.*`. Reuses `survey::Ctda`, `survey::decodeCtda`, `survey::forEachSubrecord`.
Inputs / outputs: `std::span<const survey::Ctda>` and a `ConditionWorld` (resolve run-on, base id, quest stage, stage done, faction membership, global value); a bool plus counters.
Constraints: pure code, no file access, nothing in the engine calls it yet; no allocation per evaluation except the unknown-function counter map and one log string per new unknown index; fail-closed on anything unknown.
Acceptance criteria:
- [x] Compare operators 0 to 5, OR chaining with OR binding tighter than AND, run-on resolved by the caller, the use-global flag, function dispatch by index through a table.
- [x] fn_72 GetIsID, fn_58 GetStage, fn_71 GetInFaction, fn_59 GetStageDone, fn_74 GetGlobalValue implemented against a narrow query interface; fn_566, fn_426, fn_629, fn_560 stay unknown on purpose (no clean source for their meaning).
- [x] An unknown function, a compare code above 5 or an undecodable list evaluates to false as a whole (fail-closed), counted per function index and logged once per index.
- [x] Every branch has a test written first and run red against a stub.
- [x] Gated real-data run over every INFO condition list of the five masters against a stub world answering 0 or false; aggregates only.
Out of scope: the unnamed functions, functions above the five, any engine wiring, quest aliases (run-on 5 resolution is the caller's), perk entry-point conditions.

## Decisions (all RECALLED-VERIFY unless the data shows them)

- Compare codes 0 to 5 are ==, !=, >, >=, <, <= (code 0 is 84 percent of uses, which fits ==).
- OR flag 0x01 means OR with the next condition; OR groups bind tighter than AND: `A OR B AND C` is `(A or B) and C`. The data cannot show this; the opposite reading would make `A or (B and C)`. A test pins the chosen one. A last condition that carries the OR flag simply ends its clause.
- Use-global flag 0x04: the comparison float's bits are the global's (plugin-local) form id.
- Fail-closed is at LIST level: one unknown function makes the whole list false even inside an OR, because the engine cannot know that the unknown condition would not have decided it. Chosen over per-condition false, which would let a list pass or fail by accident.
- Every condition is evaluated (no short circuit), so the unknown counters do not depend on list order.
- GetIsID and GetInFaction are 0 when the run-on resolves to nobody (subject 0).
- Form ids are handed to the world unchanged (plugin-local); resolving them to the load order is the caller's job.

## Stage log

- Branch `conditions` from main (`5ee3762b29`). Build folder `I:\Projects\OpenEden-work\conditions\build`, `components-tests` only, `-j 2`.
- Red run: with `conditions.cpp` a stub (decode false, no functions, compare invalid, evaluate false) 21 of the 24 new `SkyrimConditions*` tests failed; the 3 that passed test absence (an unknown function stays unknown, a bad subrecord stays malformed, a broken framing stays malformed). Then the real code: 24 of 24 pass.
- Real data (`SkyrimSeConditionsReal.every_info_condition_list_of_the_masters`, five masters, aggregates): 32,148 INFO records hold conditions (76,556 conditions, 0 malformed, 0 inflate failures). With the five functions known and the stub world, **12,570 lists (39.1 percent) are evaluated completely and 19,578 (60.9 percent) fail closed**; 37,207 conditions (48.6 percent) are unknown, spread over 118 distinct function indices. The three biggest holes are the unnamed fn_566 (8,648 conditions), fn_426 (7,900) and fn_629 (5,419); then fn_359 (902), then named-upstream but not built here fn_77 (845), fn_47 (822), fn_67 (777), fn_14 (696). The five built functions cover 51.4 percent of INFO conditions (the research note's 44.8 percent of all uses for these five is over all record types; INFO leans harder on fn_566, fn_426 and fn_629).
- Checks: `CI/check_layer_rules.sh` 104 exceptions; `check_tests_registered`, `check_extension_sdk`, `check_game_conventions`, `check_no_osg`, `check_mwbase_fakes`, `check_cmake_format` pass. `check_file_names` lists two files of other sessions (`test_ui_input_script.cpp`, `rotation_ground.cpp`), not touched here.
- Not run: the Linux gate (Docker hung) and GCC `-Werror`, clang-format 14 (not installed; lines kept under 120 columns by hand), clang-tidy, sanitizers, the Lua tests, `openmw-tests`, the whole `components-tests`, a fresh review, conditions other than INFO, a real world implementing `ConditionWorld`.
- For toddhow: meanings for fn_566, fn_426, fn_629, fn_560 (research note question 1); fn_359, fn_77, fn_47, fn_67 and fn_14 are the next best additions (the first has no name, the other four have upstream names).
