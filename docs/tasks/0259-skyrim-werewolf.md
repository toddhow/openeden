# Task 0259: Skyrim werewolf beast form, first slice

Design doc section: `docs/design/game-packs.md` (the Skyrim SE pack), decision 0017 (neutral actors); tasks 0212, 0216, 0246 and
0253 (creature rows), 0222 and 0245 (the player's body), 0228 (race powers and the P key), 0184 (loot), 0171 (the player's swing).
Branch `werewolf` from `014740d12a`. Same hand wrote tests and code; tests first for the pure rules and the combat half, run red
against stubs; no fresh reviewer; Windows only; local commit, not pushed.

Goal: the player can use the power Beast Form, becomes the werewolf for a while (third person, the werewolf's body and clips, claws
with the race's damage), cannot use gear or the inventory window, feeds on a corpse to extend the time and heal, and changes back
when the time is up. Behind the pack setting `[extension.skyrimse] werewolf` (default false; needs `creatures`, `player body`,
`character creation` for the P key, `inventory` for feeding and `[Game] neutral combat`, `neutral actors`, `neutral player body`).

## 1. Survey (installed Skyrim SE data; `SkyrimWerewolfSurvey` (gated by `OPENEDEN_SKYRIMSE_DATA`) and an independent Python reader of the masters and of `Skyrim - Misc.bsa`, decompiled with houseCARL)

- **The power.** SPEL `WerewolfChange` 0x92C48 (SPIT type 2, a power, cost 0, self) has one effect, MGEF `WerewolfChangeEffect`
  0x92C45: **archetype 36** (MGEF DATA +64), associated item 0xCDD84 (the race), VMAD script `WerewolfChangeEffectScript`. The script:
  on the player it starts the quest `PlayerWerewolfQuest` (0x2BA16); the very first change of a new character (`PlayerIsWerewolfVirgin` of
  `CompanionsHousekeepingScript`) only clears a flag and does `Game.SetBeastForm(false)`. Other beast powers: `WerewolfChangeRingOfHircine`
  0xF8306 (a lesser power with the same kind of effect), `WerewolfChangeFX` 0xF8208.
- **The quest and its script `PlayerWerewolfChangeScript`** (quest VMAD of 0x2BA16, 42 properties): `StandardDurationSeconds` 150,
  `DurationWarningTimeSeconds` 15, `FeedExtensionTimeSeconds` 30. Stages: `PrepShift` (SetBeastForm, fast travel off, an image space
  modifier, dispel list, controls off, `ForceThirdPerson`, `ShowFirstPersonGeometry(false)`), `InitialShift` (`SetRace(WerewolfBeastRace)`), and when the
  alias script `PlayerWerewolfScript` gets `OnRaceSwitchComplete` with the beast race, `StartTracking`: unequip everything, equip
  `WolfSkinFXArmor`, `SetAttackActorOnSight(true)`, add the werewolf and crime factions (the crime factions' `SetPlayerEnemy`), stop
  reporting crime, unequip spells and shouts, learn the Howl shout, add the ability `PlayerWerewolfLvl<NN>AndBelowAbility` by player
  level (10, 15, 20, 25, 30, 35, 40, 45, 50 and over: 0x10C6E8, 0x10F186, 0x10C6E9, 0x10F187, 0x10C6EA, 0x10F188, 0x10F189, 0x10F18A,
  0x10C6EB), set the shift-back time = `GameDaysPassed` + 150 s through the time scale, `RegisterForUpdate(5)`. `OnUpdate` warns
  (stage 20) 15 s before the end and ends (stage 100) at the time. `Feed(victim)`: + `FeedExtensionTimeSeconds / 2` for any
  victim, + the other half when the victim has the keyword `ActorTypeNPC` (the Gorging perk adds more), plays `SpecialFeeding`,
  casts the bleeding spell on the victim and `FeedBoost` (`WerewolfFeed` 0xEC357, an effect of magnitude 50 on health) on the player.
  The perk `PlayerWerewolfFeed` (0x2BA1D) calls `Feed` from its Activate entry point. `ActuallyShiftBackIfNecessary`: effects, items,
  `SetRace(PlayerOriginalRace)`, ability and shout removal, factions, crime back on, `Shutdown`.
- **The race** `WerewolfBeastRace` 0xCDD84: skeleton `actors/werewolfbeast/character assets/skeleton.nif` for both sexes (88 bones;
  every clip has 88 transform tracks), skin ARMO `SkinNakedWerewolfBeast` 0xCDD86 -> ARMA `NakedTorsoWerewolfBeast` 0xCDD87 whose MOD2 and
  MOD3 are the one model `MaleBodyWereWolf_1.NIF` (5 shapes: body and head together; z -1.29 to 138.34, so 139.6 high, 145 wide with the
  arms), behaviour project `WerewolfBeastProject`, spells `WerewolfNightEye` and `AbWerewolf` (0xA1A3E). RACE DATA unarmed damage
  **20**, reach 150 (as the log line `claws 20 reach 150`). Attack events listed by ATKE (`attackStartRight`, `AttackStartRightPower`, `AttackStartLeft`, ...).
  The level ability `PlayerWerewolfLvl10AndBelowAbility` has two effects: `AbFortifyUnarmedDamage` (0x424E2; magnitudes 0 at level 10 and
  below, 5, ..., 25, 30, 50, 60 over the levels) and `WereFXFeedBloodHolder`. `AbWerewolf` carries resist disease, fortify stamina,
  fortify health, a heal rate and 2000 carry weight; the numbers reach the combat through task 0241's ability sums (150 health and 200
  stamina at level 1 in game: `COMBAT player ability numbers: vitals 150 0 200 carry 2000`).
- **Clips** (`meshes/actors/werewolfbeast/animations`, 141 files; durations and root motion from the clips and
  `anims_werewolfbeastproject.txt`, events from `werewolfbeastproject.txt`):

| use | file | seconds | notes |
| --- | --- | --- | --- |
| idle | `ww_idlebase` | 9.1333 | generator `WW IdleBase.hkx` |
| walk | `ww_walkforward` | 1.3333 | motion block (cache 93) y 93.4308 |
| run | `ww_runforward` | 0.7333 | motion block (cache 49) y 222.219, so 303 units a second |
| claw (normal) | `ww_attackrightfast` / `ww_attackleftfast` | 1.3333 | generators `WW Attack<Side>FastLow.hkx`, `HitFrame` at 0.41 s |
| claw (power) | `ww_attackrightpowercombo` | 2.2 | `HitFrame` at 0.122 and 0.66 s |
| stagger | `ww_staggersmalll`, `ww_staggermedium` | 1.5, 2.0 | |
| death | `ww_deathtemp` | 2.0 | |
| feeding | `ww_specialfeeding` | 9.5 | not played in this slice |

  **The sidecar names the generators `WW <Clip>.hkx<nn>`** (task 0246 found the werewolf matched nothing): `SkyrimClipMeta::sidecarAliases`
  now tries `WW <clip>.hkx` and `.hkx00` to `.hkx10` for a stem `ww_<clip>` (the first with the clip's duration wins). **The plain
  `ww_attackright` and `ww_attackleft` (1.2667 s) link to none of them** (the cache index of `WW AttackLeft.hkx00` names a block of
  1.3333 s), so the normal claw is the fast one, whose generator `WW Attack<Side>FastLow.hkx` has the right duration.
- **Not found in the data**: nothing says how long the form really lasts in game time (the script converts 150 "real seconds" through the
  time scale and the game days; this slice counts pack seconds); the feed's health is a spell effect over time (magnitude 50), not instant.

## 2. Design

- **`werewolf.hpp` / `werewolf.cpp` (pure).** `BeastClock` (start, tick, warning once at 15 s left, feed adds 15 s or 30 s for a person, capped at
  600 s: ours), `levelAbility(level)`, `playerProfile()` (the creature row of the beast for the player: attack clips carry their creature
  folder, `werewolfbeast::ww_attackrightfast`) and `bareProfile()`, `bareClip` / `clipFolder`. Every number is VERIFIED or OURS in the header.
- **`SkyrimCombat::setPlayerBeast` (`combat_werewolf.cpp`).** The player's bare-hand numbers and sounds are the race's (plus the level
  ability's unarmed fortify, read from the spell), his `mCreature` is the player row (so `meleeClip` gives the beast's clips and the swing
  timing is asked for `werewolfbeast::...`), `weaponFor` ignores an equipped weapon. A power with an effect of archetype 36 is `Works` in
  `racePowers` and `castPlayerPower` calls the glue's hook instead of casting it.
- **The body.** `SkyrimWorldView::setBeastForm` moves the player body's revision and `describePlayerBody` returns the race `WerewolfBeastRace` with the
  creature fields of the row (no outfit, no gear); first person is off while it lasts. The core draws it with the existing creature path
  (`makeNeutralActorObject`), loops the beast's idle, walk and run by the gait, and plays the swing clip the pack names (the folder is
  taken off in `SkyrimCellSource::playerBody`).
- **Engine edits, small.** `Scene::neutralClipMeta` reads a clip named `<folder>::<clip>` from `actors/<folder>/...` with the sidecar
  `<folder>project`; `NeutralCombatBridge::Writer::doClipTiming` accepts that one separator; `SkyrimClipMeta::sidecarAliases` the `ww_` names.
- **The form (glue in `skyrimse_game_pack.cpp`).** `beastStart` (clock, abilities `AbWerewolf` + the level ability merged with the
  stones' through `applyExtraAbilities`, `setPlayerBeast`, `setBeastForm`), `werewolfFrame` (the clock: the warning "Your transformation is
  about to end." and the end "You return to your human form."), `beastFeed`. **Hand-written, not the shipped scripts:** they need
  `Actor.SetRace` with `OnRaceSwitchComplete`, `UnequipAll`, `ForceThirdPerson`, `Game.SetBeastForm`, a quest start with aliases and the
  `CompanionsHousekeepingScript` of the Companions quest line; the pack's Papyrus host answers none of them. The numbers are read from the data (above).
- **The scripts.** `core.skyrimse.werewolf` 1.0 (`werewolf_service.*`: `active`, `seconds_left`, `notification_count`/`notification`, Mutate `feed`).
  `werewolf_player.lua`: third person while it lasts, the claws always out (stance Weapon: the attack control attacks instead of activating), the messages;
  `inventory_global.lua`: while it lasts an activated corpse feeds instead of opening, a container does nothing; `inventory_player.lua`: no `I` window.
- **Settings** (declared in `extension.json`): `werewolf` (false), `werewolf test grant` (the power Beast Form from the start: the Companions
  quest line is not built; it is added to the character service's power list), `werewolf test seconds` (the length of the form).

## 3. Stages and evidence

1. **Tests first, red against stubs.** `testskyrimsewerewolf.cpp`: 20 tests. Against stubs of `werewolf.cpp` (every function a no-op) and
   `combat_werewolf.cpp` (`setPlayerBeast` empty) **16 failed** (clock 7, ability steps, clip names, profile, the three combat swing tests,
   the power support, the hook, the hook-less refusal); the 4 that passed test absence (a human swing has no folder, a form that
   starts under 15 s does not warn) or are the two service tests (the service was written with its tests, not red first). After the red
   run the three combat swing tests were re-keyed (the first helper pressed the attack edge, which never starts a melee swing; the real
   input is `onPlayerAttack`) and the claw clip became the fast one; the profile numbers (strides, speeds, capsule) were added.
2. **Results.** `components-tests` `SkyrimWerewolf*:CombatWerewolf*`: 20 of 20 pass. Wider filter `*Skyrim*:*Creature*:*Combat*:*Shout*:*Extension*:*Neutral*:*Frozen*:*Dragon*:*Werewolf*`: 3,283 pass, 0 fail, 96 gated skips.
3. **In game** (`tools/werewolf_check/check.sh`, Bannered Mare, fixed frame time, quiet window, volumes 0; the REAL keys and clicks as synthetic SDL events through
   `OPENEDEN_RMLUI_INPUT_SCRIPT`: a click on the creation window's Done, **P twice** (the character service offers the race power first: the Nord's
   Battle Cry; the second press is Beast Form), the attack control (left mouse button) four to six times at Hulda, the activate key on her
   corpse, W). Frames viewed and kept in `I:\Projects\OpenEden-work\papyrus\ww_keep`: `01_transform_third_person` (the werewolf stands where the human stood),
   `03` to `06_swing_*` (a side camera: the arm winds up, lunges and returns; the claw clip plays on the body), `07_feeding_message` ("You feed."),
   `09`/`10_walk_side_*` (walking), `11_warning_message` ("Your transformation is about to end."), `13_human_again` ("You return to your human form.").
   The log: `WEREWOLF transformed: 45 s, claws 20 reach 150`, `COMBAT swing player token 6 hit at 0.41 length 1.333`, `COMBAT damage player -> Hulda ...
   20.000 ... health 75 -> 55` four times and `COMBAT death`, `WEREWOLF fed on a person, 59 s left, healed`, `WEREWOLF the form is about to end`,
   `WEREWOLF back to human (the time ran out)` (45 s = 2,700 frames after it began).
4. **Determinism.** Three runs of the scenario (40 s form): see section 6.
5. **Morrowind unchanged and off unchanged:** see section 6.

## 4. Not done / not run

The Linux gate and GCC, clang-format 14 (formatted by hand to 120 columns), clang-tidy, the Lua tests, a fresh review, the Companions quest line (the power
is given by a test setting), saves (the form and its clock are not saved: a load ends it; a saved body would be the human one), the
transformation animation and effects (the form starts at once: no `idlewerewolftransformation`, no image space, no sound), the feeding
animation (`ww_specialfeeding` is not played), the Howl shout and the other werewolf abilities (night eye, Gorging perk, the perks of the Ring of Hircine), the left claw (only the right fast
claw and the right power combo are used), the werewolf's death clip for the player, a block or bash (none exists), the run (sprint) and the
backward, strafe and swim clips, the beast's capsule (the player's own collision is unchanged: 22 units radius), the werewolf as an NPC (no placed werewolf has a row),
`SetAttackActorOnSight` and the crime factions (the player does not turn the town hostile and crime still counts), the barter, crafting, loot
and dialogue windows (only the inventory window and the corpse loot are refused: a conversation still opens, pausing the world), equipping through
other means than the window, the wolf skin armour, the 15 s warning as a sound, a non-Nord stand-in, and the engine half of the clip timing path has no unit test
(`doClipTiming`'s folder name and `Scene::neutralClipMeta`: game only). The Papyrus tolerance numbers of the host were not rerun.

## 5. Edits outside the new files

`game_packs/skyrimse/CMakeLists.txt` (both lists: `werewolf.cpp`, `combat_werewolf.cpp`, `werewolf_service.cpp`), `extension.json` (three settings),
`include/game_skyrimse/combat.hpp` (the include, `BeastForm`, `setPlayerBeast`, `playerBeast`, `setBeastStartHook`, the members and `applyBeastToProfile`),
`src/combat.cpp` (`weaponFor` ignores the weapon; `applyBeastToProfile` after the profile is made), `src/combat_magic.cpp` (archetype 36 is a supported power and
`castPlayerPower` calls the hook), `include/game_skyrimse/world_view.hpp` and `src/world_view.cpp` (`setBeastForm`, `beastNumbers`, `baseIsPerson`, the beast body, first
person off), `src/neutral_world.cpp` (the folder is taken off the swing clip), `src/skyrimse_game_pack.cpp` (`startWerewolf`, the form, the
stones' ability list, the extra power), `apps/openmw/mwrender/skyrimclipmeta.cpp` (the `ww_` aliases), `apps/openmw/mwrender/scene.cpp`
(`neutralClipMeta`'s creature folder), `apps/openmw/mwphysics/neutralcombatbridge.cpp` (one separator in a clip name),
`files/data/builtin.omwscripts` and `files/data/CMakeLists.txt` (`werewolf_player.lua`), `inventory_global.lua`, `inventory_player.lua`,
`apps/components_tests/CMakeLists.txt` (two test files, three pack sources), `tools/werewolf_check` (new), `tools/testprofile/profile.cfg`
and `README.md` (a comment and a line), `game_packs/skyrimse/CLAUDE.md`, `CLAUDE.md` (changelog row). **No virtual was added to
`CombatRequestWriter`; no SDK header changed; no existing test was edited.**

## 6. Final checks

- `components-tests` `SkyrimWerewolf*:CombatWerewolf*`: 20 of 20 pass; `*Skyrim*:*Creature*:*Combat*:*Shout*:*Extension*:*Neutral*:*Frozen*:*Dragon*:*Werewolf*`: 3,283 pass, 96 gated skips, 0 fail.
  `openmw-tests` `*Neutral*:*Clip*:*Extension*:*Lua*:*Skyrim*:*Combat*`: 1,117 pass. (Whole binaries were not run.)
- `CI/check_layer_rules.sh` 104 exceptions (unchanged), `check_game_conventions.sh` ok, `tools/testprofile/coverage.sh` ok (the settings are pack settings: a comment and a README line).
- **Determinism:** three runs of `tools/werewolf_check` (40 s form; the creation click, P twice, six claw swings, the feed, W): the 104 lines of `WEREWOLF`, `COMBAT swing/hitframe/damage/death`,
  `PLAYERBODY`, `WW259` and the activation trace are byte-identical (md5 `2419fee40308` three times). A fourth run in the same batch had no beast: the creation window opened
  after the scripted click on Done (its timing depends on a worker, as in task 0225), so nothing followed.
- **Morrowind unchanged:** `tools/worldobjects_check` 111 lines OK against the `integ2` build of `main` (not a build of the exact parent); the fixed Balmora frame (`--start Balmora --random-seed 7`,
  `OPENEDEN_FIXED_FRAME_TIME`, `--auto-screenshot 600`) SHA-256 `1c5d47ad0b5dc32d` for the `integ2` build, for this build with the werewolf settings off, twice, and with them on. (An earlier run of
  this build gave `7c35223cf704b8c2`; the same sources rebuilt, and the same build with the engine files of this task put back to the parent's and then restored, gave `1c5d47ad0b5dc32d`
  both ways, so that run was a stale link, not a change.) With `werewolf` off the glue creates nothing, `castPlayerPower`'s loop finds no archetype 36 effect on an ordinary power
  and the world view's beast flag stays false.

## 7. Decisions for toddhow

1. **The shipped scripts are not run.** The form is hand-written from the scripts' numbers. Running `PlayerWerewolfChangeScript` needs `SetRace` on the
   player (a body rebuild with `OnRaceSwitchComplete`), `UnequipAll`, `ForceThirdPerson` and the Companions housekeeping quest; each is a native
   the host would have to answer. The hand-written form is smaller and runs; the scripts would also change a saved game's race.
2. **The clock counts pack seconds, not game days.** Vanilla converts 150 "real" seconds through the time scale and compares game days
   (so a wait or a sleep ends it); here a wait does not move it.
3. **The normal claw is `ww_attackrightfast`, not `ww_attackright`.** The plain attack's generator cannot be linked by name and duration; the fast one can.
   A swing of the left claw (`ww_attackleftfast`) is linked (the alias exists) but no rule alternates the hands.
4. **The power is granted by a test setting and needs the character service** (the P key and its daily cooldown), and it competes with the race's
   first power (a Nord presses P twice). A direct key for Beast Form would be a small Lua script.
5. **A conversation is not refused while a beast** (vanilla NPCs flee or fight); an activated corpse feeds, a container does nothing.
6. `creatureProfileFor` does not know the beast (`playerProfile()` is the player's alone); a werewolf NPC would need a row there and
   the clip aliases already exist.
