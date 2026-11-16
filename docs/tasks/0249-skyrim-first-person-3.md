# Task 0249: the player's first-person view in a Skyrim session, third slice

Status: BUILT; Windows only, local commit(s) on branch `firstperson3` (from local `main` at `521a3586a9`, task 0247 included), not pushed. Not run: the Linux gate, clang-format 14, clang-tidy, the Lua tests, a fresh review (see "Not run").
Design doc section: tasks 0245 and 0247 (their "Not built" lists are what this works through), 0222, 0225.
Goal: the hands hold a bow and draw and release it, hold a spell while it is held, show the weapon at the equip clip's own event, and a clawed (Argonian) first-person frame is seen through the real character creation.
Constraints: still behind `[Game] neutral player body` AND `[extension.skyrimse] first person body` (off by default). Morrowind unchanged. No virtual added to `CombatRequestWriter`.

## 1. What was built

* **Bow.** `AppearanceLimits::mHeldBow` (default false; the first-person description sets it) lets `holdGear` hold a type-7 weapon; it takes both hands (no shield). In the arms the bow waits on `WeaponBow` and is held on `SHIELD` (`SkyrimHeldGear::heldSheathNode`, `heldHandNode`; `sheathNodeFor(7)` still answers nothing, an existing test pins it). The pack's `runPlayerBow` bumps two counters; `SkyrimCombat::playerHands()` (new `combat_hands.cpp`, one `trackPlayerHands()` call at the end of `combatUpdate`) publishes a snapshot under a mutex. SDK `player_body.hpp` 0.3 (additive trailing fields): `bowDrawToken`, `bowReleaseToken`, `bowDrawing`, `bowDrawClip`, `bowReleaseClip`. The core plays `Bow_DrawLight` on a draw, loops `bow_idledrawn` while the string is held and `bow_idleheld` otherwise, plays `Bow_Release` on a release. **The arrow is not nocked** (no arrow model on the hand: not cheap).
* **Spell hands.** Fields `castHold` (0 none, 1 charging, 2 holding), `castHoldSeconds`, `castHoldClip`. A hold is a CONCENTRATION spell (a fire and forget spell is a tap and never one): charging while its cast is pending, holding while it goes on. Loops (`handsHoldClip`): `MRh_ChargeLoop`, `MRh_SelfChargeLoop`, `MRh_WardCharge`, `MRh_AimedConcentration`, `MRh_SelfConcentration`, `MRh_WardLoop`. The release clip is the existing cast token's (`MRh_Release`, `MRh_SelfRelease`). `castHoldSeconds` keeps a loop that starts late in phase (`Timeline::setIdle(name, offset)`) and is traced. In this pack the "charge before release" of a held spell is only the 0.17 to 0.37 s before the spell goes (the cast begins at the press), so the charge loop is short and the release clip replaces it.
* **Clip events.** The weapon appears at the equip clip's `weaponDraw` event and leaves at the unequip clip's `weaponSheathe` (`animationdata/firstperson.txt`: `1HM_Equip` 0.5667, `1HM_Unequip` 1.0, `Bow_Equip` 0.5333, `Bow_Unequip` 0.6667, `H2H_Equip` 0). `ArmsController::eventTime` reads the sidecar through the existing `SkyrimClipMeta::loadAsync` (project `firstperson`) on a worker; the pure rule is `weaponShown` (`skyrimhands.hpp`). Until the sidecar is read the weapon waits; a clip with no such event shows it from the start (the old behaviour).
* **Argonian frame** through the real character creation (`tools/firstperson_check`, new; clicks through `OPENEDEN_RMLUI_INPUT_SCRIPT`): four clicks on the race arrow, the name field, `Ria`, Done.

## 2. Stage log (2026-11-16)

