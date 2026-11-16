# Task 0247: the player's first-person view in a Skyrim session, second slice

Status: BUILT; Windows only, local commit(s) on branch `firstperson2` (from local `main` at `eaf0432340`, task 0245 merged), not pushed. Not run: the Linux gate, clang-format 14, clang-tidy, the Lua tests, a fresh review (see "Not run").
Design doc section: task 0245 (first slice; its section 5 "Not built" is what this works through), 0222, 0220, 0225.
Goal: F draws and sheathes the hands with the first-person equip and unequip clips, clips cross-fade instead of cutting, and the arms of every playable race are checked.
Inherited code touched: none under `components/`. `apps/openmw/mwrender/{playerbody,skyrimfirstperson,skyrimheldgear}.{hpp,cpp}`.
Constraints: still behind `[Game] neutral player body` AND `[extension.skyrimse] first person body` (off by default). Morrowind unchanged. No SDK change, no virtual added to `CombatRequestWriter`.

## 1. What was built

* **Draw and sheathe (task 0245 decision 1, approved).** The engine's toggle-weapon key (F, the Lua stance control) writes `MWMechanics::CreatureStats::DrawState` of the stub player. `PlayerBody::updateFirstPerson` reads it (`playerDrawState`) each frame; a change plays the clip of `SkyrimFirstPerson::drawTransitionClip`: `1hm_equip`, `dag_equip`, `axe_equip`, `mac_equip`, `2hc_equip`, `2hw_equip`, the fists' `h2h_equip` (and the `*_unequip` twins), `mrh_equip` / `mrh_unequip` for a spell hand, weapon to spell and back raising the new hand. All names are in `_1stperson/animations` of `Skyrim - Animations.bsa` (listing kept at `I:\Projects\OpenEden-work\combatai\fp\fpclips.txt`; 751 clips). Sheathing clears the old holds. `bow_equip`/`bow_unequip` are in the table for weapon type 7, which no pack gives the arms yet (see 4).
* **The stance** is `stanceFor(drawState, swingHold, castHold)`: the engine's draw state decides; the 8 s holds after a swing or cast (task 0245) only cover a player who has not drawn (an attack works without drawing in this build, as before).
* **The weapon in the hand** is no longer the 8 s clock for the arms: `SkyrimHeldGear::DrawClock` has an optional forced state (`force`/`unforce`, atomic, default off, so NPCs are unchanged) and `setDrawn(keepAlive, bool)`; the arms force "drawn" while the weapon stance holds and while the unequip clip that puts it away is still playing.
* **Blends.** `Timeline::pick` now also answers the clip the arms come from (`Frame::from`, `fromTime`, `blend`) for `kBlendSeconds` = 0.15 s (OURS) whenever the shown clip changes (swing starts, swing over, loop replaced, swing replaced by a swing). A one-shot source is held at its last pose, a loop source keeps running. `ArmsController` samples both clips and mixes per bone (`blendTransform`: linear translation and scale, normalised quaternion mix along the short way); a bone the old clip does not move takes the new pose; a source clip that has not decoded gives the old cut. A switch during a fade starts a new fade from what was shown (a small pop is possible then).
* **Prefetch.** At each rebuild the idle, equip and unequip clips of the weapon type are decoded on workers (`ArmsController::prefetch`), so F does not wait for a read.
* **Developer aids (tools only):** `tools/neutral_session_check` plan `fp2` (draw, swing, sheathe; `PB_BURSTS`, `PB_STEP`, `PB_COUNT`, `PB_PITCH`), plan `fp` keeps its meaning (`PB_STEP`/`PB_COUNT`/`PB_PITCH` now settable), `PB_RACE`/`PB_FEMALE`/`PB_RACE_AT`/`PB_DRAW_AT` (see 3, Khajiit).

## 2. Stage log (2026-11-16)

* **Evidence of task 0245, regenerated** (its agent lost the folder). Scenario `PB_PLAN=fp`, real attack key (left mouse button as synthetic SDL events, `[GUI] rmlui`), fixed frame time, quiet window, hour 12, 800 x 600, `framerate limit = 20`, master and music volume 0. Kept in `I:\Projects\OpenEden-work\combatai\fp\keep`:
  * `baseline0245_fists_swing_contact.png`, `baseline0245_fists_swing_full.png`: bare fists mid-swing (`1HM_AttackRight` with no weapon);
  * `baseline0245_sword_swing_contact.png`, `baseline0245_sword_swing_full.png`: the Iron Sword (`Weapons\Iron\LongSword.nif`) in the hand mid-swing; log: `PLAYERBODY first person swing 1 clip 1HM_AttackRight` after the `KEYSCRIPT tap down`.
  * Morrowind frame: Balmora first person (`--start Balmora --random-seed 7`, `OPENEDEN_FIXED_FRAME_TIME`, frame 600, `fp/mw.sh`): SHA-256 prefix `1c5d47ad0b5dc32d` with defaults and with `neutral world`, `neutral actors`, `neutral combat`, `neutral player body`, `player body`, `first person body` on. That is the hash recorded for the earlier builds; no build of the exact parent was made in this tree.
