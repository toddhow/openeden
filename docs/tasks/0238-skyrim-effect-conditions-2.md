# Task 0238: more effect conditions, powers' conditions and regeneration rates (milestone M-S7; follow-up of tasks 0236 and 0228)

Status: BUILT; Windows only, local commits on branch `effconds2` (from `47e9ab826f`), not pushed. Not run: the Linux gate, clang-format 14, clang-tidy, the Lua tests, `openmw-tests`, a fresh review (see "Not run").
Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.17; tasks 0236 (effect conditions), 0228 (race powers), 0186 (magic runtime), 0233 (status effects).
Goal: answer the biggest unanswered effect-condition functions, evaluate the conditions of race powers, and make the regeneration rate effects change how fast a vital comes back (three race powers become real).
Inherited code touched: none outside `game_packs/skyrimse`.
Constraints: pack only; no virtual added to `CombatRequestWriter`; combat edits are hook calls; magic edits additive; no new setting; no existing test edited; with `[Game] neutral combat` off nothing runs.

## 1. The functions

Names and meanings: xEdit's table (`survey_names_xedit.cpp`), the CK wiki and CommonLibSSE-NG (decision 0014's amendment); every meaning stays RECALLED-VERIFY unless the data settles it. Survey tools: `tools/skyrimse_effect_conditions_survey/{functions_by_record,share_answered,regen_values}.py` (aggregates over the five masters).

