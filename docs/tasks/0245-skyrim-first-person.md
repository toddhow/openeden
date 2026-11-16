# Task 0245: the player's first-person view in a Skyrim session

Status: FIRST SLICE BUILT; Windows only, local commit(s) on branch `firstperson` (from `main` at `c89d7314c1`), not pushed. Not run: the Linux gate, clang-format 14, clang-tidy, the Lua tests, a fresh review (see "Not run").
Design doc section: tasks 0222 (the player's body; its "First person" section is what this builds), 0220 (held gear), 0225 (character creation).
Goal: first person in a Skyrim session shows the race's first-person arms on the first-person skeleton, the weapon in the hand, the arms' own idle, swing and cast clips, and the camera sits on the rig's camera node.
Inherited code touched: `apps/openmw/mwrender/camera.{hpp,cpp}` (an optional first-person path and `getOrientation`), `rendermanager.cpp` (a call and two push_backs), `scene.{hpp,cpp}` (one new method), `skyrimidle.{hpp,cpp}` (a worker load for named files; `loadIdleAsync` now calls it with the same result).
Constraints: off by default behind `[Game] neutral player body` AND the new pack setting `[extension.skyrimse] first person body` (default false, declared in `extension.json`, in the test profile). Morrowind unchanged.
Out of scope (documented below): sprint, sneak, block, bow, staff and two-weapon clips, a draw and sheathe animation, hands' own idle for each spell, the arms' shadows, first-person armour that has no first-person model.

## 1. Survey (data, from the archive listings and the installed files)

* **The skeleton.** `Skyrim - Animations.bsa` / meshes hold exactly one first-person skeleton model, `meshes\actors\character\_1stperson\skeleton.nif` (NIF 20.2.0.7, "1st Person" in its `rigPerspective` extra data). It has the same bone names as the third-person rig (`NPC Spine1 [Spn1]`, `NPC R UpperArm [RUar]`, ...), the hand nodes `WEAPON` and `SHIELD`, the sheath nodes (`WeaponSword` ...) and the camera node **`Camera1st [Cam1]`**. Its Havok skeleton for the clips is `_1stperson\characterassets\skeletonfirst.hkx` (the third-person `skeleton.hkx` is a different file, so the third-person path rules do not apply).
* **The arm models.** ARMA `MOD4` (male) / `MOD5` (female), already decoded (`ArmatureRecord::mMaleFirstPerson`): the Nord skin's addons that have one are the hands (`1stPersonMaleHands_1.nif`, female `1stPersonFemaleHands_1.nif`); the prisoner clothes have `1stPersonPrisonerClothes_1.nif` (female `..._F_1.nif`). On the installed five masters the first-person body of the start outfit is 2 parts (hands + clothes) for both sexes (the gated test prints them).
* **The clips.** `_1stperson\animations\` holds 685 clips with the third-person names (`1hm_attackright`, `mrh_release`, `mt_idle`, `1hm_idle`, `2hm_idle`, `2hw_idle`, `h2h_idle`, `mrh_idle`, `1hm_equip`, ...). The pack's swing and cast clip names (`1HM_AttackRight`, `MRh_Release`) therefore name first-person clips when lower-cased.
* **Eye height.** The camera node stands 120.5 units above the root at rest (probe in game: camera rest (0, 0, 120.5)). The stub player has no skeleton, which is why the old first-person camera had no eye height.
* **Skyrim shows no hands while nothing is drawn**: at rest the arms are below the screen. That is the data (`mt_idle`), not a fault: the arms show once a weapon (or the fists, or a spell hand) is up.

## 2. Design

* **SDK** `openeden/game/player_body.hpp` 0.1 to **0.2** (additive): `PlayerBodyState::firstPerson`, `castToken`, `castClip` (trailing fields the core reads) and a trailing virtual `describeFirstPersonBody` whose default refuses. No virtual was added to `CombatRequestWriter`.
* **Pack.** `makeFirstPersonAppearance` (`player_body.cpp`): the first-person skeleton, the race skin addons that have a first-person model and are not covered, the start clothes and the equipped armour that have one (a piece with one covers its slots; a piece without one is not drawn and covers nothing), no head parts, and the same held weapon and shield rule as the body (one shared helper, `holdGear`). `SkyrimWorldView` / `SkyrimCellSource` answer it when `first person body` is on; the combat counts the player's casts (`playerCastCount`, `playerCastClip`, one hook in `beginCast`).
* **Core.**
  * `skyrimfirstperson.hpp` (pure): clip names and paths, `idleClipFor(weaponType, stance)`, the `Timeline` clock (a loop and a one-shot clip: wait while a clip decodes, 3 s timeout, back to the loop from its start), `rootTranslation`.
  * `ArmsController` (`skyrimfirstperson.cpp`): one auto-play controller; each clip decodes on a worker (`SkyrimIdle::loadClipAsync`, shared by file) and writes the arms' bones.
  * `Scene::makeFirstPersonArms`: the neutral actor builder with the first-person skeleton and parts, the arms controller and the held gear (sheathed copy at the sheath node, drawn copy at `WEAPON`, as an NPC's).
  * `PlayerBody::updateFirstPerson`: builds the arms when the revision changes (only in first person), starts the swing and cast clips from the pack's tokens, chooses the loop (empty hands `mt_idle`; after a swing the weapon's `1hm_idle`, `2hm_idle`, `2hw_idle` or `h2h_idle` for 8 s (`kDrawnHoldSeconds`); after a cast `mrh_idle` for 8 s), and places the arms: turned by the camera's FULL orientation (yaw, pitch) about the rig's camera node, with the node at the eye (`rootTranslation`).
  * The camera: `Camera::setFirstPersonBodyPath` makes `calculateTrackedPosition` use the world position of the arms' camera node in first person (so the clip's camera bob moves the view by up to 1.4 units); empty path = unchanged.
* **Developer aids:** `OPENEDEN_PLAYERBODY_TRACE=1` (a line a second: feet, yaw, forward, arms root, camera, current clip), `OPENEDEN_FIRSTPERSON_CONJ=1` (the opposite turn), `OPENEDEN_FIRSTPERSON_FORCE=1` (build and draw the arms in any camera mode, in the world group).

## 3. Stage log (2026-11-16)

* **Tests first, red counts.** Pack (`SkyrimFirstPerson*`, 12 tests on hand-built records plus one gated real-data test): against a stub `makeFirstPersonAppearance` 11 of 12 failed (the one that passed tests the "no arm models gives no arms" refusal), all pass with the code. Engine (`SkyrimFirstPerson*` in `openmw_tests`, 11 tests: names, idle table, 6 timeline, 2 view): against a stub header 11 of 11 failed, all pass. `SkyrimPlayerBody*`, `SkyrimCharacter*` and `PlayerBody*` still pass (77 and 23 in the two filters). The scene graph half (`ArmsController`, `Scene::makeFirstPersonArms`, `PlayerBody::updateFirstPerson`, the camera path) has no unit test of its own (it needs a renderer); it was checked in game. No existing test was edited.
* **In game** (Bannered Mare, `[Game] neutral actors`, idles, locomotion, combat, inventory, player body; fixed frame time, quiet window, hour 12, 800 x 600; `tools/neutral_session_check/check.sh ... pbody` with `PB_PLAN=fp`): frames viewed:
  * looking down with empty hands after a first swing without a weapon: both bare fists, male and female (female arms are thinner), at the bottom of the view;
  * the sword (Iron Sword, `Weapons\Iron\LongSword.nif`) in the right hand mid-swing, started both by the pack's test rule (`combat test player swing = 3`) and by the real attack key (left mouse button as a synthetic SDL event through `OPENEDEN_RMLUI_INPUT_SCRIPT`, with `[GUI] rmlui`), `PLAYERBODY first person swing 1 clip 1HM_AttackRight`, clip `1hm_attackright` then `1hm_idle`;
  * level view with the arms sheathed shows no hands (the data's own behaviour) and with a sword up in `1hm_idle` the sword is below the view at a level look.
* **Camera.** The camera is at feet + 120.5 (rest) and moves to 218.84 / 220.18 with the swing clip (the node's own bob); with the view pitched 0.6 rad down the arms root moves (0, -68, +21) and the camera stays at the eye (the turn is about the camera node). `OPENEDEN_FIRSTPERSON_CONJ` was not needed: the turn is right as built.
* **Determinism.** Three runs of the real-key scenario give 27 identical lines of `PLAYERBODY`/`PBODY` (build, swing, per-second trace with feet, camera and clip), md5 `fa06c1972103` each (screenshots are not byte-identical between runs; a burst of 30 screenshots keeps only the ones the capture queue holds, 18).
* **Morrowind unchanged.** Balmora first-person frame (`--start Balmora --random-seed 7`, `OPENEDEN_FIXED_FRAME_TIME`, frame 600): SHA-256 prefix `1c5d47ad0b5dc32d` for the parent executable (`integ2` build), this build, and this build with `neutral world`, `neutral actors`, `neutral player body` and the pack settings on.

### The loss and the rebuild

While gathering Morrowind third-person evidence I ran `mw3p.sh` without its arguments; the script then deleted its work folder, which was the whole scratch folder `I:\Projects\OpenEden-work\papyrus` (build tree, logs, the keep frames). The sources were safe in the worktree and were committed first. A full reconfigure and rebuild was started and abandoned: on the shared machine it ran at about 5 steps a minute of 3,748, so the **keep frames were not regenerated**. The frames named above were viewed before the loss and are described, not kept; the numbers (red counts, the md5 of three runs, the Balmora hashes) were read from the runs before the loss. The sources, tests and scenario scripts (`tools/neutral_session_check`, `PB_PLAN=fp`, `KEYFRAME`) are committed, so the evidence can be regenerated by building the branch and running them. The Morrowind third-person comparison was not run.

## 4. Edits outside the new files

`apps/openmw/mwrender/camera.{hpp,cpp}`, `playerbody.{hpp,cpp}`, `rendermanager.cpp`, `scene.{hpp,cpp}`, `skyrimidle.{hpp,cpp}`; `apps/openmw/CMakeLists.txt`, `apps/openmw_tests/CMakeLists.txt`, `apps/components_tests/CMakeLists.txt`; `sdk/include/openeden/game/player_body.hpp`; pack `player_body.{hpp,cpp}`, `world_view.{hpp,cpp}`, `neutral_world.{hpp,cpp}`, `combat.hpp`, `combat_magic.cpp` (a cast counter and the clip, no rule), `skyrimse_game_pack.cpp`, `extension.json`; `tools/testprofile/profile.cfg` and `README.md`; `tools/neutral_session_check/check.sh` and `luatest/scripts/pbody/player.lua` (the `fp` plan); `CLAUDE.md` (changelog row).

## 5. Not built / limits

* No draw or sheathe animation (the weapon changes place at the first swing and 8 s after the last, as an NPC's); no `1hm_equip`/`1hm_unequip`.
* Only the swing and cast clips the pack names are played (the sword and fist attacks, `MRh_Release`, `MRh_SelfRelease`). No block, bash, sprint, sneak, bow draw, staff, spell hold or two-weapon clips; walking and running show no arm motion (the `mt_walkforward` first-person clips exist; not wired).
* A hard cut from the loop to a one-shot clip and back (no blend).
* The first-person arms of a worn armour piece that has no first-person model are not drawn (its third-person model would show the wrong arms); the skin's hands stay.
* No shadow of the arms; no `1st person` field of view change; `[Camera] first person field of view` applies as before.
* Sheathed idle shows no hands (the game's own behaviour).
* A race whose skin has no first-person model (creatures) gets no arms and the camera stays as before.
* Khajiit/Argonian arms through character creation were not run (needs menus and rmlui; the sex and race flow through the same request, the female frame shows the other sex).

## 6. Decisions for toddhow

1. The arms show only after a swing or cast (Skyrim shows no hands while nothing is drawn). A key to draw and sheathe, with the equip clips, is a follow-up.
2. The camera sits ON the arms' camera node, so the clip's bob moves the view (up to 1.4 units here). Keep it, or fix the view and only draw the arms.
3. A piece without a first-person model is not drawn in first person (sleeves of robes, cuirasses of some mods).
4. The hold times (8 s of weapon or spell stance) are `kDrawnHoldSeconds`, ours.
