# Task 0295: more weapons and magic (Skyrim SE pack)

Design: `game_packs/skyrimse/CLAUDE.md` (combat sections), tasks 0168 to 0186 (melee, bow, spells), 0191, 0214, 0229, 0242, 0248, 0250.
Branch: the worktree branch from `0f4cca6e35`, not pushed. Windows only.

## 1. Survey (five masters, `I:\Projects\OpenEden-work\followers\py2\survey.py`, aggregates only)

What the data uses, and what the pack did before this task:

| Kind | Data | Before |
| --- | --- | --- |
| Weapon enchantments (WEAP EITM -> ENCH) | 3,009 of 3,262 weapons carry EITM (1H sword 439 of 483, dagger 350 of 377, axe 363 of 394, mace 356 of 374, 2H sword 360 of 393, 2H axe/hammer 710 of 752, bow 303 of 341, staff 84 of 93, crossbow 32 of 36); 230 distinct ENCH are fire-and-forget touch gear enchantments (2,590 melee weapons carry one), 439 are constant (armour and clothing), 71 are staff enchantments (type 12: fire-and-forget aimed 39, concentration aimed 15, target actor 6, location 10, one more) | Only a made (crafted) enchantment's damage-on-health number was added to the blow; no effect of an enchantment ran |
| Staves (WEAP anim type 8) | 93 weapons, 218 NPCs carry one (direct, outfit or one list level) | Not cast: an equipped staff was an unarmed swing |
| Bound weapons (MGEF archetype 17) | 9 effects, 6 spells (BoundSword, BoundBattleAxe, BoundBow, the left-hand sword, DLC2BoundDagger), each names its WEAP (DATA +8) | Archetype not supported (logged) |
| Crossbows (anim type 9, bolt flag on 45 of 54 AMMO) | 36 weapons, 13 NPCs | Not built: the bow rules take only anim type 7, and only through the global test weapon |
| Dual wielding | CSTY DATA bit 4 (allow dual wielding) is read, nothing uses it; 724 EffectWasDualCast always 0 | Not built |
| Left hand spell + weapon | NPC spell lists 2,091 NPCs; the player has a single spell or weapon | Not built |

Ranking used (data use, then cost): 1 enchanted weapons on hit, 2 staves, 3 bound weapons, 4 crossbows, 5 dual wielding and dual casting, 6 the left hand.

## 2. Built

Behind the new pack setting `[extension.skyrimse] combat weapon magic` (off by default, declared in `extension.json`, a comment in `tools/testprofile/profile.cfg`; needs `[Game] neutral combat`).

- **Data.** `WeaponRecord::mEnchantment` (EITM); `decodeEnchantment` (`scroll.cpp`, declared in `scroll.hpp`): decodeSpell for the effects, then ENIT (36 bytes: cast type +8, delivery +16, enchant type +20); `SpellRecord::mEnchantment/mEnchantType`; `CombatRecords::mEnchantments` and `CombatIndex::spell(id)` find them (never by editor id); `CombatData::weaponById` (new virtual, default null, `SkyrimWorldView` implements it); `MagicArchetype::BoundWeapon` 17.
- **Enchanted weapons.** `SkyrimCombat::applyEnchantOnHit`, one line in `applyHit` after the poison hook: a landed, unblocked, non-bash blow whose weapon carries a fire-and-forget touch enchantment applies its effects (`applySpellEffects`, so resistances, wards and effect conditions hold). Player and NPC alike (the weapon is `weaponFor`).
- **Staves.** `staffSpellOf`/`playerStaffSpell`: the equipped weapon (else the `combat test weapon`) of anim type 8 with an enchantment. `runPlayerCast` takes it as the player's spell (`onPlayerAttackEdge` feeds the cast edges), `runPlayerSwing` stands down. NPCs: `FighterGear::mStaff` (`chooseStaff`, filled by `resolveFighterGear`), `aiPickSpell` prefers it. Cost 0: charge is not tracked.
- **Bound weapons.** `applyOneEffect` case `BoundWeapon`: needs `combat weapon magic`, a non-hostile effect with a duration and a WEAP with data; sets `Fighter::mBoundWeapon/mBoundUntil`; `weaponFor` returns it first (after the werewolf rule); the effect's end clears it (`tickEffects`).

## 3. Tests (components-tests, `testskyrimsecombatweaponmagic.cpp`, 17)

Written first and run red against stubs (a decoder that returns false, a `chooseStaff` that returns null, hooks that do nothing): **10 of 17 failed**; the 7 that passed test absence or a decoder that was not stubbed (the setting off, a non-touch enchantment, a miss, no staff spell with the setting off or a non-staff, a bound weapon with the setting off, the EITM decode of a WEAP, and two runs of one trace). Then green: 17 of 17. The first stub run showed two faults of the tests themselves (a victim never read by an idle fighter, a player cast that has no clip), fixed before the code. A gated real-data test (`SkyrimWeaponMagicReal`, `OPENEDEN_SKYRIMSE_DATA`) prints `SKYRIMSE-WEAPONMAGIC weapons 3262 enchanted 3009 resolving to an ENCH 3009 melee fire-and-forget touch 2590 staves 93 enchanted 84 resolved 84 aimed fire-and-forget 46 bound weapon effects 9 naming a WEAP 9`. Wider run: `*Skyrim*:*Combat*:*Magic*:*Scroll*:*Poison*:*Summon*:*Perk*:*Extension*:*Frozen*` 3,850 ran, 3,744 passed, 106 gated skips, 0 failed; `openmw-tests` `*Neutral*:*Combat*:*Extension*:*Skyrim*` 1,149 passed. No existing test was edited.

