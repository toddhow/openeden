# Task 0299: Skyrim dungeon traps, second slice (dart traps that hurt; draugr ambushes surveyed)

Design doc section: `docs/tasks/0289-skyrim-dungeon-mechanics.md` section 3 (the ranked "not built" list, items 1 and 2).
Goal: the dart traps of Bleak Falls Barrow fire when the pillar puzzle is answered wrong, through the game's own `DartTrap`/`TrapBase` scripts, and a script's `Weapon.Fire` becomes a real projectile that hurts through the combat runtime.
Inherited code touched: `components/script_papyrus/papyrusruntime.cpp` (the auto state of a child script), `apps/openmw/mwphysics/neutralcombatbridge.cpp` (`doLaunchProjectile`, the result delivery). Pack: `papyrus_host.*`, `papyrus_world.*`, `combat_traps.cpp` (new), `world_view.*`, `skyrimse_game_pack.cpp`. SDK: `combat_rules.hpp` (`ProjectileRequest::ownerless`, a trailing field, the contract version stays 0.6).
Settings: none new. Behind `[extension.skyrimse] dungeon mechanics` (task 0289) and `papyrus` as before; the shot needs `[Game] neutral combat`.

## 1. Why `Weapon.Fire` was never reached (found by running it)

Three faults in a row, each hiding the next:

1. **The runtime started a child script in the empty state.** `DartTrap extends TrapBase`; `TrapBase` has `auto State Idle` holding `OnActivate`, `DartTrap` names no auto state. `Runtime::attach` took the auto state of the instance's own script only, so the lever's `Activate` reached `OnActivate` of the port (the new `OPENEDEN_PAPYRUS_NATIVE_TRACE` event lines show it) and nothing ran: the empty state has no such event. Now the nearest ancestor with an auto state names the start state (`papyrusruntime.cpp`; a general runtime fix, it changes any script that inherits an auto state).
2. **`GetNthLinkedRef` was not a native** (tolerant, zero), so the trap's random port was None and the shot had no source. Now `ObjectReference.GetNthLinkedRef(n)` walks the unkeyworded chain (bounded 256).
3. **A WEAP and an AMMO had class `Form`**, so `dartWeapon.fire(...)` logged "class 'Form' has no function 'Fire'". `SkyrimWorldView::scriptClassOf` now says `Weapon` and `Ammo`.

## 2. What was built

