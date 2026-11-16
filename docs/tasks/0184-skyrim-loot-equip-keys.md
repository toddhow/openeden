# Task: Skyrim loot, equip and keys (milestone M-S6, follow-up of task 0182)

Status: BUILT (Windows only, local commits on branch `loot` from `4f665dcd82`, not pushed). toddhow chose "Loot, equip and
keys" on 2026-11-15.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.17 (inventory, items), M-S6; task 0182 (the windows and
the model), task 0168 to 0183 (combat), decision 0017 (neutral actors and items).
Goal: dead NPCs can be looted; the player can equip and unequip weapons and armour, which changes the melee damage and the
armour rating combat already reads; a potion heals; a KEYM in the inventory opens its lock; the speech and bribe checks read
the gold in the inventory.
Constraints: behind `[Game] neutral inventory` and `[extension.skyrimse] inventory` (both off), plus `[Game] neutral combat`
where combat is involved. With them off nothing changes. No combat formula is changed: with nothing equipped the combat
numbers equal the parent build's.

## 1. Survey (read at `4f665dcd82`)

- **Who is dead.** `SkyrimCombat::Fighter::mDead` (`game_packs/skyrimse/src/combat.cpp`, `applyHit`, set before
  `writer.reportDeath`), and the engine's `NeutralCombatBridge::mDead` (`mwphysics/neutralcombatbridge.cpp`,
  `Writer::doReportDeath`, `isDead(handle)`). The bridge's handle is `{placement id, session}`; the physics system keys its
  actors by `ptr.mRef` and `NeutralActorBridge::handleOf(key)` gives the handle (`PhysicsSystem::announceNeutralActor`).
  So a Ptr maps to "dead" by `handleOf(ptr.mRef)` then `isDead(handle)`. The list is cleared when the cell unloads and the
  neutral actor is rebuilt alive at load (decision 0017).
- **Activation.** `MWClass::Neutral::activate` (`mwclass/neutral.cpp`): an Actor goes to `TalkAction`; a Container opens
  `OpenInventoryAction` (event `openeden_skyrimse_container_opened`, kind hard-coded to "container").
- **Pack inventory.** `InventoryModel` (owner -> item -> count, `mTouched`, a revision), `InventoryChunk` (save chunk
  `inventory` v1: the player and every touched owner), `InventoryService` (item table on a worker, bindings
  `skyrimse.inventory` 1.0), `SkyrimWorldView::npcInventory` (CNTO plus default outfit). The chunk already keeps any owner
  that had a transfer, keyed by the placement id: a corpse's changes are as durable as a chest's once the owner is touched.
- **Combat reads.** One weapon for both sides: `SkyrimCombat::mWeapon` (the `combat test weapon`), used in
  `handleHitFrame` (stamina, reach), `applyHit` (damage, skill), `runPlayerSwing`. Armour: `Fighter::mProfile
  .mMitigationPercent` (and `mPieces` for perks); the player's profile is `CombatData::playerFighter()` (no armour).
  The player's health is the engine's: `writer.subjectState(player)` and `reportDamage` with `victimIsPlayer`
  (`NeutralCombatBridge::Writer::doReportDamage` clamps `healthAfter` to [0, max] and applies it; `amount` is only
  required to be >= 0).
- **Records.** `CombatIndex::weapon(FormId)`, `ActorIndex::armor(FormId)` (`ArmorExtra`: rating, slot mask, type,
  keywords), `armourOf(pieces, skills, settings)`, `CombatIndex::magicEffect(FormId)` (`MagicEffectRecord`: archetype,
  primary actor value). ALCH records carry EFID (effect form) and EFIT (magnitude f32, area u32, duration u32).
- **Gold and keys.** `PlayerStandIn::mGold` (`speech.hpp`) read by `DialogueWorld` at `dialogue.cpp` (the gold
  functions and `bribeSucceeds`); `neutralLockState().hasKey(gameId, key)` is the engine's key ring, used by
  `decideNeutralLock` in `Neutral::activate`. The pack owns the inventory, the engine owns the lock decision.

## 2. Design

