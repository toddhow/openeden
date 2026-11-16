# Task: Skyrim smelter, tanning rack, cooking pot and weapon poisons (milestone M-S7; follow-up of task 0199)

Status: BUILT, Windows only, local branch `benches2` from `102240fda1`, not pushed. See the stage log at the end.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.17; tasks 0199 (crafting), 0186 (magic runtime), 0184 (inventory use),
0224 (mining gives the ore the smelter uses), 0206 (`linkProgression`).
Goal: the three benches task 0199 left untried in game work end to end, and a poison (an ALCH with a hostile first effect, or a potion
brewed at an alchemy lab) can be put on the weapon in hand from the inventory window and is applied to the target of the next landed blow.
Inherited code touched: none outside the pack except the one hook line in `SkyrimCombat::applyHit` (`combat.cpp`).
Constraints: behind the existing `[extension.skyrimse] crafting` setting (and `inventory`); no new setting; with them off nothing changes.
No existing test edited. No virtual added to `CombatRequestWriter`. Every formula we invented is named and tunable (`CraftingSettings`).

## 1. Survey (Skyrim.esm, Python walker, aggregates)

- Keywords (VERIFIED): `CraftingSmelter` 0xa5cce, `CraftingCookpot` 0xa5cb3, `CraftingTanningRack` 0x7866a. COBJ per bench: smelter 16, tanning rack 15,
  cook pot 18. `BenchKind::{Smelter, Cookpot, TanningRack}` and `recipesFor(kind)` already existed (task 0199), so craft and the window needed
  no new code for these benches.
- Smelter recipes: 1 ore -> 1 ingot (iron), 2 ore -> 1 ingot (silver, gold, ...), corundum + iron -> steel, dwarven scrap -> dwarven ingot. No conditions.
- Tanning rack: 15 recipes, 14 hides and pelts -> `Leather01`, `Leather01` -> `LeatherStrips`; **14 of the 15 carry one CTDA: function 47 (GetItemCount),
  type byte 0x60 (operator 3, `>=`), value 1.0, parameter 1 the pelt** (the item the recipe consumes). Before this task `conditionsAllow` hid
  every non-tempering recipe that had a condition, so the whole tanning rack showed only "Leather Strips".
- Cook pot: ALCH foods. Cooked meat is `FoodRestoreHealth` (instant, duration 0); stews add stamina or timed effects. The existing use path
  (`InventoryService::use`, task 0184) applies an instant Health restore only.
- Furniture: smelter marker `CraftingSmelterMarker1` 0x9c6ce, tanning `CraftingTanningRackMarker` 0x727a1, cook pots `CraftingCookingPot*` 0x1010b3,
  0x104110, 0x108230, 0x10bfe3 and the fire spit `CraftingCookingFireSpit` 0x68adb. Whiterun: a smelter (0x10e391, 20018,-8524,-3577), a tanning rack
  (0x2056a, 20384,-8054,-3620) and a forge in the open beside the Skyforge steps; cook pots in Breezehome, Belethor's, the Mare (spit).
- Poisons: 79 ALCH with the potion flag 0x20000. There is **no GMST for the number of doses** (only the messages `sPoisonAlreadyPoisonedMessage`,
  `sPoisonNoWeaponMessage`, `sPoisonBowConfirmMessage`). Their effect archetypes (MGEF DATA +64) and values, counted over those 79 potions' effects:
  ValueModifier (0) on a vital (Damage Health, Magicka, Stamina, and the `...Duration` kinds) 34; ValueModifier on a regeneration rate (Damage
  Magicka Rate 5, Damage Stamina Rate 4); Dual (5) 1 (Frostbite Venom); Demoralize (7, fear) 5; archetype 8 (Frenzy) 5; archetype 21 (Paralysis) 5;
  Peak (34) weakness to fire, shock, frost and magic 20.

## 2. Design

**Conditions (pure, `crafting.*`).** `recipeConditionsItemCountOnly(list)` (every condition is fn 47) and `itemCountConditionsHold(list, held)`
(the six operators; an OR chain or a global value fails closed). `CraftingService::conditionsAllow` uses them: a recipe whose conditions are
all GetItemCount holds while the player holds the item (it is then offered, as in the game); any other list keeps the old rule.

**Experience.** `craft` trains `smithing` by the value of what was made for the forge, the Skyrim forge, the smelter and the tanning rack;
the cooking pot trains nothing (RECALLED-VERIFY: Skyrim gives cooking no skill experience). The training goes through `mHooks.mSkillUsed`, so
with `player progression` on `linkProgression` (task 0206) raises the real Smithing.

