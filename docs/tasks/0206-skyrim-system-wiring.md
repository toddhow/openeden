# Task: connect the hooks the overnight Skyrim systems left open (task 0206)

Status: BUILT (Windows only, local branch `wiring` from `5a65583408`, not pushed); stage log at the end.
toddhow asked for as much Skyrim work as possible overnight (2026-11-16).
Design doc section: `docs/design/skyrim-se-implementation-plan.md` (M-S6, M-S7); tasks 0198 (Papyrus natives), 0199
(crafting), 0200 (skills and levelling), 0189 (barter), 0171 (combat damage report).
Goal: six hooks that tasks 0198 to 0200 left unconnected are connected, each with tests.
Inherited code touched: `apps/openmw/mwworld/worldimp.cpp` (the neutral player hooks lambda), `apps/openmw/mwphysics/physicssystem.{hpp,cpp}`
(`NeutralPlayerDamageFn`, `NeutralCombatWorldImpl::applyPlayerDamage`); no other engine file unless the key press check
(item 6) finds a fault.
Constraints: every change sits behind the settings that already guard the systems (`[extension.skyrimse] player progression`,
`crafting`, `papyrus`, `[Game] neutral combat`); with them off nothing changes. No existing test is edited. Edits in
`papyrus_host*` and `combat*` are not needed (the work is in the pack's glue, `crafting*`, `progression*` and the engine's
player hook).

## 1. Survey (read at `5a65583408`; Skyrim SE masters, aggregates)

- **Crafting's skill hook.** `CraftingHooks::mSkillUsed(name, amount)` is called after every success (`crafting_service.cpp`:
  brew 530, craft 579, temper 631, enchant 662, disenchant 707, names `alchemy`, `smithing`, `enchanting`). The pack
  (`startCrafting`, `skyrimse_game_pack.cpp`) logs it and calls `mCraftingSkillSink`, which nothing sets.
- **Crafting's skill read.** `CraftingHooks::mSkill(name)` is the pack setting `crafting <name> skill` (15 each).
- **Progression.** `ProgressionService::skillUsed(slot, amount)` (ignored before ready) and `actorValue(index, out)`
  (skills 6 to 23); built in `startProgression`, which runs BEFORE `startCrafting`, so `mProgression` is known there. Slots:
  Smithing 4, Alchemy 10, Enchanting 17.
- **Papyrus.** `GetActorValue` / `GetBaseActorValue` (`papyrus_host.cpp` `registerActorNatives`) ask the pack's environment
  `env.actorValue(actor, lowerName, current, maximum)` (`wirePapyrusWorld`, `skyrimse_game_pack.cpp` line 843), which answers
  `health`, `magicka` and `stamina` only; the name is the script's string, lower case, spaces and underscores removed
  (`avKey`), so `OneHanded` is `onehanded`, `Marksman` is `marksman` (archery), `Speechcraft` is `speechcraft`. A name the
  environment does not know reads 0.
- **Recipe conditions.** 601 COBJ in `Skyrim.esm`, 931 CTDA. Per bench keyword (function, compare, count): forge
  (`0x88105`): fn 448 `HasPerk` == 1, 119 (the perks `SteelSmithing` 21, `DwarvenSmithing`, `ElvenSmithing`, `OrcishSmithing`,
  `GlassSmithing`, `EbonySmithing`, `DaedricSmithing` 13 each, `AdvancedArmors` 9, `DragonArmor` 10); Skyforge (`0xf46ce`):
  fn 74 `GetGlobalValue`, 9; sharpening wheel (`0x88108`): ALL 158 recipes hold `fn 659 != 1` OR `HasPerk(ArcaneBlacksmith) == 1`
  (type bytes `0x21` and `0x01`: the OR flag; fn 659 is the tempering test "the item being improved is enchanted", the
  name `EPTemperingItemIsEnchanted` is RECALLED-VERIFY); armour table (`0xadb78`): the same pair on 226 recipes, plus 12
  `GetStageDone`, 8 fn 543 and 1 fn 47. So **tempering a plain item needs no perk, an enchanted item needs Arcane
  Blacksmith**, and a forge recipe for a better metal needs its smithing perk.
  Today `Recipe` keeps only a count of CTDA (`mConditions`), `recipesFor` hides non-tempering recipes with any condition
  (unless `crafting ignore conditions`) and tempering is never gated.
