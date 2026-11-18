# Task 0300: crossbows, real bows, dual wielding and dual casting (M-S5)

Design doc section: `docs/tasks/0295-skyrim-weapons-and-magic.md` section 5 (gaps 1, 4 and 5, ranked). Branch
`weapons2-0300` from `0c7a4f870e`. Windows only; the Linux gate, clang-format 14 (formatted by hand), clang-tidy and the
Lua tests were not run. The same hand wrote the tests and the code; no fresh reviewer.

Goal: a bow or crossbow a fighter carries (an NPC) or wields (the player) is shot with the ammunition that fits it, a
crossbow aims, fires and reloads with its own clips, an NPC whose combat style allows it dual wields, and a fire and
forget spell can be cast with both hands.

Inherited code touched (at `0c7a4f870e`): `game_packs/skyrimse/src/combat.cpp` (`weaponFor`, `hasShield`, `meleeClip`,
`beginAttack`, `applyHit`, `startHostiles`, `runPlayerSwing`, `beginBowDraw`, `releaseBow`, `launchArrow`,
`resolveProjectile`, `runPlayerBow`, `onClipFinished`, the pending bow loop of `combatUpdate`), `combat_magic.cpp`
(`beginCast`, `fireCast`, `applyOneEffect`, `resolveSpellShot`), `combat_ai_run.cpp` (the bow test), `combat_hands.cpp`,
`combat_gear.cpp` (`resolveFighterGear`), `skyrimse_game_pack.cpp` (`syncEquipment`, the settings),
`apps/openmw/mwphysics/neutralcombatbridge.cpp` (`validClipName`).

## 1. What the data says (five masters, `SkyrimRangedRealData`, gated)