**Poisons.**
- `poison.*` (pure): `WeaponPoison` (the item, its name, its effects, charges), `PoisonCoatings` (weapon item -> coating, at most 64, one coating
  per weapon: a second poison is refused, as the game's message says), `poisonSpell(...)` (a touch, fire-and-forget `SpellRecord` named after the poison).
- `CraftingService::usePoison(item, weapon, melee)`: an item is a poison when it is a Potion in the item table whose FIRST effect is hostile
  (an ALCH of the game or one made at an alchemy lab); results `NotPoison`, `NoWeapon`, `NotMelee` (a bow, a crossbow, a staff or bare hands),
  `AlreadyPoisoned`, `Applied`. It touches no inventory; `InventoryService::use` consumes the item on a positive answer through the new hook
  `Hooks::mUsePoison` (0 not a poison, 1 applied, -1 refused). Charges: `CraftingSettings::mPoisonCharges` = 1 (OURS).
- The pack (`skyrimse_game_pack.cpp`): `usePoisonOnWeapon`, `wieldedWeapon`, `syncPoison` (after every equipment sync and after an application) give the
  combat the poison of the wielded MELEE weapon, and the spent hook counts the charge down in the crafting service.
- Combat (`combat_poison.cpp`): `SkyrimCombat::setPlayerPoison(spell, charges)`, `playerPoisonCharges()`, `setPoisonSpentHook`. **The only edit in
  `combat.cpp` is one hook call in `applyHit`:** `if (!blocked && !bash) applyPoisonOnHit(...)` after the sound. `applyPoisonOnHit` runs only for the
  PLAYER attacker, spends a charge (whether or not the blow kills), tells the hook, and, if the victim lives, calls the existing
  `applySpellEffects` of the magic runtime (task 0186): resisted by the target's resistance, held by a ward, ticked per second by `tickEffects`.

**Effect archetypes that work as a poison.** Through `applyOneEffect`: ValueModifier, Dual and Absorb on Health, Magicka and Stamina (instant, or per
second for a duration: Damage Health, Damage Magicka, Damage Stamina and the `...Duration` kinds, Frostbite Venom); Demoralize (fear) is
kept as a status for its duration; Stagger staggers an NPC. A ValueModifier on a value the pack does not track (damage regeneration rates, weakness to
fire, shock, frost and magic, which are Peak modifiers on resistances) is a **status only** (it is listed and expires, nothing reads it). **Not
supported** (traced "archetype N is not supported"): Paralysis (21) and Frenzy (8). Effects that carry conditions are skipped unless
`combat test spell conditions` is set.

## 3. Out of scope
Poisons on arrows and bolts, an NPC using poison, poison coatings saved in a save (the coating is lost on a load, the item is consumed), the
Poisoner and Concentrated Poison perks, a poison's effect on resistances, stamina or magicka foods (the use path restores Health only), the
cooking pot's food effects over time, the Skyforge's and every bench's perk gates.

## Stage log
2026-11-16; same hand wrote tests and code; no fresh reviewer; Windows only.

**Tests first, red against stubs.** The new tests are `apps/components_tests/skyrimse/testskyrimsebenches.cpp` (condition rule 3, benches and
poison service 15) and `testskyrimsepoison.cpp` (combat 9, coatings 5, spell 1): 33 tests. With headers and no-op stubs of every new function,
**22 of the 33 failed** (the condition rule 3 of 3, the tanning rack 2 of 2, the cooking pot 1 of 3, the poison service 5 of 6, the smelter and forge 0 of 4,
combat poison 5 of 9, coatings 5 of 5, the spell 1 of 1). The 11 that passed against the stubs test absence (an unknown item is not a poison, no charge means no effect,
a killing blow leaves no effect on the dead, a status on an untracked value) or behaviour task 0199 already built (the smelter's and the cook
pot's craft, eating the food, the forge's skill). After the code: 33 of 33 pass, and the 693 selected crafting, combat, inventory, magic,
poison and bench tests pass (9 gated real-data tests skip). **Existing tests edited: none.**

**In game** (`tools/skybenches_check`, a copy of `tools/skyrimcraft_check`; fixed frame time; Windows; real activate key through the engine;
real clicks through `OPENEDEN_RMLUI_INPUT_SCRIPT`; frames viewed):
- **Tanning rack** (Whiterun, 20384,-8054): the real key opens `Tanning Rack`; with 2 Deer Hide the window offers `Leather` (the GetItemCount
  condition holds) and `Leather Strips (cannot)`; a real click on Leather: `CRAFT craft Leather: ok`, `skill used: smithing +10`; the hide count 2 to 1.
  The same window before the condition fix offered only Strips.
- **Smelter** (Whiterun, 20018,-8524): the real key opens `Smelter` (16 recipes); with 4 Iron Ore a real click on `Iron Ingot`: `craft Iron Ingot: ok`,
  `smithing +7`, inventory `Iron Ingot x1; Iron Ore x3` (the 0224 mining ore).
- **Cooking pot**: **opened with the test setting `crafting test bench` (the bench kind `cook`)**: the real activate key did not reach a cook pot or
  the fire spit (two cook pots, a spit and the Whiterun interiors were tried: the raycast hits the floor or the stand instead, probably because
  these furniture models have no collision hint; not investigated). With Raw Beef and 2 Salt Pile a real click on `Cooked Beef`: `craft Cooked
  Beef: ok`, no skill used; `inv.use` of the cooked beef: `INV use 000721e8: health +10`.
- **Poison**: Iron Sword wielded, `DefaultPoison` x2 in the inventory; the real `I` key and a real click on the poison's `Use` button:
  `CRAFT poison ... on Iron Sword: applied`, `INV use 0005629e: poison on the weapon`; a real left click (a swing) hits Hulda: `COMBAT poison
  ... on the blow, 0 charges left`, `COMBAT effect AlchDamageHealth player -> actor 108142 magnitude -10.000 a second for 1.000 s`, the blow
  7.525 (75 to 67.475), the poison takes 10 more (to 57.475); the second swing: 7.525 and nothing else.
- Three runs of each scenario (tanning, smelting, cooking, poison) give identical filtered `COMBAT`, `CRAFT`, `INV` and `N6INV` lines (md5 prefixes of the
  9, 9, 4 and 16 lines: `307883726b64`, `da86057e80c2`, `f52e97a71ea5`, `ca2c93096cd6`, three runs each). Frames: `I:\Projects\OpenEden-work\combatai2\evidence`.

**Morrowind unchanged.** `tools/worldobjects_check` against `integ2`'s build (main, not a build of the exact parent): 111 result lines, OK; the fixed Balmora
frame (`--start Balmora --random-seed 7`, `OPENEDEN_FIXED_FRAME_TIME`, frame 600, 1280 x 720) is `0d6dd39995b927fa` for both.

**Edits outside the new files.** `combat.hpp` (three public declarations, one private declaration, three members), `combat.cpp` (one hook call),
`crafting.hpp/.cpp` (one constant, two functions), `crafting_service.hpp/.cpp` (include, `PoisonUse`, three public functions, a member, `conditionsAllow`
branch, the skill rule, `usePoison`), `inventory_service.hpp/.cpp` (one hook, one branch in `use`), `skyrimse_game_pack.cpp` (the hook, three
functions, the spent hook, a member), `game_packs/skyrimse/CMakeLists.txt` (`poison.cpp`, `combat_poison.cpp`, the header; twice for the
combat file), `apps/components_tests/CMakeLists.txt` (four lines). New: `poison.hpp/.cpp`, `combat_poison.cpp`, the two test files, `tools/skybenches_check`.

**Not run:** the Linux gate and GCC `-Werror`, clang-format 14, clang-tidy, the Lua tests, a fresh review, a real key on a cook pot, a poison from
a brewed potion in game, a bow poison, a save and load of a coated weapon, armour or a ward against a poison in game.

## Decisions for toddhow
1. **One charge per poison** (`mPoisonCharges`, ours: the data has no dose count). The Concentrated Poison perk would make it two.
2. **A poison is not saved.** After a load the weapon is clean (the item was consumed). A chunk would be small; it needs a chunk version.
3. **Cooking trains no skill** (RECALLED-VERIFY); the smelter and the tanning rack train Smithing by the value of the result, the forge's rule.
4. **Paralysis and Frenzy poisons do nothing** (archetypes 21 and 8 are not in the magic runtime); weakness and the regeneration-rate poisons are
   status only. Ten of the 79 game poisons are therefore inert.
5. **The cook pot's real key press** needs the furniture to be pickable (a collision hint or an interaction box in the neutral objects).