**Equipped state** (pack, `inventory.*`): `InventoryModel` holds the player's equipped items: `EquippedItem{item, kind,
slotMask}`. Equipping a weapon replaces the equipped weapon; equipping armour replaces every equipped piece whose slot
mask overlaps; a potion cannot be equipped. An item the player no longer holds is not equipped (`equipped()` filters; a
transfer that takes the last one off the player unequips it). The state changes the revision. It is saved in its own chunk
`equipment` v1 (the `inventory` chunk is not touched, so no pinned test line changes): u32 count, then (item u32, kind u8,
slot mask u32).

**Equip rules come from records through hooks**: `Hooks::mEquipInfo(item)` (the world view: a WEAP is a weapon with mask 0,
an ARMO an armour with its BOD2 mask, else none), `Hooks::mHealthOf(effect, magnitude, duration)` (an instant
ValueModifier on Health that is not hostile), `Hooks::mApplyHealth(amount)` and `Hooks::mEquipmentChanged()`.

**Bindings** (`skyrimse.inventory` 1.1, additive): Read `equipped(item) -> bool`, `use_kind(item) -> string` ("weapon",
"armor", "potion" or ""); Mutate `equip(item)`, `unequip(item)`, `use(item)`. `use` on a weapon or armour toggles nothing
(the window sends equip or unequip); on a potion it applies the instant health, then removes one.

**Combat** (additive in `combat.*`): `SkyrimCombat::setPlayerEquipment({weapon, pieces})` and `queuePlayerHeal(amount)`. For
the PLAYER as attacker the equipped weapon (if any) replaces `mWeapon` (`weaponFor(fighter)`); the player's mitigation is
`armourOf(pieces, skills, settings)` once pieces are equipped, else the stand-in's (0). With nothing equipped every number
is the parent's. A queued heal is applied in `combatUpdate` through `subjectState(player)` and a `DamageReport` of amount 0
with `healthAfter = min(max, health + heal)` (the engine's existing write-back). Melee weapons only: a bow stays out of the
melee rule (the bow tests settings still drive archery). NPCs keep the test weapon.

**Gold.** `DialogueWorld` gets an optional `GoldSource` (a function); when set, the player's gold in the speech, bribe and
`GetItemCount` of Gold001 is its value. The pack sets it to the inventory's Gold001 count when the inventory is on.

**Keys.** New additive SDK interface `openeden/game/player_item_source.hpp` (`PlayerItemSource::playerHasItem(WorldId)`,
found by `dynamic_cast` on the registered source like `DisplayNameSource`). `SkyrimCellSource` implements it through a
function the game pack sets (the inventory model: a held count of the item). `Neutral::activate` ORs it into the key
check (`hasKey`) of `decideNeutralLock`; a key in the inventory opens and unlocks the lock for the session as a key from
the ring does. A KEYM that is also in the engine's ring still works. Off (no source, no inventory): as before.

**Dead loot.** `PhysicsSystem::isNeutralActorDead(ptr)`. `Neutral::activate`: an Actor, the player, `[Game] neutral
inventory` on and the actor dead: `OpenInventoryAction` with kind "actor" (the pack's `open` already registers an `actor`
owner from `npcInventory`); otherwise as before (talk). The loot window is the container window with the corpse's name
("Search"). Corpse changes are durable in the `inventory` chunk keyed by the placement. The corpse itself is rebuilt alive at
load (decision 0017); if it is killed again its loot shows what was left.

**Windows.** Rows of the player's list get an `Equip`/`Unequip` button (weapons and armour) or `Use` (potions) beside the
`Put` button; an equipped row shows `[E]`. A row key includes the equipped flag.

## 3. Stages

1. This spec.
2. Pack model, chunk, service bindings and hooks (components-tests, generated plugins).
3. Pack combat feed, gold source, key source (components-tests).
4. Engine: loot decision, dead lookup, key check (openmw-tests).
5. Lua windows and scripts (headless backend tests).
6. In game: Bannered Mare, real clicks, three runs equal; save and load; Morrowind unchanged.

## Acceptance criteria
- [x] Equip and unequip change the model, keep one weapon, and replace overlapping armour; the state round-trips in its
      chunk and a damaged chunk changes nothing.
- [x] The player's melee damage and the damage taken follow the equipped weapon and armour; with nothing equipped the
      combat numbers equal the parent's.
- [x] A potion heals the player through the engine's health write-back and is consumed once.
- [x] A KEYM in the inventory opens its lock; the speech and bribe checks read the inventory's gold; off: as before.
- [x] A dead NPC's activation opens the loot window; its changes survive a save and load in a new process.
- [x] With every new setting off, Morrowind and Skyrim sessions behave as before.

## Out of scope
Barter, stealing, enchanting, spells, poison, food over time, enchantment effects, two-handed or bow equip changing the clip
tables, a count slider, dead-state persistence, the `openeden.ui` components and themes, Papyrus files, combat formulas.

## Stage log (2026-11-15; Windows only; local commits on branch `loot` from `4f665dcd82`, not pushed; same hand wrote tests and code, tests first and run red against stubs, no fresh reviewer)

**Built.** Pack: `inventory.*` (equipped items in `InventoryModel`: `equip`, `unequip`, `isEquipped`, `equipped`, `consume`,
`setEquipped`, `equipmentRevision`; `EquipmentChunk` "equipment" v1; ALCH EFID/EFIT decode; `parseStackList`),
`inventory_service.*` (equip info, `healthOf`, `use`; bindings `skyrimse.inventory` 1.1: Read `equipped`, `use_kind`; Mutate
`equip`, `unequip`, `use`), `combat.*` (`PlayerEquipment`, `setPlayerEquipment`, `queuePlayerHeal`, `playerMitigationPercent`,
`weaponFor`, `applyPlayerHeal`; two blocks in `handleHitFrame` and `applyHit` now ask `weaponFor`, no formula changed),
`dialogue.*` (`setGoldSource`, `playerGold`), `world_view.*` (`lookupWeapon`, `armourPieceById`, `healthOfEffect`,
`setPlayerGoldSource`), `neutral_world.hpp` (`SkyrimCellSource` implements the new SDK interface), `skyrimse_game_pack.cpp`
(hooks, `syncEquipment`, the equipment chunk, the gold and key lookups, the `inventory test items` test aid declared in
`extension.json`). SDK: `openeden/game/player_item_source.hpp` (DRAFT 0.1, additive). Engine: `neutralinventory.*`
(`decideNeutralLoot`, `neutralKeyHeld`), `NeutralCombatBridge::isDeadByKey`, `PhysicsSystem::isNeutralActorDead`,
`Neutral::activate` (dead actor: loot event of kind "actor"; the key check asks the inventory too). Lua:
`inventory_windows.lua` (Equip, Unequip, Use buttons, `[E]` marker), the two scripts (three new global events),
`builtin.omwscripts` now `REQUIRES=core.skyrimse.inventory>=1.1`. Tool: `tools/inventory_check` (OWNER, ITEM, WORN, tick lines).

**Tests first, red against stubs.** Pack `components-tests` (generated plugins): 27 of 35 failed against stub bodies (the 8
that passed pin baseline or absence: nothing equipped changes no damage, no combat data refuses a heal, the plain gold, a
potion is not equippable, and the cell source answer, which was written together with its header). Engine `openmw-tests`: 4 of
11 failed against stubs (7 pin absence or the ring-only rule). Lua windows over the headless backend: 4 of 6 red against the
module of task 0182. The two `parseStackList` tests (test aid), the real-data test and the slot-lookup test (a failed
armour lookup is asked again, a success is kept) were written with or after the code and not run red. After: all pass;
`components-tests` without the Slang, complement and property groups 10,755 pass, `openmw-tests` 2,688 pass (2,690 with the
real-data tests). Existing tests edited: none.

**Real data** (`SkyrimEquipRealData`, five masters): every ALCH EFID is followed by a 12 byte EFIT (0 odd subrecords across
the inventory tables, as before); the RestoreHealth effect (0x0003EB15) is archetype 0 (ValueModifier), primary value 24
(Health), flags 0x200a00 (the hostile 0x1 and detrimental 0x4 bits are clear); `RestoreHealth01` (0x3EADD) is 25 points,
duration 0. The polarity of those two bits on a hostile effect is still RECALLED-VERIFY (the run only shows they are clear
on a restore).

**In game** (Bannered Mare, fixed frame time, `OPENEDEN_QUIET_WINDOW=1`, real key presses and mouse clicks through
`OPENEDEN_RMLUI_INPUT_SCRIPT`; `tools/inventory_check/check.sh` and `tools/neutral_session_check/check.sh`; frames viewed):
- **Loot.** Hulda dies after ten hits of 7.525 (`COMBAT death`), the activate key opens `NINV actor 0001a66e base 00013ba3
  'Hulda'`: her 8 stacks (Iron Dagger, Boots, Clothes, Necklace, Silver Ring x2, Baked Potatoes, Gold x6, Lockpick) show in
  the right window; Take all moves 14 items (`INV take_all 0001a66e -> player: ok, 14 items`), and the inventory shows them
  with Equip and Use buttons.
- **Equip.** Real clicks on Equip: the player's damage per hit is 7.525 (the test sword) before and 4.3 (an Iron Dagger,
  4 x 1.075) after; the Iron Armor is worn: `COMBAT player armour 1 pieces: rating 26.875 takes 6.225% off` (25 x 1.075 x
  0.12 + 3).
- **Armour and potion.** Hulda hits the player for 7.77 before and 7.286 after (7.77 x (1 - 0.06225)); `Use` on a Potion of
  Healing: `INV use 0003eade: health +50`, `COMBAT heal player +50 health 69.887 -> 100`, one potion consumed.
- **Key.** Angarvunde01, the door 185905 (key 0x8715F): without the item `NLOCK -> 3` (refused); with `inventory test items =
  0008715f:1` `NLOCK -> 2` and the player arrives in `Angarvunde02` (log lines; the scenario activates through Lua
  `activateBy`, not a key press, and no frame of the unlock message was kept: the screenshot it saved is the start of the
  cave).
- **A looted weapon.** No test items: Hulda is killed (ten hits of 7.525), the activate key opens her, Take all, Close, the
  `I` key, a click on Equip of the Iron Dagger she carried (`INV equip 0001397e: ok`), then the player stands before Mikael
  and every hit is 4.3 (`before armour 4.300`) instead of 7.525. (The Bannered Mare's containers hold no weapon: a scan of
  their CNTO lists found none.)
- **Three runs identical.** md5 of the COMBAT, INV and NINV lines: the equip run (a hostile NPC absent), the loot run, the
  looted-weapon run and the armour and potion run (Hulda hostile, idles on) each gave 3 of 3 equal traces.
- **Against the parent build.** The n5trade scenario (`run.sh`: Hulda hostile, player swing 2, IronSword, idles on), 2 runs
  each of the parent-era build `combat5` (another agent's build of main), of this build with every new setting off and of
  this build with `neutral inventory`, `rmlui` and the pack's `inventory` on and nothing equipped: the 36 `COMBAT damage`
  lines and the death line are identical in all six runs; the whole COMBAT trace is identical in four of six, the other two
  differ in the last digits of one sweep heading (1.5708 against 1.57224) and one extra swing line at the end of the run
  (the scenario's own timing). An earlier hostile equip run WITHOUT `neutral actor idles` (no NPC blow ever landed, the
  clips that deliver the HitFrame event were not played) gave 2 equal runs of 3 and is not used as evidence.
- **Save and load in a new process.** Dagger and armour equipped, Hulda killed and her Silver Ring x2 taken, saved by the
  menu script; a second process with no `inventory test items` setting loads it: `player=2` rings, the corpse owner known
  with 0 left, gold 106, `worn=true` for the dagger (`INV chunk restored: 2 owners, 1 touched`).
- **Morrowind unchanged.** `tools/worldobjects_check` dump equal to the parent's (111 lines), the fixed Balmora frame
  `1c5d47ad0b5dc32d` for the new build with the settings off and with `neutral world`, `neutral actors`, `neutral combat`,
  `neutral inventory`, `[GUI] rmlui` and the pack's `inventory` on.

**Limits seen.** The first armour row of a session asks the actor index for its slot mask (`armourPieceById`), which builds
the index on the main thread if no actor ever did (about a second or two, the same cost as opening a corpse; not measured in an
inventory-only session). The window rows misalign a little (rows without a button are shorter).

**Not run.** The Linux gate and GCC `-Werror`; clang-format 14 and clang-tidy (formatted by hand to 120 columns); the Lua
integration tests; sanitizers; a fresh review; mutation checks; two-handed, bow or crossbow equipment (the melee rule ignores
a non-melee weapon, the bow settings still drive archery); a shield or a spell; armour with perks (the perk path still reads
the test weapon); a locked container with a key in game (the code path is the door's); looting an NPC that died in an
exterior; stealing and ownership; a corpse that was killed again after a load (see decision 3).

## Decisions for toddhow
1. **Equipped state is its own chunk** (`equipment` v1) so that the `inventory` chunk and its pinned tests did not change.
   Fine, or merge the two (an inventory chunk version 2)?
2. **The player's weapon.** The equipped melee weapon replaces the test weapon for the PLAYER only; with nothing equipped the
   test weapon stays (so every older scenario is unchanged). NPCs still swing the test weapon until their records feed it.
3. **A corpse is rebuilt alive at load** (decision 0017), but its looted state is kept in the chunk by its placement: when it
   is killed again it shows what was left. A saved "is dead" needs the actors to be saved (not planned).
4. **Potions heal only instantly** (duration 0, not hostile, not detrimental, Health). Food, poisons and effects over time are
   not applied. A potion needs the combat rules to run (`[Game] neutral combat`), else `use` refuses and keeps the potion.
5. **Equip kinds.** Every ARMO equips (jewellery and clothes too), by its BOD2 slot mask; an armour whose slots overlap
   replaces the old one. A weapon replaces the weapon. Skyrim's own slot rules are what BOD2 gives; not checked against the
   game.
6. **The key ring and the inventory.** A KEYM in the inventory opens the lock as one from the ring does; opening it does not
   consume the key. The lock is remembered open for the session as before.
7. **The gold of the speech and bribe checks follows Gold001 in the inventory** when `[extension.skyrimse] inventory` is on;
   the stand-in setting `player gold` is then only the starting amount.
8. **New test aid** `[extension.skyrimse] inventory test items` (hex ids, optional counts): the player starts with them.
9. **`builtin.omwscripts` now needs namespace 1.1**: a pack older than this change does not start the inventory scripts.
10. **Window layout** is plain (rows misalign a little when a row has no Equip button, no sorting by equipped, no count
    slider); the components are another agent's.
