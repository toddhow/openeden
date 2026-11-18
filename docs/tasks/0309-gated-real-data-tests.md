# Task 0309: tests gated on real game data

Goal: run every test that skips without game data, find the ones that fail, fix the causes, and give the merge loop a
script that runs them (`tools/gated_tests/run.sh`).

## How the gating works

A gated test calls `GTEST_SKIP()` unless an environment variable names the data. The merge run sets none, so these
tests never run there. Found by `grep getenv apps/components_tests apps/openmw_tests` (66 variables; many are developer
switches that write a report or a golden and are not gates). The ones that gate a test, and what each needs:

| Variable | Needs | Set to |
| --- | --- | --- |
| `OPENEDEN_SKYRIMSE_DATA` (91 uses) | the Skyrim SE `Data` folder (five masters, string tables, BSAs) | the Steam `Data` folder |
| `OPENEDEN_BSA_ENTRY_CHECK_DIRS`, `OPENEDEN_ARCHIVEKINDS_CHECK_DIRS`, `OPENEDEN_PAPYRUS_DATA` | a Data folder (`;` separated for the second) | Skyrim SE Data (and Fallout NV Data for the archive kinds) |
| `OPENEDEN_FNV_DATA` | Fallout New Vegas `Data` | the Steam folder |
| `OPENEDEN_SKYRIMSE_PACK_DLL` | the built `skyrimse.dll` | `<build>/game_packs/skyrimse/skyrimse.dll` |
| `OPENEDEN_PAIRED_ROOT`, `OPENEDEN_HAVOK_IDLE_ROOT`, `OPENEDEN_SKYRIMSE_ANIMDATA_DIR` | `Skyrim - Animations.bsa` extracted (a folder holding `meshes/...`) | `<work>/full` |
| `OPENEDEN_HAVOK_ANIMDATA_DIR` | `<root>/meshes/animationdata` | `<work>/full/meshes/animationdata` |
| `OPENEDEN_HAVOK_HKX_DIR` | a folder of ONLY `.hkx` files (the tests walk it recursively and parse every file) | `<work>/hkx` |
| `OPENEDEN_TEST_MORROWIND_DATA`, `OPENEDEN_MORROWIND_DATA`, `OPENEDEN_NIF_MORROWIND_DIR`, `OPENEDEN_ANIMSET_CHECK_DIRS`, `OPENEDEN_ANIMSET_SUITE_DIR` | a Morrowind install or OpenMW's example suite | not on this machine |
| `OPENEDEN_SKYRIMSE_MO2` | an MO2 profile | not set |
| `OPENEDEN_SKYRIMSE_SURVEY_NIF_MAX`, `OPENEDEN_SKYMAT_SURVEY_MAX`, `OPENEDEN_BHK_SURVEY_STRIDE` | caps for sampling scans | set so a run takes minutes |

Extraction traps found on the way (they made five tests fail that were not regressions):

- The extraction tool (housecarl_bsa_extract) leaves eight small text files as raw LZ4 frames (magic `04 22 4D 18`),
  among them `meshes/animationdata/moth.txt`, which the animation-data test reads as a project. Decompress them.
- `OPENEDEN_HAVOK_HKX_DIR` must hold only `.hkx`; the archive also holds a stray `.nif`, `.hkt` and `.hkb`.
- `OPENEDEN_HAVOK_HKX_DIR` must be the whole archive's `.hkx`, not only `actors/character`: the float-track test pins
  the count of 2,331 binding slots.

## The table

Run: components-tests (14,223 tests listed) minus the groups left out (`*Slang*`, `*Complement*`, `*Property*`, the
survey tests named `*when_asked*`: hours of scanning, they only print aggregates), with every variable above set;
openmw-tests whole. Windows, Release.

| Test | Result | Why |
| --- | --- | --- |
| `SkyrimBountyDlgReal.theBountyAliasesFillAndNoOtherStartChanges` | FAIL, fixed | task 0239, see below |
| `SkyrimPerksRealData.everyPerkDecodesAndTheNamedOnesHaveTheScannedNumbers` | FAIL, fixed | task 0295, see below |
| the Havok and animation-data real tests (`HavokAnimDataRealData`, `HavokFloatTracksRealData`, `HavokPackfileRealData`, `HavokRealData`, `HavokIdleRealData`, `HavokGaitRealData`, `HavokPairedRealData`, `SkyrimPairedSurvey`, `SkyrimAnimMotionRealData`) | pass | (they failed first with a wrong extraction: see the traps above) |
| `PapyrusRealData`, `PapyrusRealRun` (2), `ArchiveKindsTest.installedArchives...` | pass | needed `OPENEDEN_PAPYRUS_DATA`, `OPENEDEN_ARCHIVEKINDS_CHECK_DIRS` |
| every other `Skyrim*RealData`, `Skyrim*Real`, `ObScript*` (Fallout NV), the `SkyrimSeGamePack` DLL test, `SkyrimTreePlacementsRealData`, `NeutralDoorRealData`, ... (about 120 tests) | pass | |
| the `*when_asked*` surveys, `SkyrimRadiantSurvey`, `SkyrimCreaturesRealData.surveyOfCreatureRaces` | not in the run | print numbers or take hours; run them by hand |
| `AnimSetPlayEquivalence` (2), `UiRmlInstalledFonts`, `VsgAdaptersDaeClipsSuite`, the Morrowind real tests of openmw-tests | skip | no Morrowind install or example suite on this machine |
| `SkyrimWorldViewRealData.theModList`, `SkyrimCombatGearRealData.aNamedNpcsSearchIsPrinted`, `SkyrimSpeechDemo.writesThePluginWhenAsked` | skip | need an MO2 profile, a name, a writing switch |
| `ModOrganizerRepack`, `ModOrganizerScanner`, `VFSCharacterisation` (case collision), `Kinds/VFSRange` (two) | skip | not data: they skip on Windows by design |