| fn | name | n | what the data shows | how it is answered |
| --- | --- | --- | --- | --- |
| 74 | GetGlobalValue | 333 | five globals: `GameHour` (VampireSunDamage01..03), `MQClearSkyFogGlobal`, `MQAlduinIgnoreDragonrend`, `DA16SkullDreamCount` | a globals source: a value a Papyrus script set (`GlobalVariable.SetValue`, `PapyrusHost::globalSetByScript`), else the GLOB's stored value (`CombatData::globalDefault`, the world view's `globalValue`). **A global neither knows leaves the list unanswered** (so does a comparison with a global, flag 0x04, that is unknown) |
| 300 | IsInInterior | 132 | the Detect Life family (interior / exterior variants), Clear Skies | the place (below) |
| 310 | GetInWorldspace | 118 | `VampireSunDamage`: not in Sovngarde | the place: true only in an exterior of that world; in an interior always false |
| 365 | IsChild | 23 | Frenzy/Calm: `== 0`; one trap: `== 1` | the actor's race has the Child flag: **RACE DATA +32 bit 2 (0x4), VERIFIED on the data: `NordRaceChild` and `BretonRaceChild` have flags 0x...46, every adult race 0x...43** (`CombatData::raceIsChild`, the world view reads its race index) |
| 719 | IsHostileToActor | 36 | parameter none (the cloaks, Detect Life: "hostile to the caster") and the player reference 0x14 (one poison) | `relationOf(actor, caster) == Enemy`, or against the player for 0x14; any other reference is unanswered |
| 722 | WornApparelHasKeywordCount | 61 | the armour set bonuses: `>= 4` pieces with a keyword | the number of worn pieces carrying the keyword (the actor's `mPieces`, listed per piece) |
| 629 | GetVMQuestVariable | 22 | quest variables of three quests | **not answered: not cheap** (needs the Papyrus host's quest variables inside the combat; the dialogue world has them, the combat has not) |

**The place.** The combat has no cell. The core already tells the cell source where the player stands (`PlayerPlaceSink::onPlayerPlace`, task 0211); `SkyrimCellSource` keeps the last place and gives it to `SkyrimCombat::setPlaceSource`. Every actor is taken to stand where the player stands (only loaded cells have actors); an effect cast before the core told any place leaves 300 and 310 unanswered.

**The shares** (`share_answered.py` over every CTDA of SPEL, ALCH, ENCH, SCRL and MGEF records of the five masters, run-on 0, 1 and 2 with the player reference counted as answerable; the gated test `EffectCond2Real` over the spell effects):

| | before | after |
| --- | --- | --- |
| all 2,691 conditions of the survey | 1,824 (67.8 percent) | **2,527 (93.9 percent)** |
| conditions of the 2,264 spell effects (their lists + their MGEFs, with multiplicity) | 1,963 of 2,529 (77.6 percent) | **2,408 (95.2 percent)** |
| spell effects with conditions that are fully answerable (919) | 629 (68.4 percent) | **832 (90.5 percent)** |

Still unanswered (the spell effects): fn 66 GetShouldAttack 20, 629 10, 185 9, 58 8, 323 8, 453 7, 354 6, 444 5, 569 5, 730 5 and about 17 others of 1 to 4 each.

## 2. GetRestrained for a paralysed actor (task 0236 decision 2)

The data: `GetRestrained` (fn 244) appears in 23 conditions, all `== 0` run on the subject, and every one is on a **Paralysis** effect (16 records: `AlchParalysis`, `ChillrendParalysisFFContact`, `EnchParalysisFFContact`, ...). So the records say "a paralysis does not land on an actor that is already restrained". Counting a paralysed actor as restrained would make a second paralysis refused instead of refreshing the one running (`extendStatus` already keeps the later end). **Decision (mine, for toddhow): a paralysed actor is NOT counted as restrained; `GetRestrained` stays 0** (nothing here restrains an actor: `Actor.SetRestrained` is a counted no-op in the Papyrus host). The alternative (restrained while paralysed) is one line in `World::isRestrained`, but it would also make a held-paralysis chain impossible; the data does not say which Skyrim does.

## 3. Race powers

`applyPowerEffects` now asks `effectConditionsAllow(caster, target, effect, record, unlistedPasses = true)` for each effect on each target (self effects and area effects). A real record is always listed, so its conditions decide; a hand-built record whose conditions were counted and never decoded is still taken as met (what task 0228 did for every power; the existing power tests rely on it). Battle Cry in the real data: the Demoralize effect's list is `HasKeyword(ActorTypeUndead) != 1`, `... Daedra != 1`, `... Dwarven != 1`, `IsUndead == 0`; **it does not test hostility**, so it reaches every living non-undead, non-Daedra, non-Dwarven actor in 60 feet, peaceful or not (0228 decision 2: the record's own conditions say so; the stagger effect `StaggerPushFFSelfArea` has none). Tests and in game below.

## 4. Regeneration rates

Survey (`regen_values.py`): the effects that change a rate are Peak (archetype 34) or ValueModifier (0) on actor values **27, 28, 29** (race powers `PerkArgonianFortifyHealRateNonCombat` 27, `RaceHighElfFortifyMagickaRate` 28, `RaceRedguardFortifyStaminaRate` 29, `FortifyStaminaRateFFSelf`, the Disease/poison "Dis" family, the Dragon Priest masks) and on **155, 156, 157** (the alchemy and enchanting `AlchFortifyHealRate`, `FortifyMagickaRate`, `AlchFortifyStaminaRate`, `AbDamage*Rate`, `Vampire*Rate`: named by the effects on them, RECALLED-VERIFY that 155 to 157 are the "Mult" values). Value 134 (`AbFortifyHealRateCombat`, `RaceArgonianFortifyHealthRate`) is NOT built.

Rule (OURS; `effectiveRegenRate`, `magic_status.hpp`): rate (percent of the maximum a second) = (the race's base + the sum of the 27 to 29 modifiers) x (1 + the sum of the 155 to 157 modifiers / 100), at least 0. The base is RACE DATA +84, +88, +92: 0.7, 3 and 5 for every playable race. The power numbers fit it: Histskin +10 on 0.7 (14 times), Highborn +25 on 3 (9 times), Adrenaline Rush +50 on 5 (11 times); the in-game text is recalled as "ten times faster". Damage effects are negative (the Detrimental flag), so a poison that damages a rate lowers it. The same spell cast again on the same target replaces its earlier effect instead of stacking (`regenRateFor` counts the newest effect of each (spell, effect) pair, found by the first run in game: a repeated cast stacked 55, 105, 155).

Where: `SkyrimCombat::regenerate` (the three rates now come from `regenRateFor`; hook calls only); `applyRest` is unchanged (a rest uses the base: an effect would have to be running); the player's health is the engine's, so only the part the effects ADD goes through a zero-damage `DamageReport` (`flushPlayerHealthRegen`, at least 0.25 health at a time), the same path as a rest's and a level up's gain. The effect itself is the existing non-vital branch of `applyOneEffect` (a Timed active effect that keeps its signed magnitude, as a weakness does for the resistances).

**Which powers become real: three of the four.** Argonian Histskin (health rate, +10), High Elf Highborn (magicka rate, +25), Redguard Adrenaline Rush (stamina rate, +50). **Breton Dragonskin is NOT one: it is a Peak on actor value 83 (absorb chance, `RaceBretonAbsorbSpellChance`), not a rate**, so it stays a status (a spell absorption rule is not built). The survey line of task 0228 grouped it with the rates because it is status-only too. `powerSupportOf` now answers Works for the three. A regen effect from an alchemy potion, enchantment or poison works the same way (any spell that reaches `applyOneEffect`).

## 5. Tests

New `apps/components_tests/skyrimse/testskyrimseeffectconditions2.cpp`, 24 tests in six suites (`EffectCond2Eval` 8, `EffectCond2Pure` 2, `EffectCond2Combat` 6, `EffectCond2Regen` 6, `EffectCond2Powers` 2, `EffectCond2Real` 1 gated). **Red evidence, honest account:** the code was written before the tests (the pure and combat tests cannot compile against the old API, so a stub run was not made); instead four one-line mutations were built and the tests run: `inInterior` inverted, hostility to the caster swapped for hostility to the player, the rate sum and the mult swapped, the power's per-target condition check removed. **9 of the 23 tests then written failed under them** (place 2, hostility 1, regeneration 4, powers 2) and all passed again after restoring. Hand-derived values: rates 5 to 55, 3 to 28, 0.7 to 10.7 (Histskin), (3 + 0) x 2 = 6, (3 - 2) x 2 = 2, one flush of 100 x 10 / 100 x 0.25 = 2.5 health. The old tests pin the old behaviour of 74, 300, 310, 365, 719, 722 as unanswered (`EffectCondEval.theAnsweredFunctionsAreTheListedOnesAndNoOther`, `EffectCondCombat.anEffectWithAnUnanswerableConditionDoesNothingAndIsLoggedOnce`): both still pass because `functionAnswered` keeps its list (the six are answered only by an *extended* evaluator with a source: the combat's) and an unknown global is unanswered; **no existing test was edited**. The wider run (`*Skyrim*:*Combat*:*Poison*:*Magic*:*Craft*:*Perk*:*Status*:*Papyrus*:*Neutral*:*Extension*:*Frozen*:*Condition*:*Ui*:*Power*:*EffectCond*`): 3,639 pass, 0 fail.

## 6. In game

`tools/skybenches_check` through `I:\Projects\OpenEden-work\combatai\eff.sh` (Bannered Mare, Hulda; fixed frame time; the real attack key through `OPENEDEN_RMLUI_INPUT_SCRIPT`; `[Sound] master volume = 0.0`), with `combat test cast = Hulda:<power>` (Hulda casts the power on herself every 3 s of clock; the player's taps make her fight back, so her stamina is spent).
- **Adrenaline Rush** (`PowerRedguardStaminaRegen`, the real record: Peak +50 on value 29, 60 s): `COMBAT effect RaceRedguardFortifyStaminaRate on value 29 changes the regeneration rate by 50.000 for 60.000 s`, then `COMBAT regen actor 108142 rates health 0.700 magicka 3.000 stamina 55.000 percent a second (base 0.700 3.000 5.000)`; recast every 3 s the rate STAYS 55 (it replaces). After her first swing her stamina fell 60 to 46.085 and read 57.828 at the next regeneration line a few frames later (the base 5 percent would give 3 a second). Three runs: the first 10 `COMBAT effect|cast|regen` lines are identical (md5 prefix `fefc3a540ff2`) in all three; the whole fight trace is not byte-identical (the engagement starts when the worker's records are read, the older finding of 0192/0197).
- **Battle Cry** (`PowerNordBattleCry`): `COMBAT effect RaceNordBattleCryEffect conditions hold on actor <id> (caster actor 108142)` for each of the five Mare NPCs and the player (demoralize 30 s; `fear on the player is not applied (the engine owns his body)`); three runs: the first 10 lines identical (md5 prefix `b087b3aa29ae`). **An undead or Daedra refusal is not shown in game** (the Mare has none; covered by `EffectCond2Powers` on the record's shape and by the gated test on the real list: ordinary Pass, undead Fail, Daedra Fail).
- Morrowind: see the changelog row.

## Decisions for toddhow

1. **GetRestrained** stays 0 for a paralysed actor (section 2).
2. **Battle Cry hits peaceful NPCs too**, as its record says; a stricter rule (only hostiles) would be OURS and against the data.
3. **The rate formula** is additive on the race rate and multiplicative for the Mult values (RECALLED-VERIFY; fits "ten times faster"); the first number to change if the in-game text says otherwise.
4. **Dragonskin is not a rate** (absorb chance, value 83): three powers, not four; absorb chance and value 134 are not built.
5. **Every actor is taken to stand where the player stands** for IsInInterior and GetInWorldspace.
6. **GameHour** (VampireSunDamage) reads the GLOB's stored value: nothing here gives the pack the live game hour except the rest service's clock, which is set only while a rest happens; vampires do not exist yet, so nothing is lost.
7. 629 left unanswered (needs the Papyrus host's quest variables in the combat).

## Edits outside the new files

New: `src/combat_regen.cpp`, `testskyrimseeffectconditions2.cpp`, three Python tools under `tools/skyrimse_effect_conditions_survey/`, this doc. Edited (pack only, all additive or hook calls): `include/game_skyrimse/magic_conditions.hpp` and `src/magic_conditions.cpp` (the six functions, `Place`, `GlobalLookup`, `setExtended`), `include/game_skyrimse/magic_status.hpp` and `src/magic_status.cpp` (rate values and `effectiveRegenRate`), `include/game_skyrimse/combat.hpp` (`CombatData::raceIsChild/globalDefault`, `setPlaceSource/setGlobalSource`, `regenRatePercent`, the regeneration declarations), `src/combat.cpp` (`regenerate` reads `regenRateFor` and one call each to `traceRegenRates` and `flushPlayerHealthRegen`), `src/combat_effect_conditions.cpp` (participants' child, hostility and worn pieces, the place, the globals), `src/combat_magic.cpp` (the power's per-target check, the regen value's magnitude kept, `powerSupportOf`, one trace text), `include/game_skyrimse/world_view.hpp` and `src/world_view.cpp` (`raceIsChild`, `globalDefault`), `include/game_skyrimse/neutral_world.hpp` (the place kept and given to the combat), `include/game_skyrimse/papyrus_host.hpp` and `src/papyrus_host.cpp` (`globalSetByScript`), `src/skyrimse_game_pack.cpp` (the global source wired to the Papyrus host and withdrawn with it), `game_packs/skyrimse/CMakeLists.txt` (`combat_regen.cpp` in both lists) and `apps/components_tests/CMakeLists.txt` (the pack source and the test). **No virtual added to `CombatRequestWriter`; no SDK, engine or other pack file touched; no existing test edited.** Other agents' areas (scrolls use `applySpellEffects`, standing stones use abilities): nothing here changes the scroll path; a constant ability with a rate effect is NOT applied (duration 0: the non-vital branch needs a duration), which standing-stone abilities on rates would need.

## Not run

The Linux gate and GCC `-Werror` (the traps were checked by hand), clang-format 14 (formatted by hand), clang-tidy, the Lua tests, `openmw-tests`, a fresh review, Histskin and Highborn in game (only Adrenaline Rush; the others share the code and are unit tested), the player's own Adrenaline Rush with character creation (the stand-in cast was a test aid on Hulda), the player's health flush in game, a constant (ability) effect on a rate, an undead or Daedra Battle Cry refusal in game, `GameHour` live.
