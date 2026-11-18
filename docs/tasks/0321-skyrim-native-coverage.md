# Task 0321: Papyrus native coverage survey and a first batch of shared natives

Design doc section: `docs/research/0057-papyrus-native-coverage.md` (the survey, the cross-game tables, the ranked plan).
Goal: say, from the code, which natives the vanilla Skyrim scripts call and which we answer; then answer the cheapest
ones, in the common library when Skyrim SE and Fallout 4 share them.
Inherited code touched: none under `apps/openmw`. `components/script_papyrus` (OpenEden's own, task 0093) gained
`nativeCallers()` and `papyrusformnatives.*`.

## Built

- `PapyrusRuntime::nativeCallers()` (call sites split by calling script; default empty on the interface).
- `PapyrusFormQueries` (SDK header, optional capability of a `PapyrusWorld`, default refusing bodies, no version bump).
- `ScriptPapyrus::FormNatives`: Actor.GetLevel, Actor.GetRace, ActorBase.GetRace, FormList.GetSize/GetAt/HasForm,
  ObjectReference.GetAngleX/Y/Z; twelve cosmetic no-ops (Sound, EffectShader, VisualEffect, Game.Shake*, Actor.SetAlpha
  and three animation-graph variable natives).
- Skyrim pack: `PapyrusHost` implements the queries over `PapyrusGameData` (three new defaulted virtuals),
  `SkyrimWorldView::actorLevelOf/actorRaceOf`, `PapyrusHost::registeredNatives()`, `regNoop` for the existing no-ops.
- The gated survey `testskyrimsenativecoverage.cpp` (two tests: Skyrim + Fallout 4 declarations; New Vegas against the
  Skyrim conditions and natives).
- Developer switch `OPENEDEN_PAPYRUS_NO_COMMON_NATIVES` (not a setting: a test aid).

## Evidence

- Red first: 10 of 10 `PapyrusFormNatives` and 2 of 2 `SkyrimPapyrusFormNatives` tests failed against stubs of the
  library; 12 of 12 pass. (The survey itself is a measurement, written with the code.)
- Gated real data: the common natives register against all 14,026 real scripts' classes with 0 failures (21 registered:
  the global flags are right); `*Skyrim*:*Papyrus*:*Frozen*:*Contract*:*Extension*:*Pack*` pass.
- Coverage by call site: registered 87.13 percent before, 90.98 percent after (answered 83.44 to 84.41, deliberate
  no-ops 3.68 to 6.56); missing 8,147 sites to 5,710.
- In game (start-up scripts, Helgen start, 20,000 frames, 3 runs each, identical within a group): distinct tolerant natives
  15 before (the switch above), 14 after; `actor.getlevel` is answered.
- Morrowind unchanged against the `integ2` build: `tools/worldobjects_check` 110 objects OK, Balmora frame `1c5d47ad0b5dc32d`
  for both.
- `CI/check_layer_rules.sh`: 104 exceptions (unchanged); `check_extension_sdk`, `check_tests_registered`,
  `check_cmake_format`, `check_no_osg`, `check_game_conventions` pass.

## Not run

The Linux gate (GCC `-Werror`), clang-format 14 (not installed here; lines kept to 120 by hand), clang-tidy, the Lua
tests, `openmw-tests`, a fresh review, saves with a script that called a new native, Fallout 4 call-site counts (the
install has no game scripts), a New Vegas condition evaluation.

## Decisions for toddhow

See section 7 of the research note: the optional interface instead of a version bump, the cosmetic no-ops, the
stand-in player level, the refused player race, declaration-only Fallout 4 comparison. No existing test was edited.
