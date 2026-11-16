# Task: Skyrim crafting, alchemy, smithing, enchanting (milestone M-S7, plan section 4.17; follow-up of tasks 0182, 0184, 0189)

Status: BUILT, Windows only, local branch `crafting` from `3885a4091b`, not pushed (alchemy, smithing, tempering and enchanting); see the stage log at the end.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.17; tasks 0182 (inventory model), 0184 (equip, use),
0189 (barter, the template for this task), decision 0015 (UI), decision 0017.
Goal: at a crafting furniture the player brews potions from ingredients, forges items from recipes, tempers weapons and
armour, disenchants enchanted gear to learn enchantments and enchants gear with a filled soul gem; results are items in
the pack's inventory model, the combat numbers read them, everything is saved in a pack chunk.
Inherited code touched: `apps/openmw/mwclass/neutral.cpp` (`Neutral::activate`, `Neutral::hasToolTip`: Furniture), the pure
helpers in `apps/openmw/mwworld/neutralinventory.*` (additive); in the pack only additive lines in `inventory.*`
(`ItemStat::mName`, `ItemTable::addSynthetic`), `inventory_service.cpp` (`name` binding), `skyrimse_game_pack.cpp`
(`startCrafting`, `syncEquipment`, the equip hook) and `world_view.*` (one effect-info accessor).
Constraints: behind `[Game] neutral inventory` + `[extension.skyrimse] inventory` + `[GUI] rmlui` and the NEW pack setting
`crafting` (false, declared in `extension.json`). With them off nothing changes. No existing test is edited. The shipped
`openeden.ui` components and themes, `combat*`, `papyrus_host*`, `inventory*` (beyond the additive lines) are not touched.
All formulas are RECALLED-VERIFY and live in `CraftingSettings` (tunable); record layouts are VERIFIED by the data.

## 1. Survey (data = the five Skyrim SE masters; Python walker over `Skyrim.esm`, aggregates only)

- **INGR**: 94 records; DATA 8 (value i32, weight f32), ENIT 8 (value, flags), EFID 4 + EFIT 12 (magnitude f32, area u32,
  duration u32) x 4 each (376 of 376 effect pairs), KWDA. Names are localised FULL (4 bytes).
- **COBJ**: 601 records in `Skyrim.esm`; CNAM 4 (created object; 594 present), NAM1 2 (count), BNAM 4 (the bench
  KEYWORD), COCT 4 + CNTO 8 per item (952 entries), CTDA 32 (931 conditions: perks, held items). BNAM counts: forge
  `CraftingSmithingForge` (0x88105) 159 (WEAP 62, ARMO 96), `CraftingSmithingSkyforge` (0xf46ce) 9, the cook pot, the
  smelter (16) and the tanning rack (15) results are not weapons or armour (cook pot 18); **tempering is a COBJ too**:
  `CraftingSmithingSharpeningWheel` (0x88108) 158 (CNAM = a WEAP, the item improved) and `CraftingSmithingArmorTable`
  (0xadb78) 226 (220 ARMO).
- **Benches are FURN with KWDA**: `CraftingBlackSmithForge` (0xcae0b), `CraftingBlacksmithAnvil` (0x1a2ad) and the
  Skyforge forge carry the forge keyword (the Skyforge one also the Skyforge keyword), `CraftingBlacksmithSharpeningWheel`
  (0x6e9c2) the wheel, `CraftingBlacksmithArmorWorkbench` (0xd932f) the armour table, `CraftingAlchemyWorkbench` (0xbad0c)
  and `...Tabletop` (0xd54ff) `WICraftingAlchemy` (0x4f6e6), `CraftingEnchantingWorkbench` (0xbad0d) and `...Tabletop`
  `WICraftingEnchanting` (0x4f6dd). Placements: Dragonsreach (interior) holds an alchemy lab (0x10d982) and an arcane
  enchanter (0xc6e44), Breezehome an alchemy lab (0xe4c31), Skyforge interior a grindstone (0x4951e); forges and anvils
  are in exteriors.
- **ENCH**: 604 records; ENIT 36 (cost i32, flags u32, cast type u32, amount i32, delivery u32, enchant type u32 (6 =
  enchantment, 12 = staff), charge time f32, base enchantment form, worn restrictions form), EFID/EFIT, FULL. A crafted
  enchantment names a BASE enchantment (`EnchWeaponFireDamage03` -> `EnchWeaponFireDamageBase`, EFIT magnitude 10): disenchanting
  learns the base. WEAP/ARMO carry `EITM` (the enchantment); 100 of the iron weapons have it.
