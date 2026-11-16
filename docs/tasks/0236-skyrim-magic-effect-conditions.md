# Task: Skyrim magic effect conditions are evaluated (milestone M-S7; follow-up of tasks 0186 and 0233)

Status: BUILT, Windows only, local branch `effconds` from `873cd6c130`, not pushed. See the stage log at the end.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.17; tasks 0186 (magic runtime), 0233 (paralysis, frenzy, fear), 0179 (perk
condition world), 0122 (conditions engine).
Goal: the CTDA conditions on the effects of spells, poisons and enchantments (after an effect's EFID/EFIT, and on the MGEF itself) are evaluated
for the actor the effect lands on and its caster, so a real Paralysis, Frenzy or Fear poison works with no test setting and an immunity refuses
the effect.
Inherited code touched: none outside `game_packs/skyrimse` (see "Edits outside the new files").
Constraints: pack only; no virtual added to `CombatRequestWriter`; combat edits are hook calls; the records change is additive; no new setting;
`combat test spell conditions` stays as an override; no existing test edited; with `[Game] neutral combat` off nothing runs.

## 1. Survey (`tools/skyrimse_effect_conditions_survey/survey.py`, the five masters, aggregates)

Effects read (an effect is one EFID of SPEL, ALCH, ENCH or SCRL, or one MGEF):

| record | effects | with conditions |
| --- | --- | --- |
| SPEL | 2,437 | 437 |
| ENCH | 1,070 | 90 |
| SCRL | 127 | 11 |
| ALCH | 577 | 1 |
| MGEF | 1,633 | 534 |

2,691 CTDA in all, **0 of an odd size**, 57 distinct functions. **Run-on (CTDA +20): 0 (Subject) 2,360, 1 (Target) 96, 2 (Reference) 235.**

By function (conditions; the name is xEdit's table, `survey_names_xedit.cpp`):

| fn | name | n | | fn | name | n |
| --- | --- | --- | --- | --- | --- | --- |
| 560 | HasKeyword | 726 | | 719 | IsHostileToActor | 36 |
| 74 | GetGlobalValue | 333 | | 289 | IsInCombat | 34 |
| 80 | GetLevel | 175 | | 69 | GetIsRace | 27 |
| 448 | HasPerk | 167 | | 244 | GetRestrained | 23 |
| 715 | IsUndead | 162 | | 365 | IsChild | 23 |
| 300 | IsInInterior | 132 | | 629 | GetVMQuestVariable | 22 |
| 310 | GetInWorldspace | 118 | | 640 | GetActorValuePercent | 20 |
| 72 | GetIsID | 92 | | 14 | GetActorValue | 19 |
| 700 | IsCommandedActor | 81 | | 66 | GetShouldAttack | 19 |
| 682 | WornHasKeyword | 76 | | 46, 58, 185, 354, 18, 323, ... | | 2 to 14 |
| 722 | WornApparelHasKeywordCount | 61 | | 597 | GetEquippedItemType | 53 |
| 214 | HasMagicEffect | 49 | | 71 | GetInFaction | 49 |
| 77 | GetRandomPercent | 39 | | 699 | HasMagicEffectKeyword | 16 |

(The full list is in `I:\Projects\OpenEden-work\combatai\effcond_survey.txt`, not committed.) The functions the MGEFs of the four status archetypes use:
**Calm (6)** 560, 715, 700, 448, 237, 58, 72, 71, 354, 365; **Fear/Demoralize (7)** 560 (100), 715, 448, 700, 71, 640, 72;
**Frenzy (8)** 560, 715, 700, 365, 448, 72; **Paralysis (21)** 560, 244, 77, 448, 640, 72.

**Which subject is which** (VERIFIED by reading records, `tools/skyrimse_effect_conditions_survey/dump.py`):
- `AlchParalysis`: `HasKeyword(ImmuneParalysis) == 0`, `GetRestrained == 0`, run-on 0. It is the actor being paralysed that must not be immune:
  **Subject = the target of the effect**.
- `ParalysisFFAimed`: the same plus `HasKeyword(ActorTypeDragon) == 0`. The spell `Paralyze`'s second effect (`StaggerPushFFAimed`) carries
  `HasKeyword(ActorTypeDragon) == 1` on the SPEL's effect: a dragon is staggered, not paralysed.
- `PerkImpactStaggerPushFFAimed`: `EffectWasDualCast == 1` and `HasPerk(Impact) == 1`, both **run-on 1**: Impact is a perk of the caster, so
  **run-on 1 (Target) = the caster** (RECALLED-VERIFY of the wiki; the data settles it).
- `PerkMasterMindConfDownFFAimed`: `HasPerk` (caster, run-on 1) AND (`HasKeyword(Daedra) == 1` OR `Undead` OR `Dwarven` OR `IsUndead == 1`): the Master
  of the Mind perk lets Fear work on the undead; the ordinary `AlchInfluenceConfDown` has the same keywords `== 0`.
- Run-on 2 (Reference) with the reference 0x14 is the player (`GetLevel` of the player on trap effects).

VERIFIED ids: `ActorTypeUndead` 0x13796, `ActorTypeDaedra` 0x13797, `ActorTypeDragon` 0x35D59, `ImmuneParalysis` 0xF23C5 (Skyrim.esm KYWD).
The brief called fn 560 HasMagicEffectKeyword: xEdit names **560 HasKeyword** and **699 HasMagicEffectKeyword** (CommonLibSSE-NG agrees); the data
agrees with xEdit (560 holds a keyword form in 726 conditions, on actor types).

## 2. Design

**Records (`perks.hpp`, `perks.cpp`).** `SpellEffect::mConditionList` and `MagicEffectRecord::mConditionList` hold the decoded CTDAs (first parameter
resolved to the load order for the functions whose first parameter is a form: the perk list plus 699, 66, 719, 136, 444, 264, 47, 58, 56, 310, capped at
256); `mConditions` still counts every CTDA, so a list shorter than the count (a damaged CTDA) cannot be evaluated. `MagicEffectRecord::mKeywords` reads
the MGEF's KWDA (HasMagicEffectKeyword).

**Evaluation (`magic_conditions.*`, new).** `effectconditions::Evaluator` evaluates a list over up to three actors (target = Subject, caster = run-on 1,
player = run-on 2 with the reference 0x14) through the conditions engine of task 0122 and the perk participant of task 0179. It **answers** 560, 448,
715 (the keyword ActorTypeUndead, RECALLED-VERIFY that this is the game's test), 72, 71, 69, 70, 80, 14, 277, 640 (health, magicka and stamina from the
fractions, any other value reads as full), 597, 682, 699 and 214 (the effects running on the actor, `HasMagicEffectKeyword` through the MGEF's keywords),
289, 46, 77 (seeded: splitmix64 of the cast's ids and a counter, no clock), 244 (always 0: nothing here restrains an actor), 700 (always 0: nothing
commands one) and 724 EffectWasDualCast (always 0: there is no dual casting; so Firebolt's Impact stagger is refused, as the game refuses it for a
single cast). That is **1,824 of the 2,691 conditions (67.8 percent)**. Everything else is UNANSWERED: the list fails closed (the effect does nothing),
the function is traced once. The biggest unanswered: 74 GetGlobalValue 333 (13 distinct records, shout words, the Vampire sun damage, quest fog effects),
300 IsInInterior 132 (the Detect Life family), 310 GetInWorldspace 118, 722 61 (armour set bonuses), 719 36, 365 23, 629 22.
Unanswered is not Fail: the verdict is `Verdict::Unanswered`, counted apart.

**Combat (`combat_effect_conditions.cpp`, new; three hook calls in `combat_magic.cpp`).** `SkyrimCombat::effectConditionsAllow(caster, target, effect,
record)` at the three places that skipped an effect with conditions: `applySpellEffects` (a thrown, touched or coated effect), the concentration
self-effects, and a beam's tick. No conditions, or the override setting: true (nothing is evaluated). Counts that do not match the lists (the effect has
CTDAs the pack never decoded, as in the tests of task 0186): skipped as before, traced `cannot evaluate`. Otherwise the effect's own list and the MGEF's
list must both hold; a refusal is traced `refused: its conditions fail` or `refused: a condition cannot be answered here`. Participants are the
perk engine's `actorParticipant` plus the fighter's death, combat state (`mCombatUntil`), vital fractions and the MGEFs running on it (a scratch vector
that keeps its capacity: no allocation after warm-up; the strings are built only when a trace is set). Powers (`applyPowerEffects`) keep applying all
their effects, as in task 0228; `powerSupportOf` is unchanged.

`combat test spell conditions` stays as an override (the 0186 tests use it).

## 3. Out of scope
Unanswered functions above (GetGlobalValue, IsInInterior, GetInWorldspace, WornApparelHasKeywordCount, IsHostileToActor, IsChild, quest functions);
conditions on ENCH effects (90) and ALCH effects (1): the poison path builds its `SpellRecord` from the item's effects and carries no CTDAs, only the
MGEF's (which is where every poison condition is: `AlchParalysis`, `AlchInfluenceConfDown`, `AlchInfluenceAggUp`), and an enchantment reaches combat
only through the damage-on-Health path; a cloak or aura spell (`FireDamageConcAimedCloak`); dual casting; the player as a target of
paralysis, fear or frenzy (task 0233); GetRestrained and IsCommandedActor are constants; calm.

## Stage log

2026-11-16; same hand wrote tests and code; no fresh reviewer; Windows only.

**Tests first, red against stubs.** New `apps/components_tests/skyrimse/testskyrimsemagicconditions.cpp`, 34 tests in five suites: `EffectCondEval` 18 (real
record shapes: AlchParalysis, AlchInfluenceConfDown, PerkImpactStaggerPush, PerkMasterMind, run-on 1, player reference, OR groups, unanswered, random seed,
running effects), `EffectCondRecords` 3 (SPEL and MGEF decode: resolved parameters, a damaged CTDA counted and not listed, KWDA), `EffectCondCombat` 11
and `EffectCondCombatRuns` 1 (the combat runtime through the contract: Paralyze lands on an ordinary actor with no setting, refused on a dragon and on an
`ImmuneParalysis` actor, Fear refused on the undead, a caster-perk gate both ways, an unanswerable condition, counted-but-unlisted conditions skipped, the
spell's own effect list, the override, three equal traces), and `EffectCondRealData` 1 (gated, the five masters).
With a stub `magic_conditions.cpp` (every function unanswered) and a stub `effectConditionsAllow` (the old rule), the 33 non-gated tests gave
**29 failed and 4 passed** (the 4 that passed test absence or the old behaviour: no target, the damaged-condition record, the override, an effect with no
conditions); the gated test was written after the code. After the code 34 of 34 pass.

**Real data** (gated test `EffectCondRealData`, the five masters): 2,264 spell effects with an MGEF; **919 have conditions, all 919 fully listed (no CTDA
damaged), 629 (68 percent) answerable**; the real `Paralyze` effect 0 passes on an ordinary actor and fails on a dragon, `Fear` effect 0 passes on an
ordinary actor and fails on an undead, and Firebolt's damage passes while its Impact stagger and Intense Flames fear are refused (no dual cast, no perk).

**Existing tests:** `components-tests` `*Skyrim*:*Combat*:*Poison*:*Magic*:*Craft*:*Perk*:*Status*:*Papyrus*:*Neutral*:*Extension*:*Frozen*:*Condition*:*Ui*`
plus the new ones: 3,680 tests, **3,604 pass, 0 fail**, 76 skipped (the gated real-data tests). **No existing test edited. No test pins the old "conditions
do not apply" behaviour in a way that this breaks:** the two tests that use `setTestSpellConditions(true)` still apply everything, and the tests that set only
`mConditions = 2` with no list still skip the effect (the "counted, not listed" rule above is kept for exactly that reason).

**In game** (`tools/skybenches_check` through `I:\Projects\OpenEden-work\combatai\eff.sh`; Bannered Mare, Hulda; fixed frame time; the REAL attack key through
`OPENEDEN_RMLUI_INPUT_SCRIPT`; `[Sound] master volume = 0.0`; **no `combat test spell conditions`**):
- **Paralysis** (`Paralyze05`, coated by the inventory binding, one real tap): `COMBAT effect AlchParalysis conditions hold on actor 108142 (caster player)`,
  `effect paralysis on actor 108142 for 15.000 s`, `paralysis ended`; frames (`evidence/effcond_paralysis_sheet.png`): she is hit, folds behind the bar and stays down.
  **Three runs: the 4 `COMBAT effect|poison|effect conditions` lines have the same md5 prefix `1384a6f6645c` in all three.**
- **Fear** (`Fear03`): `AlchInfluenceConfDown conditions hold`, `effect demoralize ... 30 s`, `effect fear`, `ai mode Duel -> Retreat`, ended, `Retreat -> Advance`.
- **Frenzy** (`Frenzy03`): `AlchInfluenceAggUp conditions hold`, `effect frenzy ... 10 s`, ended.
- **Firebolt (`combat test player spell = Firebolt`, 41 magicka)**: `FireDamageFFAimed ... magnitude -25.000 instant` (no conditions), then
  `PerkImpactStaggerPushFFAimed refused: its conditions fail on actor 108142` and `PerkIntenseFlamesConfDownFFAimed refused: its conditions fail`: the conditioned
  effects are correctly refused. Three runs, the 12 `COMBAT effect|spell|effect conditions` lines, md5 `656d041cc81f` in all three.
- **Not shown in game: an undead or a dragon refusing an effect.** The Mare has none, and a draugr needs `--start DustmansCairn02`: the nearest placed draugr was
  found and the player was moved to it, but the player ended 640 units below it in empty space (the cell's collision was not loaded under that spot) and no
  blow landed; not pursued. The refusal on `ImmuneParalysis`, a dragon and an undead is covered by `EffectCondCombat` and `EffectCondEval` on the real records'
  list shapes and by the gated test on the real `Paralyze` and `Fear`. A real Fear or Paralyze SPELL cast was not run: the stand-in player has 100 magicka and they cost
  153 and 450; the poisons use the same MGEF lists.
- The default trace lines per cast: `conditions hold on ...` or `refused ...`.

**Morrowind unchanged**, against `combat5`'s build (the newest executable on the machine, NOT a build of the exact parent `873cd6c130`): see the changelog row.

**Edits outside the new files.** Pack only, all hook calls or additive: `perks.hpp` (`SpellEffect::mConditionList`, `MagicEffectRecord::mConditionList` and `mKeywords`,
comments), `perks.cpp` (`effectParameterIsForm`, `pushEffectCondition`, the SPEL and MGEF CTDA cases, MGEF KWDA), `combat.hpp` (the include, `effectConditionsAllow`,
`effectParticipant`, members and three read accessors, a comment), `combat_magic.cpp` (three call sites of `effectConditionsAllow` replacing the three skips),
`magic_status.hpp` (a comment), `game_packs/skyrimse/CMakeLists.txt` (2 sources in each of 2 lists, 1 header), `apps/components_tests/CMakeLists.txt`
(2 pack sources, 1 test), new `tools/skyrimse_effect_conditions_survey/{survey,dump,examples}.py`. **No virtual added to `CombatRequestWriter`; no SDK, engine or
other pack file touched; no existing test edited.** Scrolls (another agent): a SCRL decoded through `decodeSpell` gets its effect conditions for free; nothing
here depends on or changes the scroll path.

**Not run:** the Linux gate and GCC `-Werror` (written against the GCC traps by hand), clang-format 14, clang-tidy, the Lua tests, `openmw-tests`, a fresh
review, an undead or dragon refusal in game, a real Fear or Paralyze spell in game, a cloak or area spell, IsHostileToActor and the other unanswered functions.

## Decisions for toddhow
1. **Unanswered fails closed.** About a third of the effect conditions (GetGlobalValue, IsInInterior, IsInWorldspace, armour set bonuses, IsHostileToActor, IsChild) make
   their effect do nothing. The alternative is to treat them as true; that would let the Vampire's sun damage and the Detect Life effects apply everywhere. I kept
   closed. Which of the unanswered ones do you want first (GetGlobalValue needs a globals source in combat; IsInInterior needs the cell kind)?
2. **GetRestrained and IsCommandedActor are 0.** A paralysed actor therefore can be paralysed again (the duration extends, as in task 0233). If the game's
   "restrained" includes a paralysed actor, the second poison would be refused instead.
3. **IsUndead is the keyword ActorTypeUndead**, the only undead marker that is VERIFIED in the data; the game's own test may also read an actor flag.
4. **Powers still apply all their effects** (task 0228): the conditions on a Battle Cry's effects are not asked. Say if you want them evaluated too.
