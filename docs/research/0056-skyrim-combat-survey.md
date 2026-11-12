# Research note 0056: what Skyrim SE combat (milestone M-S5) needs, measured on the installed data

Date 2026-11-12. Research only: no engine, SDK or pack code changed. Base `c91e219be0` (local `main`), branch `skycombat`.
Plan references: `docs/design/skyrim-se-implementation-plan.md` 4.12 to 4.16, 4.19, 4.21, 5.2 (M-S5), 7 (D1 to D15).
Related: notes 0054 (clips), 0054-provenance (clean-room log), 0055 (rules survey), task 0119 (what `components/havokpack` decodes).

## 0. Summary

1. **Combat data is small and regular.** Over the five masters (Skyrim, Update, Dawnguard, HearthFires, Dragonborn): 3,262 weapons,
   3,675 armours, 54 ammunition, 236 projectiles, 195 combat styles, 475 perks, 1,354 spells, 1,573 magic effects, 743 enchantments,
   117 shouts. Every subrecord that M-S5 reads has **one size in all records** (WEAP `DNAM` 100 bytes, `CRDT` 24, `DATA` 10; MGEF `DATA` 152;
   SPEL `SPIT` 36; ENCH `ENIT` 36 (4 records 32); PROJ `DATA` 92 (6 others 84/88); CSTY blocks in two sizes; RACE `DATA` 164, `ATKD` 44).
2. **The animation protocol is event names, and the data states them.** The sidecars (`animationdata/*.txt`) give each attack clip the
   same ordered events: `preHitFrame`, `weaponSwing`, `HitFrame`, then `AttackWinStart`/`AttackWinEnd`, `attackStop` (or `PowerAttackStop`).
   143 of the 189 attack-named clips of `DefaultMale` have exactly one `HitFrame`. RACE `ATKE` names the event that starts each attack
   (202 distinct names), RACE `ATKD` holds damage multiplier, chance, strike angle, stagger and recovery per attack.
3. **The player's behaviour graphs are large: 18 files, 19,652 objects, 3,477 states, 3,224 clip generators, 1,140 state machines, 55 classes
   of which 16 are Bethesda-specific (`BS*`, 684 objects), 1,985 distinct event names.** Reading and running them is the 70 to 130 day
   unknown plan 4.12 already named. **Recommendation: do not read the behaviour graphs for combat.** Drive combat from CreativeMotion's graph
   with Skyrim's clips and the event names above (plan option O1+O2). Optional later: a bake-time table "event name -> clip" read from a
   handful of simple behaviour classes (section 6.4).
4. **First slice** (an NPC and the player trading melee hits from a real swing event) needs: a clip player for one 1H swing with its sidecar
   events (the engine already plays `mt_idle` from `.hkx`), a new core hook that delivers animation events to the pack and a "sweep" query
   to Jolt, actor values (health/stamina), the weapon and armour numbers and ten GMSTs. It does **not** need Papyrus, perks, spells or
   behaviour graphs. Estimated 35 to 65 days of the 134 to 250 for all of M-S5 (section 8).
5. **Magic first-slice spells:** Healing and Ward use no scripts; **Firebolt's damage effect itself carries a `VMAD` script** (31 percent of
   all 1,573 effects do; 376 of 1,354 spells use at least one such effect). Ward is `AccumulateMagnitude` + `PeakValueModifier` (no script).

Everything below is an aggregate or an identifier of the data format (record type, field offset, enumerator, setting name, event name).
No game text, form id, mod path or asset is reproduced. Numbers marked RECALLED-VERIFY are not confirmed by the data.

## 1. Method, sources, provenance

* **Records.** A throw-away Python reader (zlib records, groups, `XXXX`) loaded the five masters; records are merged by global form id
  (owner plugin = master the id's top byte names), a later plugin replaces an earlier one, **deleted winners are dropped** (SPEL 1, NPC_ 1, IDLE 2,
  EXPL 1; the loaded total of 1,178,642 records equals note 0055). **"Census" below = every record in the five files; "winners" = after the
  merge.** Per-type: AVIF 153/149, GMST 1,659/1,650, WEAP 3,293/3,262, ARMO 3,835/3,675, AMMO 66/54, PROJ 237/236, EXPL 278/275 (after the deleted one),
  CSTY 198/195, PERK 523/475, SPEL 1,440/1,354, MGEF 1,633/1,573, ENCH 752/743, SHOU 117/117, WOOP 107/107, RACE 204/161, IPCT 663/663, IPDS 339/313, SNDR 3,260/3,150.