- **SLGM**: 17; DATA 8 (value, weight), SLCP u8 (capacity 1 to 5), SOUL u8 (the soul held); `SoulGemGrandFilled` (0x2e4ff)
  has capacity 5 soul 5.
- **GMSTs** (VERIFIED): `fAlchemySkillFactor` 1.5, `fEnchantingSkillFactor` 1.25, `fEnchantmentPointsMult` 0.12,
  `fEnchantmentEffectPointsMult` 8.0. No smithing GMST exists in the data.
- **Pack side read**: `ItemTable` (`inventory.hpp`) is keyed by form id and holds stat, effects and keywords;
  `InventoryModel::consume/transfer/equip` (task 0184); `InventoryService::use` drinks an ALCH through
  `ItemStat::mEffects` and the `mHealthOf` hook; `syncEquipment` (`skyrimse_game_pack.cpp`) gives the combat rules a
  `const WeaponRecord*` and a vector of `ArmourPiece` by value (so a changed copy is "one read of item quality").
  The container activation path is `Neutral::activate` -> `OpenInventoryAction` -> Lua event (`neutralinventory.*`).

## 2. Design

**Synthetic items.** A brewed potion, a tempered weapon and an enchanted weapon are new items: form ids `0xFF000000 + n`
(the prefix of items made in game; no plugin of a load order uses index 0xFF), counted from 1 in creation order, so the
same actions give the same ids. A definition (kind, name, value, weight, category, effects or base + temper per mille +
enchantment + magnitude) is a canonical key: the same definition returns the same id and stacks merge. The registry is
added to the item table (`ItemTable::addSynthetic`) once the table is built, so every inventory function sees them.

**Pack** (`game_packs/skyrimse/{include/game_skyrimse,src}/crafting.*`, `crafting_service.*`):
- `parseCraftingRecords`: INGR, COBJ, ENCH, SLGM, FURN (bench keywords), KYWD (only the crafting names), WEAP/ARMO EITM, MGEF
  editor ids; `CraftingTable` merges a load order (later wins).
- Rules (pure, `CraftingSettings`, RECALLED-VERIFY): potion = effects shared by at least two chosen ingredients (two or
  three); magnitude = best ingredient magnitude x (1 + (fAlchemySkillFactor - 1) x skill / 100), duration the best; poison
  when the strongest effect is hostile; value from the effects' base cost (cap); weight 0.5. Tempering: improvement
  fraction = `mTemperPerSkill` x skill (0.004: 6 percent at 15, 40 at 100), grades Fine, Superior, Exquisite, Flawless, Epic,
  Legendary by fraction; weapon damage and armour rating x (1 + fraction); a better temper replaces, a worse or equal is
  refused. Enchanting: magnitude = base enchantment magnitude x soul multiplier (size 1 to 5: 0.5, 0.7, 1.0, 1.3, 1.6) x
  (1 + (fEnchantingSkillFactor - 1) x skill / 100); the soul gem is used up; one enchantment per item.
- Knowledge: effects known (eating an ingredient reveals its first effect, brewing reveals every shared effect of the
  potion) and enchantments known (disenchanting reveals the base). Nothing is known at the start; the window shows `?`
  for an unknown effect.
- Service `skyrimse.crafting` 1.0 (bindings, Mutate only from the global script), the window logic in Lua over the
  headless UI backend: alchemy (ingredient list, up to three selected, preview, Brew, Eat), recipes (smithing and
  tempering, with needs and Make), enchanting (items, known enchantments, soul gems, Enchant, Disenchant).
- A hook `mSkillUsed(skill, amount)` is called after each successful craft (alchemy, smithing, enchanting): the skills
  work of another task can listen; the skill the formulas read is `mSkill(name)` (setting `crafting <name> skill`, 15).
- Combat read: `syncEquipment` asks `CraftingService::weaponFor`/`armourFor` so a tempered or enchanted item reaches the
  combat rules as a changed copy (damage x (1 + fraction) plus the damage of a damage-health enchantment; armour rating x
  (1 + fraction)). Applied: damage, armour rating. NOT applied (stored only): every other enchantment effect (fear,
  fortify, resist, absorb, paralyse ...), soul trap, charge use and recharge, elemental resistance of the target.
- Saving: chunk `crafting` v1 (known effects, known enchantments, next number, every synthetic definition with its name).

**Engine** (additive): Furniture gets a tooltip and, when the player activates it with `[Game] neutral inventory` on, the
event `openeden_skyrimse_bench_opened` (placement, base, name; same text-id rule as the container event). One pure decision
function and one field builder in `neutralinventory.*`.

**Lua**: two scripts (global: the Mutate calls; player: the window) and a window module `crafting_windows.lua`.