* **Tests first, run red against stubs.** Pack `testskyrimseplayerhands.cpp` (16 tests): **10 of 16 failed, the 6 that passed pin absence or defaults**. Core `testskyrimfirstperson3.cpp` (18 tests): **16 of 18 failed, 2 pin absence**. After the code: 16 of 16 and 18 of 18 pass; the wider `*Skyrim*:*Combat*:*Hands*:*Neutral*:*Extension*` filter of `components-tests` (987) and the `*Skyrim*:*Neutral*:*PlayerBody*:*Combat*` filter of `openmw-tests` pass; the 76 first-person, held gear and player body tests of earlier tasks pass unedited.
* **Bow in game** (`fp3/run3.sh`, `PB_PLAN=fp2`, `combat test weapon = HuntingBow`, `combat test ammo = IronArrow`, Hunting Bow 0x13985 equipped, real keys: F, the attack held 7 to 9.3 s, F): log `draw state 0 -> 1 clip bow_equip`, `bow draw 1 clip Bow_DrawLight`, `bow release 1 clip Bow_Release`, `draw state 1 -> 0 clip bow_unequip`. Frames viewed (`I:\Projects\OpenEden-work\combatai\fp3\keep`): `bow_equip_contact.png` (the bow is not there for the first frames and appears about 0.5 s in, at the event), `bow_draw_contact.png` (the left hand holds the bow, the string drawn back), `bow_release_contact.png` (an arrow leaves). A first run with the weapon only equipped through the inventory shows that the pack treats an equipped bow as a melee weapon for the attack key (a `1HM_AttackPower` swing): the bow draw needs the pack's bow weapon, which only the test setting gives (decision 3).
* **Spell hold** (`combat test player spell = Healing`, attack held 5 to 8.7 s): `cast hold 0 -> 1 clip MRh_SelfChargeLoop`, `cast 1 clip MRh_SelfRelease`, `cast hold 1 -> 2 clip MRh_SelfConcentration after 0.4 s`, `cast hold 2 -> 0` at the release.
* **Argonian** (`fp3/keys_arg.txt`): `CHARACTER finished: Ria, race Argonian, male`, then F; frames `keep/argonian_first_person_contact.png`: the scaled green hands rise into view (the hands model of the race, `1stpersonMaleHandsArgonian_1.nif`, task 0247 section 5). The creation window's layout moves with the race description, so the coordinates of the Name field and Done differ per race (Nord: 372 and 407, Argonian: 390 and 426); Done does nothing with an empty name.
* **Determinism.** Bow trace lines (draw state, bow draw, release): runs 3 and 4 equal; run 5 differs in the combat clock of the draw line (6.900 against 6.967) because the pack's combat clock starts on the first update after its records are ready, which moves by a frame between runs (task 0183 fixed it only for runs with a fixed frame time and no wait here); nothing of this task is involved. See the spell runs in section 3.
* **Morrowind unchanged:** see section 3.

## 3. Numbers at the end

Red counts above. Morrowind first-person Balmora frame (`fp/mw.sh`, `--start Balmora --random-seed 7`, frame 600): SHA-256 prefix `1c5d47ad0b5dc32d` with defaults and with every new setting on. Three spell runs (`sp1` to `sp3`): the `PLAYERBODY` draw, cast and hold lines are identical (md5 `37dc9545...` with the held seconds left out; those depend on the pack's clock start). Bow runs: 2 of 3 byte-identical, the third differs only by that clock start. Frames: `I:\Projects\OpenEden-work\combataip3\keep\` (`bow_equip_contact.png`, `bow_draw_contact.png`, `bow_release_contact.png`, `spell_hold_contact.png`, `argonian_first_person_contact.png`).

## 4. Edits outside the new files

`sdk/include/openeden/game/player_body.hpp` (0.3, trailing fields); `game_packs/skyrimse/include/game_skyrimse/{combat.hpp (include, accessor, private members), appearance.hpp (mHeldBow)}`, `src/{combat.cpp (3 lines), player_body.cpp (holdGear), world_view.cpp (1 line), neutral_world.cpp (the new fields), CMakeLists.txt}`; `apps/openmw/mwrender/{skyrimfirstperson,skyrimheldgear,playerbody}.{hpp,cpp}`; `apps/components_tests/CMakeLists.txt`, `apps/openmw_tests/CMakeLists.txt` (one line each, `components_tests` two); `CLAUDE.md` (changelog row). New: `player_hands.hpp`, `combat_hands.cpp`, `skyrimhands.hpp`, two test files, `tools/firstperson_check/`.

## 5. Existing tests that would need editing

None were edited. Holding a bow in the THIRD-person body would break `SkyrimPlayerBody.aBowAndAWeaponWithNoModelAreNotHeld`, and a bow in `sheathNodeFor` would break `SkyrimHeldGear` "types 0, 7, 8, 9, 200 have no sheath"; that is why the bow is first-person only (`mHeldBow`) and has its own node functions.

## 6. Not run / not built

The Linux gate and GCC `-Werror` (traps checked by hand), clang-format 14 (by hand), clang-tidy, the Lua tests, a fresh review, the whole test binaries, the nocked arrow, Khajiit (the same hands model as the Argonian by the data), a female hand, crossbow, staff, two-spell hands, walking and running arms, the equip clip event of a spell hand (none exists). The scene-graph half (`ArmsController::eventTime`, `onceProgress`, the `PlayerBody` wiring) has no unit test (it needs a renderer): checked in game.

## 7. Decisions for toddhow

1. Fire and forget spells have no charge loop: the pack goes at the press plus the clip's event; a real charge needs a combat rule change.
2. The charge and hold loop names (`MRh_*ChargeLoop`, `*Concentration`, `WardLoop`) are my choice among the archive's clips; the intro clips (`MRh_PreAimedCon` and so on) are not used.
3. An equipped bow does not make the attack key draw it (only the test weapon does): the pack's equipment sync only knows melee weapons; fixing it is a combat change.
4. The bow is held on `SHIELD` (left hand node), judged by the frames only.