- **Perks module.** `PerkConditionWorld` + `conditions::Evaluator` answer `HasPerk` (fn 448) and `GetBaseActorValue` (277) from
  a `PerkParticipant`; `PlayerProgression::canBuyPerk` is the model (`hasPerkSet()`, `actorValues()`, `level()`). Fn 659
  is unknown to the global table (it would fail a list closed) but `Evaluator::setExtraFunctions` adds a function for one
  evaluator only (the perks module does this for fn 568 and others).
- **Health.** `SkyrimCombat::syncProgression` (`combat_skills.cpp`) raises the combat profile's maximum and writes a zero-damage
  report with `healthMax = max(engine max, profile max)`; the bridge (`neutralcombatbridge.cpp doReportDamage`) clamps
  `healthAfter` to the report's `healthMax` and calls `NeutralCombatWorld::applyPlayerDamage(report)`;
  `PhysicsSystem::NeutralCombatWorldImpl::applyPlayerDamage` passes ONLY `report.healthAfter` to the engine hook, which
  (`worldimp.cpp`, `setNeutralPlayerHooks`) does `health.setCurrent(clamp(healthAfter, 0, health.getModified()))`. The
  engine's `DynamicStat<float>` base (and so the HUD bar, `MWGui::HUD::setValue` reads `getModified()`) is never touched, so
  a level up that adds 10 health leaves a bar whose maximum stays 100 and a current value clamped to it.
  The engine shows magicka and fatigue too (`CreatureStats::getMagicka`, `getFatigue`), but no neutral-combat code writes
  or reads them for the player (the pack keeps the player's stamina and magicka itself and the pack's HUD package carries
  them), so there is nothing to follow for those two.
- **Benches.** `Neutral::activate` (`neutral.cpp`) returns `OpenInventoryAction(..., "bench")` for a Furniture activated by
  the player when `[Game] neutral inventory` is on (`decideNeutralBench`); the action sends the extension event
  `openeden_skyrimse_bench_opened`; `crafting_global.lua` opens the session. Task 0199 only proved the Lua half, with the
  test setting `crafting test bench` (`open_kind`).

## 2. Design