After the fixes: `SkyrimBountyDlgReal`, `SkyrimPerksRealData` and the other Skyrim bounty, radiant and follower tests
pass (118 re-run), the whole non-real Skyrim, dialogue, quest, alias, faction and condition set (3,205 tests) passes,
and the whole openmw-tests (3,230) passes.

## The two failures

### 1. `SkyrimBountyDlgReal`: BQ01, BQ02 and BQ03 `Innkeeper` aliases not filled (task 0230's test)

Not found by a `git bisect` build per commit (76 commits since the test was added, each incremental build about 15
minutes of machine time) but by instrumenting the fill: a trace of the alias's conditions and a direct evaluation of
Hulda against them. The alias has two conditions: `GetIsEditorLocAlias` (passes for Hulda) and
`GetInFaction JobInnkeeperFaction == 1` (fails). Every innkeeper in the five masters is listed in JobInnkeeperFaction
(FACT 0x5091B) at rank -1 (stored byte 255). Commit `af0d24f3e5` (task 0239, follower commands) made a rank of -1 "not a
membership" (the hirelings are listed so in the follower factions until they are hired), and tasks 0261 and 0278
re-added exceptions one faction at a time, only when their setting is on (JobStewardFaction with `houses`,
JobHostlerFaction with `riding`). JobInnkeeperFaction was never added, so with `papyrus` alone no innkeeper is an
innkeeper and the bounty quests lose their Innkeeper alias. That is a game behaviour regression, not only a stale test:
task 0230's in-game check ("BQ02 starts with Steward Proventus and Innkeeper Hulda") no longer holds on main. The note
that started this task said BQ03 only; on this checkout all three fail.

Fix: `kJobInnkeeperFaction` is a default member of the rank -1 set in `SkyrimConditionWorld` and in `SkyrimWorldView`,
and the pack's own set always starts with it (`setRankMinusOneMembers` replaces the set). After it: 11 aliases gained,
0 lost, the quests the story manager starts and the start gates unchanged (the test's own numbers). New test
`SkyrimRankMinusOne.theInnkeeperFactionCountsAtRankMinusOneWithNoSettingAndAnotherFactionDoesNot` (in
`testskyrimseenchantenit.cpp`; written with the fix and not run red: the default set was empty before, so its first
expectation is false on the old code by reading). The existing 0239 test is untouched and passes.

Decision for toddhow: rank -1 is a membership for the job factions (this fix) and not for the hirelings' follower
factions. The data holds -1 in both lists; the real engine rule is RECALLED-VERIFY.

### 2. `SkyrimPerksRealData`: 4 odd subrecords

`ENCH.ENIT` of 32 bytes in 4 of the 604 enchantments of Skyrim.esm (measured, aggregates only). Task 0295 (commit
`0c7a4f870e`) added the ENCH decoder with an ENIT of exactly 36 bytes; the perks test counts the odd subrecords of all
combat records and expects 0. Every field the decoder reads lies in the first 24 bytes, so a 32 byte ENIT reads as well:
`kEnchantEnitShortBytes`. New test `SkyrimEnchantEnit` (2 tests; run red against the old decoder: 1 of 2 failed, the
other pins that other sizes stay odd). The existing `aShortEnitIsCountedAndTheRecordHasNoData` (20 bytes) still passes.

## Not run, and for toddhow

- The Linux gate, clang-format 14 (formatted by hand), clang-tidy, the Lua tests.
- Everything that needs a Morrowind install (see the table).
- The `*when_asked*` surveys and `SkyrimRadiantSurvey` (long scans, no assertions).
- No existing test was edited. The innkeeper rule change (a faction counts at rank -1 with no setting) is a gameplay
  decision for toddhow, see 1.
- On this machine background test processes only advance while a tool call is running; the first full run took hours
  of wall time for about 45 minutes of work.
- `tools/gated_tests/run.sh <build> [work]` needs the animation archive extracted once (the script says how and does
  not extract it; it prints which variables are not set and its tests will skip). A full run takes about 30 minutes.