## 3. Stages
1. This spec. 2. Records and pure rules (tests first, red against stubs). 3. State, synthetic registry and chunk. 4. Service
and bindings over the inventory. 5. Pack glue: hook wiring, equipment, setting. 6. Engine hook (openmw-tests). 7. Lua windows
(headless tests) and the two scripts. 8. In game: brewing, forging, tempering, enchanting with real clicks, three runs equal,
save and load; Morrowind unchanged.

## Out of scope
Perks (Alchemist, Steel Smithing, Enchanter ...), recipe conditions beyond a setting that shows or hides them, learning
effects from the Papyrus or books, the smelter and cook pot windows beyond what the generic recipe list gives, staff and
scroll enchanting, charge, soul trap, selling crafted items beyond what barter already does, gamepad.

## Stage log
2026-11-16; same hand wrote tests and code; no fresh reviewer.

**Built.** Pack: `crafting.*` (records INGR, COBJ, ENCH, SLGM, FURN, KYWD, MGEF editor ids, WEAP/ARMO EITM; the pure rules; the made
items and the knowledge; the save chunk `crafting` v1), `crafting_service.*` (session, alchemy brew and eat, craft, temper,
disenchant, enchant, the combat read, the namespace `skyrimse.crafting` 1.0). Additive pack edits: `ItemStat::mName`,
`ItemTable::addSynthetic`, `InventoryModel::grant`, `InventoryService::addSyntheticItem` and the `name` binding,
`SkyrimWorldView::magicEffectData`, the equip hook and `syncEquipment` read the base of made gear. Engine: `decideNeutralBench`,
a Furniture tooltip and the event `openeden_skyrimse_bench_opened` (`neutral.cpp`, `neutralinventory.*`). Lua:
`crafting_windows.lua`, `crafting_global.lua`, `crafting_player.lua`, two `builtin.omwscripts` lines. New pack settings (declared in
`extension.json`): `crafting` (false), `crafting test bench` (false), `crafting ignore conditions` (false), `crafting alchemy skill`,
`crafting smithing skill`, `crafting enchanting skill` (15 each). No new `[Game]` setting. Tools: `tools/skyrimcraft_check` (a copy
of `tools/inventory_check` whose scenario opens a test bench; the window clicks are real, through `OPENEDEN_RMLUI_INPUT_SCRIPT`).

**Tests first, red against stubs.** Pack, pure: 32 of 35 new tests failed against the stub (the 3 that passed test absence or
refusals: `anEffectOfOneIngredientAloneIsNotShared`, `oneIngredientTwiceOrNoneOrFourShareNothing`, `aBadDefinitionGivesNoItem`;
the real-data test skipped). Service: 11 of 11 run failed before the run was stopped (each stub test waits 62 s for a record table
that never arrives); the rest of the 43 were not run red. Engine (`NeutralCrafting`, 4 tests): written before the code but the
implementation was added before the first build, so NOT run red. Lua windows (15 tests): the module was written first, so NOT run
red and no mutation was run. After: `SkyrimCrafting*` and `CraftingServiceFixture.*` 96 of 96 pass, `SkyrimCraftingWindows` 15,
`NeutralCrafting` 4; the gated real-data test passes. **Existing tests edited: none.**

**Real data** (`SkyrimCraftingRealData`, Skyrim.esm): 94 ingredients, 594 recipes (7 COBJ have no CNAM), 604 enchantments, 158
forge, 158 sharpening wheel and 220 armour table recipes, the keyword ids and the grindstone, anvil, alchemy lab and enchanter
FURN of the survey; all five masters in game: 115 ingredients, 1,378 recipes, 743 enchantments, 336 furniture with keywords, read
on a worker in 2.3 s.