1. **Crafting trains skills.** `crafting::skillSlotOfCraftingName(name)` (alchemy 10, smithing 4, enchanting 17, else
   `kSkillCount`) and, in the pack, `mSkillUsed` calls `mProgression->skillUsed(slot, amount * boost)` when the progression
   is on (the same `progression test amount` multiplier combat's hook uses; a member `mProgressionBoost`), before the old
   sink. The accessor `SkyrimGamePack::progressionService()` hands the `ProgressionService` to other glue (kept general:
   `ProgressionService*`, null with the setting off).
2. **Real skills for crafting.** `hooks.mSkill` reads `ProgressionService::actorValue(kFirstSkillValue + slot, v)` when the
   service is ready; otherwise the settings, as before.
3. **Papyrus skills.** `progression::slotOfActorValueName(key)` (the lower case key, with `marksman` and `archery`, `speech`
   and `speechcraft` both) and the player branch of `env.actorValue` answers current = maximum = the real skill when the
   progression is ready. Every other name and every non-player actor are unchanged. (Task text says "skills 6 to 23": a
   script passes names, the index is the AVIF order that `actorValue(index)` uses.)
4. **Perk-gated recipes.** `Recipe::mConditionList` (decoded CTDA, form parameters resolved through the load order);
   `crafting::recipeConditionsHold(list, perks, level, values, itemEnchanted)` evaluates with the perks module and
   an extra function 659 (the flag); a hook `CraftingHooks::mRecipeConditions` (set by the pack only with the progression
   on). A recipe is gated this way only when every condition is function 448, 659 or 277 (what the data holds for forge
   and tempering); any other function (Skyforge's global, quest stages) keeps today's rule (forge: hidden, tempering: shown).
   The same test hides a recipe from the list and refuses `craft`/`temper` (`UnknownRecipe`). For a tempering recipe the
   flag is "the item that would be improved is enchanted". `crafting ignore conditions` still overrides everything.
5. **Health maximum.** `NeutralPlayerDamageFn` gets the report's maximum as a second argument; a pure helper
   `MWPhysics::neutralPlayerHealthWrite(base, modifier, current, healthAfter, healthMax)` decides the numbers the engine
   stat takes (base raised or lowered so `getModified()` equals the pack's maximum; never outside [0, max]; a report with no
   maximum changes only the current value, as before). Magicka and stamina: not shown by the engine for this player and
   not written by the pack: nothing to do.
6. **Benches by key.** Verify in game with the real activate key (`OPENEDEN_RMLUI_INPUT_SCRIPT`), fix what fails (nothing needed fixing; see the stage log).

## 3. Stages

1. This document. 2. Tests first (new files only), run red against stubs. 3. Code for 1 to 5. 4. Game: crafting raises
Alchemy in the skills window, the health bar maximum after a level up, the real key on a bench; three equal runs; setting
off equals the parent; Morrowind unchanged. 5. Changelog row.

## Out of scope
Perk effects of crafting (Alchemist, Steel Smithing multipliers: entry points are the perks module's; crafting does not
call them yet), the experience formulas themselves, other perks' recipe conditions that need an item or quest answer.

## Stage log

2026-11-16; same hand wrote tests and code; no fresh reviewer; Windows only; branch `wiring` from `5a65583408`; not pushed.

**Built.** Pack: `progression::slotOfActorValueName`, `progression::skillActorValue(service, key, out)`,
`crafting::skillSlotOfCraftingName`, `crafting::recipeConditionsPerkOnly`, `crafting::recipeConditionsHold` (perks module:
`PerkConditionWorld` + `Evaluator` with the extra function 659 through a thread-local flag), `Recipe::mConditionList` (decoded
CTDA, form parameter of fns 448, 74, 58, 59 resolved), `CraftingHooks::mRecipeConditions`, `crafting::linkProgression` (wraps
`mSkill` and `mSkillUsed`, installs `mRecipeConditions`, caches perk set and values by `PlayerProgression::revision()`),
`CraftingService::conditionsAllow` (used by the recipe list, `craft` and `temper`) and `visibleRecipes()` made public;
`SkyrimGamePack::progressionService()`, `mProgressionBoost` (the `progression test amount` multiplier now also scales what a
craft trains), the player branch of the Papyrus `env.actorValue`. Engine: `MWPhysics::neutralPlayerHealthWrite`
(`neutralplayerhealth.hpp`, pure), `NeutralPlayerDamageFn` takes `(healthAfter, healthMax)`, `applyPlayerDamage` passes the
report's maximum, the `worldimp.cpp` lambda sets the stat's base so `getModified()` equals the pack's maximum (one
`NCOMBAT player health maximum A -> B` line per change). Tools: `tools/skyrimcraft_check` places the player at a base
(`place.lua` was a stub for that), tilts the view (`pitch.lua`, `PITCH`, `PITCHFRAME`) and prints the recipe count of a bench.

**Tests first, red against stubs.** New `apps/components_tests/skyrimse/testskyrimsewiring.cpp` (19 tests) and
`apps/openmw_tests/mwphysics/testneutralplayerhealth.cpp` (8). Against stubs (functions returning "none", "true", a no-op
`linkProgression`, a health write that never changes the maximum): **14 of 19 and 6 of 8 failed**; the 5 and 2 that passed pin
the old behaviour (no hook keeps the old rule, a global condition stays hidden, `crafting ignore conditions`, a progression that
is not ready, a report with no maximum, the same maximum). After the code all 27 pass. **One of my own tests was corrected
after it failed for a test mistake, not a code fault:** the progression rig used use mult 1, so a craft's amount (1,259 for a
test potion) rolled the skill over and the experience was not linear; it is 0.01 now and the two assertions compare
`0.01 * amount`. No existing test is edited. Full runs: `components-tests` without the Slang, complement and property groups
11,429 pass; `openmw-tests` without the Slang, Papyrus and ObScript groups 2,764 pass.

**Real data (Skyrim.esm COBJ walk, Python, aggregates).** Condition functions per bench keyword: forge fn 448 x119, Skyforge fn
74 x9, wheel fn 448 x158 + fn 659 x158, armour table 448 x226, 659 x226, 59 x12, 543 x8, 47 x1; the 448 perks are `SteelSmithing`
21, the metal perks 13 or 14 each, `AdvancedArmors` 9, `DragonArmor` 10 for the forge and `ArcaneBlacksmith` for ALL 384
tempering recipes, whose pair is `fn 659 != 1` OR `HasPerk(ArcaneBlacksmith)` (type bytes 0x21, then 0x00 or 0x01).

**In game** (Bannered Mare for crafting and combat, Dragonsreach for the key; every run with `OPENEDEN_QUIET_WINDOW=1` and
sound off, `OPENEDEN_FIXED_FRAME_TIME`; runs and frames in `I:\Projects\OpenEden-work\ragdoll5\wire`):

