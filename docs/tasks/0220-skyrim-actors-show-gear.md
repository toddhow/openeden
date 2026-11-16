# Task 0220: Skyrim actors show their gear

Design doc section: `docs/design/game-packs.md`, task 0121 (appearance), 0214 (real weapons).
Goal: a Skyrim actor wears all its armour, and shows its weapon and shield: sheathed at peace, drawn while it fights.
Inherited code touched: none inherited; the Skyrim SE pack and the neutral renderer are OpenEden's own.

## 1. Survey

**Why the Whiterun guard looked bare (found by a gated probe on the five masters).** The guards (`GuardWhiterunImperial*`)
have the default outfit `d33c6`, whose single item is `d33c7`, a leveled list with flags `4` = **use all** and four
entries (the cuirass, boots, helm and shield of one set). `ActorIndex::wornModels` resolved every leveled item with
`pickLeveled`, which picks ONE entry, so the guard wore one piece (the probe printed a cuirass, or a helm, or a shield,
or boots alone for the six guards) and showed the skin of the body, hands and feet for the rest. Task 0214's
`resolveFighterGear` already expanded use-all lists; the outfit path did not. The night lighting and a model read failure
were not the cause (every part model read: "4 of 4 parts drawn"). A shield worn as an outfit item is an ARMA with no
skinned shape, so it was counted "with no skinned shape" and never drawn.

**Where held items go.** `skeleton.nif` (`meshes/actors/character/character assets`) has these nodes: `WEAPON`, `SHIELD`,
`QUIVER`, `WeaponAxe`, `WeaponBack`, `WeaponBow`, `WeaponDagger`, `WeaponMace`, `WeaponSword`. A weapon NIF (`longsword.nif`)
is a `BSFadeNode` with one shape that holds blade and scabbard textures and an extra data string `WeaponSword` (its own
attach point); a shield NIF carries `SHIELD`. Neither is skinned, so they are ordinary static models placed under the node.
There is no scabbard-only file for ordinary weapons (one file in the five masters, Mehrunes' Razor), so the whole model
sits at the sheath node while sheathed and in the hand while drawn (the scabbard is not left on the hip while the blade is
in the hand: limit).

**Draw and sheathe animation.** The archive holds `1hmequip.hkx`, `1hmunequip.hkx`, `h2hequip.hkx`, `bowequip.hkx` ...
(creature folders; the humanoid ones belong to the behaviour graph). They are not played here: the weapon changes place
on the frame the first combat clip starts and back 8 s after the last one.

## 2. Design

* Pack: `ActorIndex::wornModels` expands a use-all leveled list (every entry the level allows, in order, no chance-none).
  `ActorRecords::mWeapons` / `ActorIndex::weaponLook` (WEAP MODL, DNAM type, DATA damage). `resolveHeldGear`
  (`appearance.*`) repeats task 0214's selection (highest damage then lowest form id among the WEAP items of the resolved
  inventory and default outfit; highest-rated shield, none with a two-handed weapon) over the actor index alone, because
  the combat index is read on a worker and may not be ready when an actor is drawn; a test pins that it picks what
  `resolveFighterGear` picks. It is independent of `combat real gear`: what is shown is what `combat real gear` would
  fight with.
* SDK `actor_appearance.hpp` 0.5 (additive, at the end): `heldWeaponModel`, `heldWeaponType`, `heldShieldModel`.
* Pack setting `[extension.skyrimse] held gear` (default false, `extension.json`, test profile). Off: the appearance is
  what it was (the shield stays in `parts`); the use-all outfit fix is not behind it (it is a bug fix of the appearance
  of every neutral actor with a use-all outfit; decision for toddhow).
* Engine: `mwrender/skyrimheldgear.*`. After the actor object is made and before it is compiled, the shield model goes
  under `SHIELD`, the weapon under the sheath node of its type and, in a second place, under `WEAPON`, each in a
  `vsg::Switch`; a controller (auto-play) flips the masks by a pure `DrawClock`. `Scene::playNeutralActorClip` (the combat
  clip path, task 0170) notes a combat clip. Bows, staves and crossbows are not held; arrows and the quiver are not
  drawn. The player has no Skyrim body (a stub Morrowind-record NPC, first person), so nothing is attached to him.

## 3. Stage log

See the end of this file (filled in as the stages ran).

### Stage log (2026-11-16)

* Red runs (pack, against stubs: use-all expansion off, `resolveHeldGear` returning empty): 10 of 17 `SkyrimOutfitUseAll.*`
  and `SkyrimHeldGear.*` failed, the 7 that passed test absence (setting off, plain list, level filter, bow and bare
  hands, no addon for the race, unknown NPC, weapon without a model). All 17 pass with the code. The 8 engine tests
  (`SkyrimHeldGear*` in `openmw_tests`) were written after the code, not mutation-checked.
* `components-tests` `*Skyrim*:*Neutral*:*Extension*:*Frozen*` 2,093 pass; `openmw-tests`
  `*Neutral*:*Skyrim*:*Extension*:*Lua*:*Combat*` 912 pass. No existing test edited. `SDK actor_appearance.hpp` has no
  version pin test (grep), so 0.5 breaks nothing.
* In game (`gear.sh`-style runs, hour 12 and time frozen through the new `GEAR_HOUR` of `n5gear`, `combat real gear`,
  `held gear`): the log lists per actor `Skyrim held gear "..." : weapon WeaponSword, shield yes`; frames
  `gear_h1/user/screenshots/screenshot001.png` (guard in armour, shield on the arm, sword on the hip) and `012` (sword in
  the hand while fighting, shield up); `gear_mare/.../screenshot000.png` (Hulda) and `003` (patron with a two-handed
  weapon on the back). Three runs: md5 of the COMBAT, NCOMBAT and held-gear lines identical.
* Morrowind: Balmora frame `1c5d47ad0b5dc32d` with the setting off and on; `worldobjects_check` not run (no reference
  executable; none of its code changed).

### Edits outside the new files

`pack`: `actors.{hpp,cpp}` (WEAP read, `weaponLook`, use-all in `wornModels`), `appearance.{hpp,cpp}` (`resolveHeldGear`,
`mHeldGear`, `kHeldGearKey`), `world_view.{hpp,cpp}` (`setHeldGear`), `skyrimse_game_pack.cpp` (reads the setting,
includes `appearance.hpp`), `extension.json`; `sdk/.../actor_appearance.hpp` (0.5); engine: `mwrender/scene.cpp`
(attach after the actor is built, note a combat clip), `apps/openmw/CMakeLists.txt`, `apps/openmw_tests/CMakeLists.txt`,
`apps/components_tests/CMakeLists.txt`; tools: `tools/neutral_session_check/check.sh` and `luatest/scripts/n5/geargobal.lua`
(`GEAR_HOUR`), `tools/testprofile/profile.cfg` and `README.md`; `CLAUDE.md` (changelog row).

### Decisions for toddhow

1. The use-all outfit fix changes every neutral actor with such an outfit, with no setting (a bug fix of the first slice).
2. Draw state is "a combat clip in the last 8 s" (OURS), not the AI's engaged flag (the combat contract is pinned at 0.5);
   an AI that is closing in still has its weapon sheathed until its first clip.
3. Bows and arrows, the scabbard left on the hip while drawn, and the hair under a helmet are not done.