## 4. In game (`tools/skyweaponmagic_check`, derived from `tools/skysummons_check`; Bannered Mare, quiet window, volume 0, fixed frame time, the REAL attack control as synthetic SDL clicks)

- Enchanted sword (`combat test weapon = EnchIronSwordFire01`, `DIST=110`): `COMBAT enchantment EnchWeaponFireDamage01 of EnchIronSwordFire01 on the blow of player`, `effect EnchFireDamageFFContact ... magnitude -5.000 instant`; health 75 -> 67.475 (the 7.525 blow) -> 62.475 (the 5) at the next swing's start, and so on to 29.9.
- Staff (`combat test weapon = StaffFireball`, `DIST=300`): `player staff casts StaffEnchFireball`, `cast ... cost 0.000 magicka 100.000 of 100.000`, the projectile hits the actors, `explosion ... reached 3`. At `DIST=450` the bolt ends after 0.009 s on a beam 50 units ahead (the same happens with the Fireball spell).
- Bound weapon (`combat test hostile = Hulda`, `combat test cast = Hulda:DLC2BoundDagger`): before the cast her blows are the unarmed 4.000; after `effect bound weapon DLC2BoundWeaponDagger on actor 108142 for 120.000 s` her reach is 98.7 and a blow is 6.660, with the bound dagger's own enchantment applied on the blow (its Script effect is `archetype 1 not supported`, the perk effects fail their conditions).
- Three runs of each scenario: the COMBAT lines are byte-identical (md5 `e9c81e4d9f20` 24 lines, `5351d8e6b83d` 35 lines, `fa798c952451` 128 lines).
- Frames: ten of the staff cast viewed (`I:\Projects\OpenEden-work\followers\wm\staff4\user\screenshots`): the room and the three actors at the cast; **no fireball in flight and no bound weapon is in any frame** (the bolt flies 0.1 s at 300 units; no model is attached to a hand).
- Morrowind unchanged against the `integ2` build: `tools/worldobjects_check` 111 lines OK, the fixed Balmora frame `1c5d47ad0b5dc32d` for both, and for this build twice.

## 5. Gaps, ranked (data use, then cost)

1. **Crossbows** (36 weapons, 13 NPCs, bolts 45 of 54 AMMO). The bow code is the global test weapon (`mWeapon`, `mAmmo`) and checks anim type 7 in about ten places; a real equipped bow or crossbow for the player and a real gear bow for an NPC do not exist yet. The third-person clips are `crossbow_aim*`, `crossbow_release`, `crossbow_reload` under `meshes/actors/character/animations/dlc01/` (listed from `Skyrim - Animations.bsa`); their sidecar events were not read and the engine's clip lookup of a `dlc01` folder is unchecked.
2. **Real equipped bows for the player and NPC bow gear** (303 bows carry enchantments that would apply to arrows): the same re-plumbing as 1.
3. **Enchantment charge** (WEAP EAMT, ENIT cost): staves and weapons never run out here; a recharge and the soul gem rules are crafting's.
4. **Dual wielding** (CSTY DATA bit 4 is read; no second weapon slot exists in the equipment model or the clips) and **dual casting** (condition 724 stays 0).
5. **The left hand** (a spell in one hand, a weapon in the other): the player has one spell or one weapon.
6. **Visuals**: no staff or bound weapon model in a hand (`player_body` and `heldgear` do not know a bound weapon), no staff glow, the Script effect of a bound weapon (`EnchBoundDaggerFX`, archetype 1).
7. Concentration staves (15 of 71, flame-thrower style) and target-actor or location staves: `aiPickSpell` takes aimed fire-and-forget only; the player path hands any delivery to `beginCast`.
8. A touch enchantment of a bow or thrown weapon; the "weapon enchants only on the weapon's own blows" rule for bashes (a bash applies none here).

## 6. Decisions and notes for toddhow

- A staff is preferred over an NPC's own spells (it costs nothing); an NPC with a staff and a sword still swings the sword when close (the AI picks Cast or melee by its style numbers).
- `playerStaffSpell` falls back to the `combat test weapon` when nothing is equipped, so a game check needs no inventory clicks; it only matters with the setting on.
- Enchantments are stored beside spells and scrolls and answered by `spell(id)`; `spellByEditorId` does not find them.
- Charge is not tracked (gap 3); a weapon enchantment applies on every blow.
- No existing test edited; none needs a change.
- Not run: the Linux gate (GCC `-Werror`: the new code uses braces, `const` references and `(void)` free lines by hand), clang-format 14 (wrapped by hand to 120), clang-tidy, the Lua tests, a fresh review.