1. **Crafting raises Alchemy.** `progression test amount = 40`, test bench alchemy, Wheat (eaten) and Blue Mountain Flower
   brewed with real clicks: the skills window (key K) shows `Alchemy 15 (0 / 379)` before and `Alchemy 16 (100 / 421)` after,
   character experience `0 / 100` to `16 / 100`; log `PROGRESSION skill Alchemy 15 -> 16 (level xp 16 of 100)` after
   `CRAFT skill used: alchemy +16`. Frames `r_prog_alch1/user/screenshots/screenshot000.png` and `screenshot004.png`. Three runs:
   29 identical trace lines.
2. **Health maximum.** `progression test amount = 400`: one brew raises Alchemy 15 -> 23 and the character experience to 156
   of 100, K, `Health +10` (real click), Hulda hostile 100 units away. New build: `NCOMBAT player health maximum 100 -> 110`,
   then every hit reads `health after 92.065 of 110`, `84.295 of 110`, ...; the build before (same pack, parent engine): `... of
   100` for every hit. The engine HUD's red bar after the level up (frame 1450): ends at x 223 (99.8 of 110, 90.5 percent) with
   the fix and at 243 (full: 99.8 clamped to 100) without (`r_hq_new` and `r_hq_parent`, `screenshot003.png`). Three runs of
   the new build: 120 identical trace lines. **Magicka and stamina:** the engine's blue and green bars are on screen and never
   move (the pack keeps those two itself and writes nothing to the engine's stats), so there is nothing to follow; decision
   for toddhow below.
3. **Perk-gated forge.** Forge bench, progression on, no perk: 47 recipes (the same with the progression off, and 241 with
   `crafting ignore conditions`). Level up (Health), `Steel Smithing` bought with a click in the skills window, then the
   forge: 81 recipes. Three runs identical. Sharpening wheel: 204 recipes with and without the progression (a player with no
   weapon: fn 659 is 0, so the pair holds).
4. **Bench by the real key.** `OPENEDEN_ACTIVATE_TRACE`: `ACTKEY focus: "skyrimse:b765196" name 'Alchemy Lab' tooltip 1
   distance 123.5`, `NINV bench 0010d982 base 000bad0c 'Alchemy Lab' opened`, `CRAFT open Alchemy Lab (alchemy)`; the
   alchemy window is on screen (`r_benchp_0.5/user/screenshots/screenshot001.png`). The engine path of task 0199 was right and
   needed no fix: 0199's placements looked level at a table lower than the eye, so the focus ray passed over it. With the view
   tilted down by 0.5 radians (a player script calling `camera.setPitch`) from the west side of the lab it focuses at once.
   Three runs identical.
5. **Setting off equals the parent.** Progression off: the alchemy trace (`CRAFT`, `INV`, `N6INV`) of 2 runs of the new build
   equals the parent's byte for byte; the one-sided "Hulda hits the player" combat trace (178 lines) of 2 new runs equals the
   parent's. Morrowind: `tools/worldobjects_check` 110 lines OK and the fixed Balmora frame `1c5d47ad0b5dc32d` for the parent
   executable and the new build (also with the neutral combat settings on).

**Not run or not built.** The Linux gate and GCC `-Werror`, clang-format 14, clang-tidy, the Lua tests, a fresh review, mutation
checks. **Papyrus `GetActorValue` and `GetBaseActorValue` in game:** no script was run (it needs a generated `.pex`); the pack's
environment branch is a few lines over `skillActorValue`, which is unit tested, and the host's clamp rule (`avNow`) was read, not
tested at host level. Not done: smithing and alchemy perk effects, other perks' recipe conditions (a recipe whose list holds a
function other than 448, 659, 277 keeps the old rule), the engine's magicka and stamina bars, a host-level Papyrus test.

## Decisions for toddhow
1. **Tempering now needs Arcane Blacksmith for an enchanted item** (fn 659); before, nothing gated it. Enchanted gear to
   improve is not offered until the perk is bought; plain gear is as before. Only with the progression on.
2. **Forge recipes need their smithing perk** when the progression is on: a new character sees 47 forge recipes (of 241) until it
   buys Steel Smithing and the others. The old rule hid all of them unless `crafting ignore conditions`, so this only unlocks.
   Skyforge (global value) and quest-stage recipes stay hidden.
3. **Papyrus skills answer with a maximum of 0** so that `ModActorValue` on a skill is kept. The names are RECALLED-VERIFY.
4. **The engine's health maximum follows the pack's on every report that carries one.** With the progression off the report's
   maximum is the engine's own, so nothing changes (equal traces). Magicka and stamina bars of the engine are not followed.
5. **`progression test amount` now also scales what crafting trains** (a developer aid).
6. **A thread-local flag carries fn 659's answer into the evaluator** (the function table holds plain function pointers); it
   is set around one evaluation on the main thread.