* **Layouts** were taken from xEdit's record definitions and CommonLibSSE-NG's headers (toddhow's permission, 2026-11-11) and **each one was checked on the data**:
  one subrecord size everywhere, enumerators inside the declared set, value ranges plausible (for example WEAP `DNAM+0` has 10 values 0 to 9; `+76` is
  6/7/8 = One-Handed/Two-Handed/Archery for 3,173 of 3,262; MGEF `DATA+80` has only 0/1/2 and `+84` only 0 to 4). Sources actually fetched this session:
  CommonLibSSE-NG `include/RE/B/BGSEntryPoint.h` (entry points 0 to 91), `RE/E/EffectArchetypes.h` (archetypes 0 to 46) and `RE/E/EffectSetting.h`
  (the MGEF `DATA` member order, which matches the offsets measured). xEdit layouts for WEAP `DNAM`, RACE `DATA`/`ATKD`, SPEL `SPIT`, ENCH `ENIT`, PROJ `DATA`
  and CSTY blocks are from memory and **RECALLED-VERIFY except where this note says the data agrees**.
* **Behaviour files** were extracted from `Skyrim - Animations.bsa` (654 `.hkx` that are not clips: character behaviours, creature behaviours, props);
  class names and counts come from `components/havokpack`'s own `Packfile` reader (`hkxconvert info`). Event and variable name tables were read with a
  throw-away walker over the container fix-ups (F6, F7 of 0054-provenance): in `hkbBehaviorGraphStringData` the first two array members are the event
  and variable name arrays (39 events and 33 variables in `blockbehavior.hkx`, names plausible: REASONING + DATA, not logged in 0054-provenance yet).
  No Havok SDK, no decompiled code and no other project's Havok source was used.
* The reader scripts live in `I:\Projects\OpenEden-work\skycombat\tools\` (`esm.py`, `load.py`, `s1.py` to `s9.py`, `bhv.py`); they are **not** committed
  (no tests; the repository's survey-tool pattern, task 0116, needs C++ with tests written first). If toddhow wants the numbers reproducible in CI, port them to
  `game_packs/skyrimse/survey_*` (section 9, decision 8).
* Not read: the character files (`characters/*.hkx`: the archive holds none; the project files name them), the 2,508 `hkbClipTriggerArray` objects in the
  player's behaviours (they carry events too), `hkbBehaviorGraphData` variable initial values.

## 2. Actor values, race values, derived-stat settings

**AVIF (149 winners).** 20 carry skill data (`AVSK`, 16 bytes of floats) and a perk tree (`PNAM`/`FNAM`/`XNAM`/`YNAM`/`HNAM`/`VNAM`/`SNAM`/`INAM`, 219 nodes):
the 18 skills plus two (a heal-rate power mod and a magicka-rate mod). Perk tree sizes per skill 7 to 16 nodes. The other 129 are the plain values.
Combat-relevant plain ones: Health, Magicka, Stamina, HealRate, MagickaRate, StaminaRate, SpeedMult, CarryWeight, InventoryWeight, CritChance, MeleeDamage,
UnarmedDamage, Mass, Aggression, Confidence, Energy, Morality, Mood, Assistance, DamageResist, PoisonResist, FireResist, ElectricResist, FrostResist,
MagicResist, NormalWeaponsResist, Paralysis, Invisibility, ArmorPerks, ShieldPerks, BowSpeedBonus, BowStaggerBonus, AbsorbChance, ShoutRecoveryMult,
Telekinesis, the `*Mod` and `*SkillAdvance` families. The actor value *index* used by `WEAP DNAM+76`, `MGEF DATA+68` and `PERK` is a position in the game's own
list (CommonLibSSE-NG's `ActorValue`: 24 = Health, 25 = Magicka, 26 = Stamina, 6/7/8 = skills) and is **not** stored in the AVIF record (RECALLED-VERIFY: the
order of the engine's table; the data agrees for 6/7/8 and 24 to 26 by usage).

**RACE (161 winners) starting values** (`DATA`, 164 bytes; offsets verified by comparing NordRace, WolfRace and DragonRace, RECALLED-VERIFY names): +36 health,
+40 magicka, +44 stamina, +48 carry weight, +52 mass, +56/+60 acceleration/deceleration, +84 health regen, +88 magicka regen, +92 stamina regen, +96 unarmed damage,
+100 unarmed reach. Nord: 50/50/50, carry 300, regen 0.7/3/5, unarmed damage 4, reach 96. Dragon: 500/150/100, unarmed damage 25. Wolf: 12/0/200.
Most common stored starting health: 50 (64 races), then 100, 250. RACE also holds the **attack list**: `ATKD` (44 bytes) x 2,065 in 155 races with its
event name `ATKE` (202 distinct names; 79 races have `bashStart`, 52 `AttackStartH2HRight/Left`, 46 `attackStart_Attack1`, 41 `attackStart`, 39 each of five
`attackPowerStart*` directions). `ATKD` field meanings (xEdit, RECALLED-VERIFY, values from data): +0 damage multiplier (1.0 for 1,013 of 2,065; 2.0 for 514;
1.5 for 326), +4 chance (1.0 for 1,322), +8 spell, +12 flags (0, 4, 12), +16 attack angle (0 for 1,875), +20 strike angle (35 for 1,582), +24 stagger (0 to 0.6),
+28 attack-type keyword, +32 knockdown, +36 recovery time, +40 fatigue multiplier (1.0 for 2,021).

**Combat-relevant GMSTs (666 non-string GMSTs; 568 float, 97 int, 1 bool).** Names and values as stored. Those marked * are used by the first slice.
Many names are leftovers from other games and unused by Skyrim (RECALLED-VERIFY which are read):

| Area | Name = value |
| --- | --- |
| Armour | * fMaxArmorRating = 80; fArmorScalingFactor = 0.12; fArmorBaseFactor = 0.03; fArmorRatingBase = 1.0; fArmorRatingMax = 2.5; fArmorRatingPCMax = 1.4; fClothingArmorScale = 1.0 |
| Block | * fBlockMax = 0.7; fBlockWeaponBase = 0.3; fBlockWeaponScaling = 0.2; fBlockSkillMult = 2.0; fBlockAmountWeaponMult = 1.0; fBlockPowerAttackMult = 0.66; fCombatBlockChanceWeaponMult = 0.25; fStaminaBlockDmgMult = 0.25; fStaminaBlockBase = 0.0; fStaggerBlockBase = 0.001, fStaggerBlockMult = 0.005, fStaggerBlockAttackMult = -0.0001; fStaggerBlockAttackShieldBase = 0.01, ...Mult = 0.001; fShieldBaseFactor = 0.45; fShieldScalingFactor = 0.2; iPerkBlockDisarmChance = 50 |
| Bash | fCombatBashReach = 141; fShieldBashMax = 0.25; fShieldBashMin = 0.05; fShieldBashPCMax = 0.25; fWeaponBashMax = 0.25; fStaminaBashBase = 35; fStaminaPowerBashBase = 55 |
| Power attack | * fPowerAttackDefaultBonus = 1.0; fPowerAttackStaminaPenalty = 2.0; fActionPointsPowerAttackMult = 1.5; fPowerAttackCoolDownTime = 0.0 |
| Stamina cost of attacking | * fStaminaAttackWeaponBase = 20; fStaminaAttackWeaponMult = 1.0; fActionPointsAttackUnarmed = 22 |
| Sneak attack | fCombatSneak1HSwordMult = 3, ...1HDagger = 3, ...1HAxe = 3, ...1HMace = 3, ...2HSword = 2, ...2HAxe = 2, fCombatSneakHandMult = 2; fCombatDamageBonusSneakingMult = 2; fCombatSneakAttackBonusMult = 100; fDamageSneakAttackMult = 1 |
| Stagger / knock | fStaggerMassOffsetBase = 2.1; fStaggerPlayerMassMult = 2.5; fStaggerMin = 0; iStaggerAttackChance = 50; fKnockbackDamageMult = 5; fKnockbackForceMax = 128; fKnockbackTime = 0.3; fKnockdownCurrentHealthThreshold = 50; fProjectileKnockMultBiped = 0.25 |
| Damage / death | fDamagePCSkillMax = 1.5; fDeathForceDamageMax = 40, ...Min = 1; fDeathForceRangedDamageMax = 50, ...Min = 1; iCombatDismemberPartChance = 50; iCombatExplodePartChance = 75; fCombatPlayerLimbDamageMult = 0.5; fCombatPlayerBleedoutHealthDamageMult = 0.0 |
| Regeneration in combat | fCombatHealthRegenRateMult = 0.0; fCombatMagickaRegenRateMult = 0.33; fCombatStaminaRegenRateMult = 0.35; fDamagedHealthRegenDelay = 0.5; fDamagedMagickaRegenDelay = 0.5; fDamagedStaminaRegenDelay = 0.5; fDamagedAVRegenDelay = 1.0 |
| Magic | fMagicWardPowerMaxBase = 1.3; fMagicWardPowerMaxMult = 0.0; fShoutTime1 = 0.2; fShoutTime2 = 0.9 |
| AI | fCombatDistance = 141; fCombatAbsoluteMaxRangeMult = 4; fCombatCurrentWeaponAbsoluteMaxRangeMult = 1.25; fCombatAdvanceInnerRadiusMax = 256; fCombatAdvanceOuterRadiusMax/Mid/Min = 512/256/128; fCombatGroundAttackTimeMin/Max = 8/17; fCombatConfidenceModifierMin/Max = -0.25/1; fBowNPCSpreadAngle = 4; iAICombatRestoreHealthPercentage = 60 |
| Misc | iArrowInventoryChance = 33; fAttachedArrowAgeMax = 10; fCombatMissileStickDepth = 20; iFriendHitCombatAllowed = 4; fSneakBaseValue = -15 |

The *formulas* that combine these (damage taken, armour mitigation, block percentage, stagger chance) are **not in the data** and are RECALLED-VERIFY; they are
mathematical facts to be pinned by tests on small cases and compared with observed numbers once an in-game test exists (plan 4.13).

## 3. Perks

475 winners (523 in the census). Per perk: 358 have one effect, 62 two, the rest up to 18; 31 have none (flag-only/placeholder). Effects: **607 entry-point, 32 ability, 28 quest** (`PRKE`
type 2/1/0). 12 perks have scripts. Conditions: 546 perk-level (before the first effect) and 1,649 inside effects (`PRKC` tabs), 328 perks have effect conditions.

**Entry points actually used: 68 of the 92 that exist (0 to 91; names from CommonLibSSE-NG `BGSEntryPoint.h`).** Ranked by uses (607 total):

| Rank | id name | uses | | Rank | id name | uses |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 35 ModAttackDamage | 81 | | 13 | 56 ModPickpocketChance | 13 |
| 2 | 51 ApplyCombatHitSpell | 58 | | 14 | 59 ModLockpickSweetSpot | 13 |
| 3 | 29 ModSpellMagnitude | 54 | | 15 | 74 FilterActivation | 13 |
| 4 | 38 ModSpellCost | 43 | | 16 | 77 ModEnchantmentPower | 13 |
| 5 | 85 ModArmorRating | 29 | | 17 | 0 CalculateWeaponDamage | 11 |
| 6 | 36 ModIncomingDamage | 23 | | 18 | 88 ModSpellRange_TargetLoc | 11 |
| 7 | 14 Activate | 18 | | 19 | 66 ModAlchemyEffectiveness | 11 |
| 8 | 76 ModTemperingHealth | 18 | | 20 | 57 ModDetectionSneakSkill | 10 |
| 9 | 30 ModSpellDuration | 17 | | 21 | 32 ModArmorWeight | 10 |
| 10 | 60 ModSellPrices | 15 | | 22 to 68 | (47 more, 8 uses down to 1) | 116 |
| 11 | 8 ModBuyPrices | 15 | | | | |
| 12 | 1 CalculateMyCriticalHitChance | 14 | | | | |

Coverage of the 607 uses: **7 entry points give 50 percent, 21 give 80, 30 give 90, 42 give 95, 62 give 99.** Combat-related ones (damage, hit, block, stagger, armour, crit,
sneak, bow, bash, power attack, ward, spell magnitude/duration/cost, ids listed in the survey script) account for 433 of 607 uses. The dominant function code is 3
(multiply; 75 of 81 ModAttackDamage) and for ApplyCombatHitSpell 10 (add spell; RECALLED-VERIFY function names). Entry-point *parameter types* (`EPFT`): 1 (float) 479, 5 68,
4 18, 2 36, 6 3, 7 3. Perk conditions by function (PERK records): `HasKeyword` 713, `HasPerk` 413, `GetBaseActorValue` 261, `GetIsID` 98, then a tail of 54 functions; coverage 2 functions = 50 percent,
9 = 80, 22 = 95; **84.0 percent of perk condition uses are of functions the pack's conditions table already implements** (section 5).
Conclusion: a perk system for M-S5 is the top 21 entry points evaluated at 8 call sites (damage dealt, damage taken, armour rating, crit, spell cost/magnitude, bash, power attack).

## 4. Magic

**MGEF (1,573 winners).** Layout verified (CommonLibSSE-NG member order, offsets measured): `DATA+0` flags, +4 base cost, +8 associated form, +12 skill, +16 resist AV, +64 archetype,
+68 primary AV, +72 projectile, +76 explosion, +80 casting type, +84 delivery, +88 secondary AV.
* **Archetype histogram (37 of the 47 in use):** ValueModifier 368, Script 344, PeakValueModifier 292, DualValueModifier 114, SummonCreature 74, Absorb 57, Stagger 52, Cloak 44, Paralysis 29, Demoralize 27,
  Calm 22, DetectLife 19, Reanimate 14, Frenzy 13, TurnUndead 13, SpawnHazard 13, Invisibility 12, BoundWeapon 9, Rally 8, Disarm 6, Light 6, SlowTime 5, then 15 archetypes with 4 or fewer
  (SoulTrap, Banish, Etherealize, EnhanceWeapon, CureDisease, AccumulateMagnitude 3, Werewolf, ...). The four value archetypes are 1,138 of 1,573 (72 percent).
* **Casting x delivery:** Constant+Self 430, FireAndForget+Self 418, FireAndForget+Aimed 261, FireAndForget+Touch 196, Concentration+Aimed 103, FireAndForget+TargetLocation 80, FireAndForget+TargetActor 32,
  Concentration+Self 28, others 25. 524 effects name a projectile and 160 an explosion.
* **Scripts: 489 of 1,573 (31 percent) have `VMAD`.** Script archetype 202 of 344 do; Absorb 35 of 57; Stagger 17 of 52; ValueModifier 90 of 368; Peak 27 of 292. 518 effects have conditions (1,398 `CTDA`).

**SPEL (1,354 winners).** Types (`SPIT+8`): Spell 676, Ability 402, VoicePower 175 (the word-of-power spells), Power 49, LesserPower 21, Disease 16, Poison 15. Cast: FireAndForget 756, Constant 425, Concentration 173.
Delivery: Self 674, Aimed 448, Touch 129, TargetLocation 66, TargetActor 37. Effects per spell: 1 for 865, 2 for 262, 3 for 125, up to 8. **No SPEL record has `VMAD`**, but 376 spells (27.8 percent) use at least one
effect with a script. 189 spells have conditions (828). Effect uses by archetype in spells: ValueModifier 603, PeakValueModifier 500, Script 284, DualValueModifier 205, Stagger 139, Absorb 127, SummonCreature 76, Cloak 58, Paralysis 35.

**First-slice spells (as stored):**
* **Healing** (Spell, Concentration, Self): two ValueModifier effects on AV 24 and 26, no script, no projectile. FastHealing is the same with FireAndForget.
* **Firebolt** (Spell, FireAndForget, Aimed): effects (1) ValueModifier on AV 24 (Health) with a projectile **and a script**, (2) Stagger, (3) Demoralize with a projectile. Flames (Concentration, Aimed): ValueModifier + Demoralize, no scripts.
* **Ward** (WardLesser/Greater/Steadfast; Spell, Concentration, Self): AccumulateMagnitude (AV 63) + PeakValueModifier (AV 39), no script, no projectile; GMST fMagicWardPowerMaxBase = 1.3.

**ENCH (743).** Weapon enchantments 672, apparel 71. Effects by archetype: PeakValueModifier 536, ValueModifier 164, DualValueModifier 102, Script 95, Absorb 60. 541 have one effect, 12 have six.
3,009 of 3,262 weapons and 2,813 of 3,675 armours name an enchantment (`EITM`; includes template/created-item references, RECALLED-VERIFY).
**SHOU (117) / WOOP (107):** every shout has exactly three words (351 `SNAM`); the words' spells use ValueModifier 221, DualValueModifier 97, Script 88, Stagger 84, Peak 53, SummonCreature 16, Paralysis 15, SpawnHazard 11.

## 5. Weapons, armour, projectiles, combat styles, impacts

**WEAP (3,262).** `DATA` (10): value u32, weight f32, **damage u16** (min 0, median 14, max 10,000; 111 with 0). `DNAM` (100): animation type (+0): HandToHand 19, 1HSword 483, 1HDagger 377, 1HAxe 394, 1HMace 374,
2HSword 393, 2HAxe/Warhammer 752, Bow 341, Staff 93, Crossbow 36 (names RECALLED-VERIFY; the ten values 0 to 9 are in the data). **Speed (+4)** 16 distinct values 0.5 to 10 (0.7 for 750, 1.0 for 713, 0.6 for 371, 0.8 for 372,
0.9 for 356, 1.3 for 346); **reach (+8)** 0.7 (387), 1.0 (1,651), 1.3 (1,223), 1.5 (1); flags (+12) 64 for 1,591 and 0 for 1,551; **min/max range (+28/+32)** 500/2500 for 414 (bows) else 0; skill AV (+76) 6/7/8;
**stagger (+96)** 0 for 850, 0.75 for 467, 0.85 for 387, 1.0 for 332, 1.1 for 383, 1.15 for 346, 1.25 for 365, up to 1.5. Number of projectiles (+26) 1 and attack-animation byte (+25) 255 in every record.
`CRDT` (24): crit damage u16 (4 to 8 most common), crit multiplier f32 (1.0 for 3,160), flags byte 1 for 3,228. Impact link `BIDS` 3,208 weapons, blocking material `BAMT` 3,238, attack sound `SNAM` 49,
equip/unequip sound `NAM8`/`NAM9` 3,241, `VMAD` 19.

**ARMO (3,675).** Rating = `DNAM`/100 (min 0, median 13, max 120; 2,607 above 0). Armour type (`BOD2+4`): Light 1,177, Heavy 1,413, Clothing 1,075. 312 carry the shield slot (bit 0x200), 307 of them with a rating above 0.
2,813 have `EITM`, 46 `VMAD`.
**AMMO (54):** flags 4 (24), 6 (20), 0 (9); damage 0 (12), 10 (12), 14 (7), 16 (3) ... **PROJ (236):** `DATA` 92 bytes for 230; type (+2): Missile 102, Lobber 10, Beam 15, Flame 24, Cone 47, Arrow 38 (no Barrier); gravity 0 for 153, 0.35 for 36,
1.0 for 24, 4.0 for 11; speed up to 99,999; range 37 to 60,000; explosion link in 93. **EXPL (275):** 52-byte `DATA`; 83 with an enchantment link. **HAZD (51).**
**CSTY (195):** blocks `CSGD` general (40 bytes for 186), `CSME` melee (32 for 144, 28 for 50), `CSCR` close range (16 for 186), `CSLR` long range (4), `CSFL` flight (32/20/12/28/4); 195 styles, **3,793 of 6,362 NPCs** name one (152 distinct).
CSGD floats: +24 and +32 are 1.0 for ~150 of 195 (multipliers), +36 is 0.2 for 168 (avoid-threat chance), the offensive/defensive multipliers vary per style. CSME +28 is 0.1 for 117 (special attack multiplier, RECALLED-VERIFY).
**IPCT (663)**: 650 have a sound, 613 a decal, 298 a second sound; **IPDS (313)** 18,184 material-to-impact pairs over 79 distinct materials; **SNDR (3,150)** sound descriptors, 240 with conditions (99.6 percent of those
condition uses are of implemented functions).
**NPC_ combat inputs:** `AIDT` aggression 0/1/2/3 = 3,019/971/2,368/3; confidence 0..4 (4 for 3,413); mood, assistance 0/1/2 = 2,488/1,274/2,599. Essential 343, protected 153, unique 1,285, summonable 75, respawn 3,120 (`ACBS` flag bits: essential 0x2, protected 0x800, from xEdit, RECALLED-VERIFY). 124 NPCs carry their own attack data.
**Conditions in the combat records** (PERK, MGEF, SPEL, ENCH, SNDR, CSTY, WEAP, ARMO, IDLE, PACK: 15,872 conditions, 192 distinct functions): the pack's table (`game_packs/skyrimse/src/conditions.cpp`, `sTable`, 124 to 125 entries) answers **77.9 percent** of the uses
(PERK 84.0, MGEF 86.9, SPEL+ENCH 89.9, SNDR 99.6). Biggest gaps: `IsUndead` (fn 715; 207), `WornApparelHasKeywordCount` (722; 43+), `SpellHasCastingPerk` (713; 75), `EPMagic_SpellHasKeyword` (693; 54), `IsHostileToActor` (719), `IsBlocking` (569), `GetCombatState` (323),
`IsAttackType` (697), `GetGraphVariableInt` (675; 123), `HasLinkedRef` (362; 357) and package-related ones (620, 631, 612). "In the table" is not "answerable": every function needs the `ConditionWorld` fed by real actor state.

## 6. Behaviour

### 6.1 What the files contain

654 non-clip `.hkx` files in `Skyrim - Animations.bsa` (the character files themselves are not in any archive of this install; the project text files name them). 76 distinct classes, 68,971 objects. The player/NPC set
(`meshes/actors/character/behaviors`, 18 files):

| Measure (player set) | Value |
| --- | --- |
| Files / objects / classes | 18 / 19,652 / 55 |
| hkbStateMachineStateInfo (states) | 3,477 |
| hkbStateMachine | 1,140 |
| hkbStateMachineTransitionInfoArray | 1,294 |
| hkbClipGenerator (clip references) | 3,224 |
| hkbClipTriggerArray (annotation events inside the graph) | 2,508 |
| hkbBlenderGenerator / child / hkbModifierGenerator | 588 / 1,933 / 465 |
| hkbVariableBindingSet | 1,885 |
| `BS*` classes (Bethesda-specific) | 16 classes, 684 objects (BSSynchronizedClipGenerator 226, BSIsActiveModifier 95, BSiStateTaggingGenerator 79, BSBoneSwitchGenerator 69, BSEventEveryNEventsModifier 47, BSCyclicBlendTransitionGenerator 45, BSEventOnDeactivateModifier 21, BSDirectAtModifier 11, ...) |
| Largest files | `mt_behavior` 5,115 objects / 1,198 states; `1hm_behavior` 4,794 / 931; `0_master` 2,418 / 336 states / 112 state machines; `magicbehavior` 1,272; `blockbehavior` 895; `staggerbehavior` 112 (2 states); `bashbehavior` 131 (9 states); `bow_direction_behavior` 212 |
| Events (name table) | `0_master` 1,217; `mt_behavior` 931; `1hm_behavior` 461; `horsebehavior` 212; `magicbehavior` 97; others 9 to 44; **1,985 distinct** over the 18 files, 656 containing "idle" |
| Variables | `0_master` 230; `horsebehavior` 118; `1hm_behavior` 101; `magicbehavior` 81; 301 distinct |

Other classes (all 654 files): `hkbPoseMatchingGenerator` 96, `BGSGamebryoSequenceGenerator` 603 (clips driven from the legacy sequence files), `hkbPoweredRagdollControlsModifier` 93, `hkbRigidBodyRagdollControlsModifier` 55, `hkbGetUpModifier` 46,
`hkbKeyframeBonesModifier` 91, `hkbFootIkControlsModifier` 25, `BSLookAtModifier` 128, `BSLimbIKModifier` 1, `hkbEvaluateExpressionModifier` 468 (expression strings evaluated over variables).

### 6.2 Combat events

*Behaviour name tables* hold, among the 1,985: attack 77 names (`attackStart*`, `attackPowerStart*`, `AttackWin*`, ...), block 16, bash 20, stagger 6, recoil 5, hit 10, death 22, ragdoll 6, release 27, draw 13, cast 22, spell 59,
power 43, sprint 26, sneak 14, over 100 `KillMove*` events (cinematic kill moves: out of scope). The master graph is an event protocol: the engine sends an event name, the graph picks a state.

*Sidecar events (`animationdata/defaultmale.txt`: 2,135 generators, 6,086 events, 252 distinct names; `defaultfemale` 6,091; `firstperson` 4,307 over 1,051 generators).* Top: `FootRight` 372, `FootLeft` 371, `00NextClip` 273,
`NPCFootScuff*` 271/254, `SoundPlay` 214, `NPCSoundPlay.NPCKillStruggle` 183, **`HitFrame` 176, `preHitFrame` 165, `weaponSwing` 137, `NPCweaponSwing` 131, `NPCHitFrame` 130, `attackStop` 127**, `FootScuffLeft/Right` 123/108,
`IdleStop` 103, `InitiateWinBegin` 95, `AttackWinStart` 84, `AttackWinEnd` 84, `CastOKStart` 80, `InitiateWinEnd` 75, `AttackWinStartLeft/EndLeft` 48, `attackRelease` 38. By class of clip (name contains):

| Class | generators | with events | events | main events |
| --- | --- | --- | --- | --- |
| attack | 189 | 187 | 1,128 | HitFrame 157, preHitFrame 148, weaponSwing 133, attackStop 123, AttackWinStart/End 81, CastOKStart 78, AttackWinStartLeft 47, attackRelease 38, PowerAttackStop 25, weaponLeftSwing 23 |
| power attack | 52 | 52 | 355 | HitFrame 61, preHitFrame 52, weaponSwing 44, PowerAttackStop 25 |
| block | 43 | 34 | 100 | InitiateWinBegin 20, InitiateWinEnd 19, blockHitStop 13, bashRelease 7, bashExit 7, blockAnticipateStop 6 |
| bash | 23 | 23 | 111 | preHitFrame 15, HitFrame 15, bashExit 14, bashRelease 6, SoundPlay.NPCHumanCombatShieldBash 4 |
| stagger | 28 | 27 | 164 | staggerStop 24, InitiateWinBegin 24 (plus footsteps) |
| recoil | 15 | 15 | 30 | recoilStop 13 |
| bow | 82 | 67 | 200 | arrowAttach 6, bowDrawStart 6, bowDrawn 6, SoundStop.WPNBowPullLPM 6 (the release events `BowRelease`, `BowReleaseFast` are in the behaviour name tables) |
| cast/shout | 158 | 124 | 245 | Voice_SpellFire_Event 12, shoutStop 12, MLh_SpellFire_Event 5, MRh_SpellFire_Event 2 |
| death/kill move | 21 / 121 | 16 / 121 | 122 / 1,903 | NPCKillMoveStart/End, 2_KillActor, paired events |
| equip | 44 | 43 | 212 | weaponDraw 20, WeapEquip_Out 19, BeginWeaponDraw 16, BeginWeaponSheathe 14 |

Attack clips by family: 2HM 63, 1HM 57, 2HW 26, DW 17, H2H 11, other 15. **143 of 189 attack-named clips have exactly one `HitFrame`**, 41 none, 5 more than one. The most common sequences are
`preHitFrame, weaponSwing, HitFrame, CastOKStart, AttackWinStart, attackStop, AttackWinEnd` (18 clips) and for power attacks `preHitFrame, weaponSwing, HitFrame, AttackWinStart, AttackWinEnd, PowerAttackStop` (12). Event times are playback time (F48 of 0054-provenance);
animation time = stored time x speed. Meanings (`HitFrame` = the moment the weapon should damage; `preHitFrame` = start of anticipation/enable hit test; `weaponSwing` = swing sound; `AttackWin*` = window; `attackStop` = recovery end) are
RECALLED-VERIFY from the order and names, not from the data. `AttackWin*` vs the combo/charge window is the least certain.

### 6.3 Assessment: read Skyrim's behaviour graphs, or drive combat from CreativeMotion's graph

**Option A, read and run the behaviour graphs (plan O3).** Needs: a layout for each of 55 classes (16 Bethesda-specific), established from data alone (no schema is in the packfile; the 2010 packfile has no type reflection we use),
a state machine runtime (wildcard transitions, transition effects and blending, event queue ordering), generators (clip, blender, manual selector, pose matching, synchronised clip, state tagging), the modifier set (expression evaluation over 301 variables, ragdoll controls,
look-at and direct-at, foot IK) and a way to feed the 301 variables from game state. Size: 19,652 objects in the player set alone; nothing documents the semantics except the Havok runtime. **Estimate 70 to 130 days plus 60 to 150 of unknown (plan 4.12 O3), with no oracle.** Provenance of
each class layout is a data-only claim, one by one. Benefit: Nemesis/Pandora/FNIS behaviours and custom weapon types would run. Risk: highest in the whole plan.

**Option B, CreativeMotion graph with Skyrim clips and the event names (plan O1+O2).** What it takes for combat: a table of slots (attack by weapon type and direction, power attack, block start/hit/stop, bash, recoil, stagger, bow draw/release, cast, death),
a clip loader (exists for `mt_idle`: `apps/openmw/mwrender/skyrimidle.cpp`, `components/havokpack/idle.hpp`, task 0145; sidecar events and root motion exist: `components/havokpack/animationdata.hpp`), event delivery from clip time to the pack, and graph states in CM. The needed event names are a dozen
(rows of 6.2), settled by the data, and the clips are plain data. Per-weapon attack selection comes from RACE `ATKD`/`ATKE`, weapon `DNAM`, and the clip naming (`1HM_...`, `2HM_...`, `2HW_...`, `DW_...`, `H2H_...`). Estimate 25 to 45 days for all humanoid combat states
(plan 4.12); creatures 15 to 30 more. Loss: mods that replace behaviour graphs. Mods that replace clips keep working through the slot names.

**Recommendation: B for M-S5.** Reasons: (1) every combat fact the milestone needs is in clips + sidecars + records, all of which are decoded or decodable today; (2) A's cost is larger than the whole of the rest of M-S5 and cannot be tested against an oracle;
(3) CreativeMotion is the animation runtime (memory: `creativemotion-is-the-animation-runtime`) and already blends, inertializes and hosts ragdolls; (4) a hit moment is a *timestamp in the clip*, which a data-driven graph delivers exactly (`HitFrame` is ordered after `weaponSwing` in 143 of the 189 clips), so
combat feel does not depend on the graph. Record as D4 for combat (already decided in general, 2026-11-11).

### 6.4 Optional hybrid, later (not for M-S5)

A bake-time reader of five simple classes (`hkbClipGenerator`, `hkbStateMachineStateInfo`, `hkbStateMachineTransitionInfoArray` and its event ids, `hkbBehaviorGraphStringData`, `hkbBehaviorReferenceGenerator`) could produce a table "state -> clip -> event", to
choose clips the way the vanilla graph does, without running the graph. About 8 to 15 days, derived data only; it also tells which attack clip belongs to which weapon animation type. Worth it only if hand-made slot tables prove wrong in play.

## 7. Engine side: what exists to reuse (checked at `c91e219be0`)

| Need | Exists | Where |
| --- | --- | --- |
| Shape and ray queries on the one Jolt world | `castRay`, `castSphere` (main thread) and the SDK's read-only `PhysicsQueryService::castShape` | `apps/openmw/mwphysics/physicssystem.hpp` (l. 328, 333); `sdk/include/openeden/physics.hpp` (l. 111, 169) |
| Ragdolls for dead actors | `RagdollHost` (spawn/remove/readActor, up to 8 actors), `PhysicsSystem::startActorRagdoll`/`readActorRagdoll`, `ActorRagdollDriver` writing bones | `apps/openmw/mwphysics/ragdollhost.hpp`, `physicssystem.hpp` l. 265 to 269, `apps/openmw/mwrender/actorragdolldriver.hpp` (task 0106; the rig is CreativeMotion's humanoid, a Skyrim skeleton needs the bone map of task 0070) |
| Actor bodies, movement, path following in a neutral session | `updateNeutralActors`, `NeutralPath::Follower` (setGoal/follow/cancel), capsule bodies; the pack side `ActorRules` (`onActorAdded`, `update`, `onGoalFinished`) and `ActorRequestWriter` (`setMovement`, `goTo`, `cancelGoal`, `setHeading`) | `apps/openmw/mwphysics/physicssystem.hpp` l. 212, `neutralpathfollower.hpp`, `sdk/include/openeden/game/actor_rules.hpp` v0.2 (tasks 0135 S2 to S4) |
| Conditions | `conditions::Evaluator::evaluate` (OR/AND rules, fail-closed), table of ~125 functions, `SkyrimConditionWorld` | `game_packs/skyrimse/src/conditions.cpp` (l. 960), `include/game_skyrimse/dialogue.hpp` l. 257 |
| Skyrim clips | `HavokPack::sample`, `sampleFloats`, `readAnimationProject`/`readMotionTable`/`linkClip` (events, root motion), `clipHkxPath` | `components/havokpack/animation.hpp` l. 122, `animationdata.hpp` l. 64 to 103, `idle.hpp` |
| An NPC playing an `.hkx` clip | the calm `mt_idle` loop on a worker (task 0145), `SkyrimActor` skeleton and skinned draws | `apps/openmw/mwrender/skyrimidle.cpp`, `skyrimactor.cpp` |
| Animation graph, blending, transitions | CreativeMotion graph/clip/gltf, the pose driver | `components/creativemotion/anim/{graph,clip,gltf}`, `apps/openmw/mwrender/creativemotiondriver.*`, `gltfclips.*` |
| HUD | RmlUi overlay, input, scaling (tasks 0104, 0111) | `components/ui_rml/` |
| Sound for hits | engine sound manager and Skyrim voice path (`say`) | `components/fuz`, the 0143 path (impact sound descriptors are not read yet) |

**Missing for combat (all new, additive):** (1) a way for the pack to ask the core to *play a clip by slot* on an actor and to be told its events, (2) a swing/shape sweep request from the pack to the core,
(3) pack-owned health/stamina/magicka state with the core notified of death (to start the ragdoll), (4) projectile bodies owned by the core, (5) damage and hit events to the HUD. `ActorRules` v0.2 has none of these (it knows position, heading, goals).
Everything is a trailing virtual with a "not supported" default, the pattern of `actor_rules.hpp`.

## 8. Ranked stage plan

Estimates are agent-days, mine, plus or minus 50 percent; they include tests and a frame/trace check but not review rounds. The plan's M-S5 total was 120 to 200; this breakdown gives **134 to 250**.
Process: full loop for every stage touching `mwphysics`, the SDK, ragdolls or saves (root guide); all new behaviour behind a setting off by default (`[Game] neutral combat`), Morrowind compared unchanged as for earlier stages.

| # | Stage | Content | Acceptance (short) | Days |
| --- | --- | --- | --- | --- |
| S5-0 | Contracts and decision | decision record for the combat hooks (extends decision 0017/D3): `ActorRules` 0.3 or a `CombatRules` interface: play-slot request, animation-event callback, sweep query, health/death notification; skeleton of `combat.*` in the pack | SDK versions pinned by tests; docs | 3 to 6 |
| S5-1 | Actor values and derived stats (pure, pack) | AVIF/RACE/NPC_ starting values, Health/Magicka/Stamina, regen with the GMSTs of section 2, damage resist, armour rating (section 5), level/skill derivation for NPCs; no engine | hand-derived tests per formula; real-data census: all 6,362 NPCs get a finite value; formulas RECALLED-VERIFY listed | 10 to 18 |
| S5-2 | Clip slots and event delivery | pack-side slot table (1H right/left attack, 2H, power, block, recoil, stagger, bow, cast) from clip names + RACE `ATKE`; engine plays an attack clip on a Skyrim NPC (generalising the `mt_idle` loader) and delivers sidecar events at clip time to the pack in order | trace of one swing: `preHitFrame, weaponSwing, HitFrame, attackStop` at the clip's times; no allocations per frame | 15 to 30 |
| S5-3 | Melee hit and damage — **FIRST SLICE** | on `HitFrame` the pack asks for a sweep along the weapon's reach (WEAP reach x race reach; angle from `ATKD`), the core casts a shape in Jolt and returns the actor ids; damage = weapon damage with the formulas of S5-1, applied to health; hit reaction clip; stamina cost; death at 0 | NPC vs player (player swings on a timer from the same sidecar, no first-person arms needed) trade hits in the Bannered Mare; health values match a hand calculation; deterministic trace; Morrowind byte-identical | 15 to 28 |
| S5-4 | Block, bash, power attack, stagger | block start/hit/stop (`blockHitStop`, `InitiateWin*`), shield and weapon block with `fBlock*`, bash (`bashRelease`), power attack stamina/damage, recoil/stagger by weapon stagger (+96) | block reduces damage per the GMST formula; bash staggers; power attack costs stamina | 12 to 22 |
| S5-5 | Death and ragdoll | on death start `RagdollHost` for the Skyrim skeleton (bone map from the user's skeleton NIF via task 0070's skeleton map), corpse capsule moved at rest | NPC dies and settles; trace repeatable | 8 to 15 |
| S5-6 | Bow and arrows | PROJ type Arrow/Missile as core-owned Jolt bodies with gravity/speed from PROJ, AMMO damage, bow clips (`bowDrawStart`, `bowDrawn`, `BowRelease`), draw speed/`BowSpeedBonus`, arrow stick/inventory chance | an arrow flies on the PROJ arc, hits an NPC, deals damage | 12 to 22 |
| S5-7 | Perks (combat entry points) | evaluation of the top 21 entry points (80 percent of uses), perk conditions via the existing evaluator fed by real actor state, ability perks | tests per entry point; `HasPerk`/`HasKeyword`/`GetBaseActorValue` answerable | 12 to 22 |
| S5-8 | Magic runtime, first spells | `ActiveEffect` list with deterministic ticks; archetypes ValueModifier, Peak, Dual, Absorb, Stagger, AccumulateMagnitude (ward), Demoralize; casting (FireAndForget, Concentration), projectile spells on S5-6's bodies; **Healing, Firebolt, Ward**; the Firebolt damage effect's `VMAD` script handled natively (decision 3) | the three spells work on an NPC; effects end on time | 20 to 35 |
| S5-9 | Combat AI and styles | `AIDT` aggression/confidence, `CSTY` blocks as data, states: advance, circle, attack choice, block, retreat at low health (`iAICombatRestoreHealthPercentage`), using the path follower and slots | an NPC engages, circles, blocks, flees | 15 to 28 |
| S5-10 | Impacts and sounds | `IPDS`/`IPCT` by material, `SNDR` play, hit/swing/draw sounds | hit plays the material's sound (not heard by an agent: listening check by toddhow) | 6 to 12 |
| S5-11 | HUD | health/magicka/stamina bars, crosshair, enemy health in RmlUi | frames viewed | 6 to 12 |

**First slice = S5-0 + S5-1 (subset) + S5-2 (1H sword only) + S5-3 (the `1HSword` animation type, dummy NPC vs player)**: 35 to 65 days. It needs no Papyrus, no perks, no spells, no behaviour graphs and no first-person arms.

## 9. Decisions for toddhow

1. **Behaviour graphs:** confirm B (CreativeMotion graph + Skyrim clips and events) for combat; accept that Nemesis/Pandora behaviour mods will not work (D4 stands). Optional later hybrid (6.4)?
2. **The player's swing in slice 1:** drive the player's hit timer from the sidecar times without first-person arms (recommended), or wait for first-person clips (771 files in `_1stperson`; extra 10 to 20 days)?
3. **Firebolt's script:** the damage effect has `VMAD` (31 percent of effects do). Recommended: implement the archetype natively and ignore the attached script until the Papyrus world natives exist; or hold Firebolt for Papyrus (plan 4.21: 98 to 180 days).
4. **Contract shape:** extend `ActorRules` (0.3) with animation/health/events, or add a separate `CombatRules` interface (recommended: separate, so the AI/schedule rules and combat rules version independently).
5. **Fidelity of the formulas:** damage, armour mitigation (cap 80), block, stagger chance are RECALLED-VERIFY. Accept "plausible and tunable" for L3, or require an oracle from the real game (a screenshot-and-number protocol on toddhow's install)?
6. **Skeleton and rig for death:** CreativeMotion's rig is the standard humanoid; the Skyrim skeleton needs the bone map of task 0070 (measured in note 0036). Is a baked skeleton map from the user's NIF acceptable?
7. **Scope of M-S5 vs creatures:** humanoids only in M-S5 (dragons, animals and hands-to-hand variants later: creatures 15 to 30 days more).
8. **Reproducibility:** port the Python survey scripts to `game_packs/skyrimse/survey_*` with a gated real-data test (task 0116 pattern, about 4 to 6 days) so the numbers of this note can be re-run in CI, or accept the note as a one-time measurement.
9. **Full loop** on S5-0 to S5-6 (SDK, physics, ragdolls): confirm, no lighter loop.

## 10. Not verified

* All formulas (damage, armour, block, stagger, crit, regen) and the meaning of most flags; the meaning of `AttackWin*`, `CastOKStart`, `InitiateWin*`, and `ATKD` fields beyond their ranges.
* Skyrim's actual use of the leftover GMSTs; `RACE DATA` names beyond the five values compared on three races; `CSTY` field meanings (only value distributions were measured).
* Whether the first two arrays of `hkbBehaviorGraphStringData` are always events then variables (true in `blockbehavior.hkx` by names; the counts of all 18 files plausible but not cross-checked with the event ids used by transitions).
* The 2,508 `hkbClipTriggerArray` events inside the behaviours, the character files, `hkbClipGenerator` to clip-file linkage, creature behaviours (only counted), mod content (not surveyed: five masters only).
* No Linux gate, build, clang-format or tests were run: nothing in the repository but this note changed. No in-game check.