- 341 bows (animation type 7), 36 crossbows (type 9). Of 6,361 NPCs 1,239 carry a bow and arrows, 15 a crossbow and
  bolts, 1,152 a second one-handed weapon next to their first, and 50 of those have a combat style whose DATA bit 4
  (allow dual wielding) is set (examples: Mercer Frey, Brynjolf, Ralof, the Alik'r warriors).
- AMMO DNAM flags bit 2 (value 4): **set on an arrow, clear on a bolt** (RECALLED-VERIFY, checked by name): 26 of 26
  ammunition named `*Arrow*` have it set, 9 of 9 named `*Bolt*` have it clear; the 10 ammunition of the dwarven
  ballista traps and sphere centurions (flags 6 and 7, not playable) are left out of that count.
- The crossbow clips are in `meshes/actors/character/animations/dlc01/`: `crossbow_aim`, `crossbow_release`,
  `crossbow_reload` and the Dwarven set `crossbow_aimdwarven`, `crossbow_dwarvenrelease`, `crossbow_reloaddwarven`.
  **They have no sidecar events**: the DLC project files (`CrossbowProject.txt`, `DwarvenCrossbowProject.txt`) are not in
  the installed `Skyrim - Animations.bsa` (the Dawnguard archive is not installed). The clip lengths are read from the
  decoded clips: aim 0.2 s, release 2.83 s, reload 2.67 s (as played in game).
- Dual wield power attack clips `dw_attackpower{forward,back,left,right,stab}` exist; `DW_AttackPowerForward` has
  `HitFrame` 0.799 (defaultmale.txt). The normal blow of the left hand is `1HM_AttackLeft` (HitFrame 0.367).
- **No `DualMagic_*Release` clip file exists** (only `dualmagic_idle`); the sidecar names those states but a behavior
  graph layers `MRh_*` and `MLh_*` clips. The core plays one clip per actor, so a dual cast plays the right hand's.
- GMSTs: `fMagicDualCastingEffectivenessBase` 2.2, `fMagicDualCastingCostMult` 2.8, `fMagicDualCastingTimeMult` 1.0.
  No GMST exists for dual wielding.

## 2. What was built (all behind settings that are off by default)

| Setting (`[extension.skyrimse]`) | Does |
| --- | --- |
| `combat ranged gear` | An NPC with `combat real gear` shoots the best bow or crossbow of its inventory (with ammunition that fits it); the player the bow or crossbow he wields, with the best ammunition he holds (spent one unit a shot through the inventory). Crossbow: the aim clip, the bolt leaves as the release clip begins, the reload clip follows; the player cannot shoot again before the reload clip's length has passed and must have aimed all the way (the aim clip's length). The test weapon still comes first (`combat test weapon` may now name a crossbow) |
| `combat dual wield` | An NPC whose combat style allows it (CSTY bit 4) and that carries a second one-handed weapon swings right hand then left hand (`1HM_AttackLeft`, the off-hand weapon's damage and reach); its power attack is `DW_AttackPowerForward` with both weapons' damage added; it has no shield |
| `combat test player offhand` | The player's left-hand weapon (editor id): his normal blows alternate hands. A test aid: the inventory has one weapon slot |
| `combat dual cast` | A fire and forget spell that is aimed or on the caster (not a power) is cast with both hands when the caster can pay 2.8 times its cost: the magnitude of its value effects is 2.2 times, the cost 2.8 times (GMSTs). Every such cast of the player is dual when affordable: there is no second attack control |

Code: `components/.../combat_ranged.*` (pure rules: `isRangedAnimType`, `isBolt`, `ammoFitsWeapon`, `chooseRangedWeapon`,
`chooseAmmo`, `crossbowClipsFor`, `chooseOffHandWeapon`, `dualWieldAllowed`, the dual cast numbers),
`combat_ranged_run.cpp` (`rangedKitFor`, `isDualWielding`, `beginReload`, `playerAimSeconds`), `FighterGear` gains `mBow`,
`mAmmo`, `mOffHand`; `PlayerEquipment` gains `mAmmo` and `mAmmoItem`; `Shot` remembers the weapon and ammunition that
fired it (kits differ per fighter); `SkyrimWorldView::lookupAmmo`, `CombatData::dualCastSettings`. Engine: `validClipName`
also takes the one folder prefix `dlc01/`.

## 3. Checks

- Tests written first and run red against stubs of `combat_ranged.cpp`: **30 of 45 new tests failed** (15 pass against
  the stub by nature: absence, controls). Green: 45 of 45; the Skyrim, combat, neutral, extension and frozen
  `components-tests` (3,839) and the neutral, combat, extension and Skyrim `openmw-tests` (1,188) pass. No existing test
  edited.
- Gated real data (`SkyrimRangedRealData`): the counts of section 1; every ranged weapon an NPC carries has ammunition
  that fits it; the choice is the same twice.
- In game (`tools/skyweapons2_check`, quiet window, volumes 0, Lua and physics on the main thread, fixed frame time;
  scenarios in `/i/Projects/OpenEden-work/weapons2/scen.sh`):
  - Hulda with `combat test weapon = DLC1CrossBow` and `DLC1BoltSteel` (`xbowA`): aim clip 0.2 s, release 2.83 s, reload
    2.67 s, bolt 31.175 damage to the player (19 + bolt, skill 15); frames: `weapons2/keep/crossbow_npc_crops.png` (the
    aim, the arms forward, the release and the reload; the crossbow itself is not drawn). 3 runs, one md5.
  - Dawnguard guards, real gear, `combat test crowd = 6` in `DLC1DawnguardHQ01` (`gearC`): two of them shoot their
    crossbows (33.06 damage), the others use their melee weapons. 3 runs, one md5.
  - The player's equipped `DLC1CrossBow` with six bolts, the real attack control held 1.5 s (`xbowB`): two shots, one
    bolt spent each, the second press during the reload is refused. 3 runs identical but for the combat clock's start
    (the same sequence shifted by 1.5 s: the records worker's readiness, as in task 0265).
  - Dual wield, the player with `combat test player offhand = IronDagger` (`dualwield`): right 7.525, left 4.3, reach 141
    and 98.7. 3 runs, one md5.
  - Dual cast (`dualcast`): Firebolt cost 114.8 (41 x 2.8), magnitude 55 (25 x 2.2); the single cast costs 41 and does
    25. 3 runs, one md5.
  - Off equals before: `n5trade` and `n5bow` with the three new settings unset: the parent's md5s (`642f2593e98e`,
    `e975486f1c27`) appear in the runs made on a quiet machine; the other runs differ by sub-unit positions of the
    sweep and the arrow, as the parent's own runs do (the machine was shared with other builds).
  - Morrowind: `tools/worldobjects_check` 111 lines OK against `integ2`'s build; the fixed Balmora frame is
    `1c5d47ad0b5dc32d` for both.
- Not run: the Linux gate, GCC, clang-format 14, clang-tidy, the Lua tests, the whole test binaries (selected filters),
  a fresh review, a dual-wielding NPC in game (the unit tests and the 50 NPCs of the real-data count stand for it), a
  real bow of an NPC in game, a crossbow frame with the weapon in the hand.

## 4. Decisions and open points for toddhow

1. The crossbow's timing is OURS where the data is silent: no events exist, so the aim is the aim clip's length, the bolt
   goes at the start of the release clip, and the reload clip follows the release clip's end (a cycle of about 5.7 s;
   the real game's may be shorter: RECALLED-VERIFY).
2. The player's ammunition is chosen by the pack (the best that fits what he holds): the inventory has no ammunition
   equip kind. One unit is spent a shot.
3. An NPC with a ranged weapon and real gear shoots at any distance (the test-hostile and crowd rules never swing it);
   the combat AI chooses by its range.
4. A dual wielder's power attack adds the second weapon's damage (OURS); its stamina cost is the main weapon's.
5. The player has no left-hand slot or left-hand spell (`EquipKind` is one weapon): **the left hand, a spell in one hand
   and a weapon in the other, and the player's dual wield from the inventory are NOT built**. Dual casting reads no
   perk, and condition 724 (`EffectWasDualCast`) still answers 0. A dual cast is not animated with the left hand.
6. The crossbow and the bow are not drawn in the hand (appearance holds types 1 to 6), and the player's first-person arms
   have no crossbow clips.
7. A `validClipName` test (`CombatSight`) pins that `a/b` is refused: the new rule allows only the prefix `dlc01/`.