**In game** (Dragonsreach, `tools/skyrimcraft_check`; frames viewed; three runs of each scenario give the same md5 over the
`N6INV`, `CRAFT` and `INV` lines): (1) **alchemy** with Wheat and Blue Mountain Flower: Eat on Wheat learns Restore Health and the
rows then show it (`Restore Health, ?, ?, ?`); choosing both and Brew gives `Potion of Restore Health` (id ff000001, value 16, both
ingredients used, `Restore Health, ?, Fortify Health, ?` shown after), and `inventory.use` drinks it through task 0184's use path:
`INV use ff000001: health +5.375` (5 x 1.075 at skill 15), the potion consumed (needs `[Game] neutral combat`). (2) **smithing**: forge,
Iron Sword from 2 ingots and 1 leather strip (`CRAFT craft Iron Sword: ok`), then the grindstone, `Improve Iron Sword` at smithing
100: `Epic Iron Sword`, ingots 6 to 3 (the row then says `(cannot)`); wielded: `CRAFT combat reads Epic Iron Sword: damage 7 ->
10`. (3) **enchanting**: the enchanter lists a Steel Greatsword of Scorching, an Iron Sword and a Grand Soul Gem; Disenchant learns
`Fire Damage` and destroys the greatsword; choosing the sword, the enchantment and the gem shows `Result: Fire Damage 16.6`; Enchant
makes `Iron Sword of Fire Damage`, uses the gem; wielded: `damage 7 -> 24`. (4) **Save and load** (`[Game] neutral saves`): after the
forge and temper run, a save and a load in a new process restore `CRAFT chunk restored: crafting, 1 made items`, the inventory shows
`ff000001=Epic Iron Sword x1`, it is still equipped and the combat read is 10 again. (While the crafting window is open the world is
paused, so the pack does not sync equipment until the window is closed: the scenario closes it first.)

**Morrowind unchanged.** `tools/worldobjects_check` against `integ2`'s build (main): 111 lines, OK; the fixed Balmora frame
(`--random-seed 7`, `OPENEDEN_FIXED_FRAME_TIME`, frame 600) is `0d6dd39995b927fa` for the reference, the new build, and the new build
with `neutral world`, `neutral actors`, `neutral inventory`, `[GUI] rmlui` and the pack's `inventory`, `crafting`, `crafting test
bench` on. `openmw-tests` 2,751 pass (without the Slang, Papyrus and ObScript groups).

**Not run or not built.** The Linux gate and GCC `-Werror`, clang-format 14, clang-tidy, the Lua integration tests, sanitizers, a fresh
review, mutation checks, **a real key press on a bench**: the Furniture activation path (engine decision, event, global script) is
written and its decision is unit tested, but my attempts to stand the player before the Dragonsreach lab did not face it, so the
in-game checks open the bench through the test setting `crafting test bench` (`open_kind`); armour tempering, poisons, the Skyforge,
smelter, cook pot and tanning rack, scrolling a 158 row list, and a gamepad were not tried in game (the first three are unit tested).

## Decisions for toddhow
1. **Every formula is RECALLED-VERIFY** and lives in `CraftingSettings` (alchemy strength 1 + 0.5 x skill / 100 from
   `fAlchemySkillFactor`; temper +0.4 percent per smithing point, grades Fine 4, Superior 8, Exquisite 15, Flawless 25, Epic 35,
   Legendary 50 percent; enchant magnitude = base x soul multiplier (0.5 to 1.6) x (1 + 0.25 x skill / 100); potion value from the base
   cost; none is a setting yet).
2. **Recipe conditions.** 119 of the 159 forge recipes and ALL 384 tempering recipes carry conditions (functions 659 and 448, a perk
   test; the conditions engine could answer them with the perks module). Forge recipes with conditions are hidden unless
   `crafting ignore conditions`; tempering recipes are shown whatever their conditions, because hiding them made tempering
   unusable. Perk gates are not enforced.
3. **What combat reads.** Weapon damage x (1 + temper) plus the magnitude of a damage-on-Health enchantment (added once, not per
   target, no elemental resistance); armour rating x (1 + temper). Every other enchantment effect (fear, fortify, resist, absorb,
   paralyse), soul trap, charge use and recharge, and armour enchantments are stored and shown but apply nothing.
4. **Knowledge.** Nothing is known at the start; eating teaches the first unknown effect, brewing every shared effect, disenchanting
   the BASE enchantment (`ENIT` +28); the window shows `?` for an unknown effect. The soul gem is used up (no black soul gem return).
5. **Names.** A potion is named after its first effect (`Potion of Restore Health`, `Poison of ...` when that effect is hostile);
   a made weapon `<grade> <name> of <enchantment>`; an effect or enchantment with no FULL uses its editor id made readable.
6. **Made items** have ids `0xFF000000 + n` in creation order and are items of the inventory model, so barter, the loot window and
   equipping already see them. They never reach a record store.
7. **Skills.** The formulas read `[extension.skyrimse] crafting <name> skill` (15); the pack calls `mCraftingSkillSink(name, amount)`
   after each success (a skills system can set it) and logs `CRAFT skill used`. Nothing raises a skill.
8. **Equipment on tempering.** A tempered or enchanted item stays equipped (the new item takes the place of the old one).
9. **Window layout** is plain: the status line shifts the rows by one line; the enchanting lists show 4 rows each; a recipe list of
   158 rows needs the list's scrollbar (not driven in game). Furniture of every kind now shows a name tooltip with
   `[Game] neutral inventory`.