- `Weapon.Fire(source, ammo)` reaches the new hook `PapyrusWorldHooks::fireWeapon` with the weapon, the source, the ammunition, the source's place and its angles (a script's `SetAngle` first, else the placement's own stored angles: `PapyrusGameData::referenceRotation`; the view keeps rotations of scripted placements and of every link target, `SkyrimWorldView::referenceRotation`). The pack queues it (`ScriptCombatBridge::queueShot`) and the next combat update fires it with the combat's own writer.
- `SkyrimCombat::fireTrapShot` (`combat_traps.cpp`): the PROJ of the ammunition gives speed, gravity and range; the shot is `ProjectileRequest::ownerless` (new trailing field; the core accepts an invalid owner only with it and refuses the flag with a real owner), flies from the ProjectileNode of the port, hurts the first actor or the player it meets (`resolveTrapShot`, `applyTrapDamage`: (weapon + ammunition damage) x 1 after the victim's armour, a reaction clip or a death; no attacker, no perks, no sneak bonus, no provocation) and sticks in a wall like an arrow. The bridge delivers an ownerless result (it dropped every result whose owner was not a known actor).
- `trapAimOf` (stored Euler angles to the engine's heading and pitch, the convention of `eulerToQuat`; the X sign is RECALLED-VERIFY) and `trapMuzzle`: **the dart port's NIF has a `ProjectileNode` at (0, 16, 0) with no rotation** (read from the node block of `meshes/traps/trapdartportsingle01.nif`); a dart that started at the port's origin hit the port's own collision after 5 units in every one of 35 shots (seen in game), so it starts 16 units along the facing.
- `OPENEDEN_PAPYRUS_NATIVE_TRACE=1` also prints `Papyrus event <name> to <script> of <ref>` for each queued event.

## 3. In game (`tools/skydungeon_check`, five masters, BleakFallsBarrow01, lever pulled with the pillars unsolved)

- Before: the four ports and their child ports animate, no dart.
- After (`I:\Projects\OpenEden-work\combatai\d299`, plans `plan3.txt`, `plan5.txt`): 35 trap shots leave the 4 ports and their chains (the first lines: `COMBAT trap shot launch token 1 from 5042.979 -1287.662 -481.833 heading 0.000 pitch 0.000 speed 1300.000`), cross the room (ends at (5042.98, -715.25, -766.07) after 570 units, others at the far wall or the floor, one in the air at the player) and one hit the player in the middle of the room: `COMBAT trap damage -> player 1.000 health 100.000 -> 99.000 of 100.000`. The damage is small because the level-1 dart weapon and its ammunition have 1 damage in the data; nothing is hand-tuned.
- Determinism: see the stage log.

## 4. Draugr ambushes: surveyed, NOT built

- `masterAmbushScript` sits on 15 or more ACHR of the cell (`000380B3` and its neighbours), bases `LvlDraugrAmbushMelee1HMale`, `...2HHelmetMale` and the like: aggression 2, hostile on sight, so the combat AI would engage them if it saw the player.
- In four runs (player teleported 50 to 350 units from `229557`/`229555`, `combat ai`, `actor packages`, `quest alias packages`, with and without `combat factions`) **no draugr engaged the player**: the sight ray is the likely blocker (the actors stand in coffin alcoves behind the sarcophagus lids, which the game opens by activating the actor's linked reference: `onEndState`, the lid is the actor's linked ref; the lid's `trigger01` clip converts but nothing plays it and its collision stays). They do not sleep in furniture either: the package brain gives them a wander goal and they walk.
- What the script needs that the pack does not raise: `OnCombatStateChanged` and `OnGetUp` (the exits from `waiting`), a script's `SetAV("Aggression", n)` reaching the combat AI (`SetAV` is stored by the host, the AI reads the NPC_ aggression only), and an `EvaluatePackage` that matters (it is a quiet no-op).
- Ranked next steps: (1) ambush furniture: the draugr's PACK and its furniture use (coffin markers `DragonPriestCoffinMarker`, alcoves), (2) the lid: play `trigger01` on a linked activation and drop its collision (task 0289's gate rule), (3) raise `OnCombatStateChanged` from the combat AI's engage and release, (4) forward `SetAV Aggression`.
- Not built either: swinging blades, pressure plates, the oil pool, trigger boxes dealing damage (items 3 of task 0289; the shot path is what they need for projectile traps; blades need a hit volume).

## 5. Tests

- Red first (against stubs): `PapyrusAutoState` 2 of 4, `CombatTraps` 6 of 9, `TrapAim` 3 of 4, `SkyrimDungeonTraps` 5 of 8: **16 of 25 failed**, the 9 that passed pin refusals and absence.
- Written WITH the code, never run red: `SkyrimDungeonTraps.GetNthLinkedRef...`, `TrapAim.theMuzzleIsSixteenUnitsAlongTheFacing` (the first red run was before they existed), the six `OwnerlessShot` tests of `openmw-tests` (`mwphysics/testneutralownerlessshot.cpp`: the bridge was edited before the first build of `openmw-tests`, so no red run against the old bridge exists; they exercise acceptance, the refusals, a hit on the player, on an actor and an expiry). The world-view rotation table and the `Weapon`/`Ammo` classes have no unit test (they need the plugin walk); the game run shows them.
- Runs: `components-tests` filter `*Papyrus*:*Skyrim*:*Combat*:*Trap*:*Extension*:*Frozen*` 4,014 passed (112 gated skips); `openmw-tests` filter `*Neutral*:*Projectile*:*Combat*:*Extension*` 979 passed.
- No existing test edited.

## 6. Stage log

See the CLAUDE.md row of 2026-11-18.