* **Draw, swing, sheathe** (`PB_PLAN=fp2`, items `00012eb7` equipped; F at running frame 240, the attack at 480, F at 690; `fp/keys_draw.txt`): the log reads `draw state 0 -> 1 clip 1hm_equip`, `swing 1 clip 1HM_AttackRight`, `draw state 1 -> 0 clip 1hm_unequip`. Frames (viewed): `keep/fp2_draw_contact.png` (hands empty, then rising, the sword coming up into view and the idle pose), `keep/fp2_swing_contact.png`, `keep/fp2_sheathe_contact.png` (the sword in the hand while the unequip clip lowers it, then the view clear).
* **Determinism.** Three runs of the draw/swing/sheathe scenario give identical `PLAYERBODY`/`KEYSCRIPT` lines (md5 `25d54bce5f10fb8f1db75909f960141c` for each; screenshots are not byte-compared).
* **Blends:** seen in the contact sheets (the hands move continuously between the clips); the pure fade is pinned by 8 `SkyrimFirstPersonTimelineBlend*` tests and 4 `SkyrimFirstPersonBlendPose*` tests. No trace of the weights was logged.

## 3. Tests

* New `apps/openmw_tests/mwrender/testskyrimfirstperson2.cpp` (27 tests: draw clip names, transitions, stance, weapon in hand, blend maths, the timeline fade, `playingOnce`, the forced draw clock and `setDrawn` over real `vsg::Switch` nodes). **Written first against stubs that answer nothing: 21 of 27 failed, the 6 that passed pin absence** (a first pick has no fade, a not-ready clip has no fade, the fade only adds fields, an unforced clock behaves as before, unforcing, opposite-sign quaternions against the stub). All 27 pass with the code; the 12 + 5 tests of tasks 0245/0220 and `SkyrimPlayerBody*`/`PlayerBody*` still pass (58 together).
* New gated `SkyrimFirstPersonRacesRealData` (`apps/components_tests/skyrimse/testskyrimsefirstperson2.cpp`, needs `OPENEDEN_SKYRIMSE_DATA`): the arms of the ten playable races (RACE 0x13740 to 0x13749), both sexes.
* No existing test was edited or deleted.
* The scene-graph half (`ArmsController` blending, `PlayerBody` wiring) has no unit test of its own (needs a renderer); checked in game.

## 4. Not built, and what it takes

* **Khajiit and Argonian hands through character creation (task 0225): frames NOT obtained.** The character service accepted the requests from a script (`SkyrimCharacterRequest` race index 5 = Khajiit, female, finish: `CHARACTER finished: Dragonborn, race Khajiit, female`, body rebuilt as `7 parts, female`), but with `menus` on the session sits under the main menu and the world does not run after the window closes (no simulation, no first-person arms, no frame). Getting past it needs the real clicks of task 0207 and 0225's menu flow (`tools/racemenu_check` with an input script), which I did not have. Instead the data is checked (gated test above; result in section 5). The `PB_RACE` aids stay in the tools.
* **Bow.** Needs (1) a pack answer that holds a type-7 weapon (`holdGear` only takes types 1 to 6) with the bow model in the left hand node, (2) player bow draw/hold/release tokens in `PlayerBodyState` (a new trailing field; the pack's player bow machine in `combat.cpp` has `mPlayerDrawing` and the draw time, but nothing publishes them), (3) the clips `bow_drawlight`, `bow_idledrawn`, `bow_release` timed by the bowDrawn event. The equip/unequip names are already in the table.
* **Spell hold clips** (`mrh_readyloop`, `mrh_precharge`, `mrh_wardloop`, concentration loops): the cast token is a one-shot; a hold needs a "casting since" field from the pack. Draw and sheathe of the spell hand and `MRh_Release` exist.
* Walking and running arm motion (`mt_walkforward`, `mt_runforward` first-person clips exist), block, bash, sprint, sneak, two-weapon clips: not wired.

## 5. Real-data result of the race check

`SkyrimFirstPersonRacesRealData` passes on the five masters (14 first-person tests pass together): all ten playable races, both sexes, get 2 parts (hands + prisoner clothes). **Khajiit (0x13745) and Argonian (0x13740) get `1stpersonMaleHandsArgonian_1.nif` / `1stPersonFemaleHandsArgonian_1.nif` (the clawed hands); the other eight races get `1stpersonMaleHands_1.nif` / `1stPersonFemaleHands_1.nif`.** So the data path is right; what was not seen is the frame (section 4). Design note: every playable race but those two shares one pair of hands, so skin colour comes from the model's own texture set, not a race tint.

## 6. Edits outside the new files

`apps/openmw/mwrender/skyrimheldgear.{hpp,cpp}` (forced draw state: three small members and `setDrawn`), `skyrimfirstperson.{hpp,cpp}` (timeline fade, draw clips, blend, prefetch), `playerbody.{hpp,cpp}`; `apps/openmw_tests/CMakeLists.txt`, `apps/components_tests/CMakeLists.txt` (one line each); `tools/neutral_session_check/check.sh` and `luatest/scripts/pbody/player.lua` (plan `fp2` and `PB_*` fields); `CLAUDE.md` (changelog row).

## 7. Not run

The Linux gate and GCC `-Werror` (traps checked by hand), clang-format 14 (formatted by hand), clang-tidy, the Lua tests, the whole `openmw-tests` and `components-tests` binaries (the filters named above), a fresh review, a Khajiit/Argonian frame, a bow or spell hold, a weapon change while drawn (rebuild keeps the draw state), F pressed while a swing is playing.

## 8. Decisions for toddhow

1. The fade time 0.15 s (`kBlendSeconds`) is ours; the hands switch with a linear mix, not Skyrim's own blend times.
2. The weapon model appears in the hand at the start of the equip clip and leaves at the end of the unequip clip (Skyrim switches it at an event inside the clip; the sidecar events could place it exactly).
3. An attack without drawing still shows the hands for 8 s (the old rule), which Skyrim would not allow; the real fix is to refuse the attack until drawn, a combat rule.
4. The first-person arms follow `CreatureStats::DrawState` of the stub player, which the Lua stance control writes; any other writer (quick keys, spells) works the same.
