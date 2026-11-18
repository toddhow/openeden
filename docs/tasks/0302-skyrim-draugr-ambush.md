# Task 0302: Skyrim draugr ambushes (alcove draugr wake, rise and attack through the game's own script)

Design: `docs/tasks/0299-skyrim-dungeon-traps.md` section 4 (the survey and the ranked next steps), `docs/tasks/0289-skyrim-dungeon-mechanics.md`.
Settings: `[extension.skyrimse] ambush` (off; needs `papyrus`, `actor packages`, `combat ai`, `creatures`, `[Game] neutral combat`, `neutral actors`, `neutral actor idles`; declared in `extension.json`).
Windows only. Same hand wrote the tests and the code; no fresh reviewer.

## 1. What the data says (Skyrim.esm, read by a Python walker)

- The ambush draugr (`LvlDraugrAmbush*`, `EncDraugr01Ambush*`; 464 ACHR in the five masters carry `masterAmbushScript`) have **AIDT aggression 0** (the 0299 survey said 2: that is the script's `fAggression`, not the record) and three packages: `ambushSleepPackage` (condition `GetActorValue(Variable01) == 0`: Sleep at the actor's own LINKED reference), `AmbushPatrolLinkCustom01` and `AmbushSandboxEditorLocation512` (condition `Variable01 == 1`: sandbox in 512 units of the editor location).
- The linked reference is a furniture marker: 124 `CreatureAlcoveMarker`, 65 `DragonPriestCoffinMarker`, 42 `CreatureAlcoveBgMarker`, others (spider pipes, falmer pods, centurion ports ...). Bleak Falls Barrow's two draugr (placements 229555 = `000380B3` and 229557 = `000380B5`) lie at `CreatureAlcoveBgMarker` (8844, -5257, -2049) and (8842, -4226, -2047); **no lid or door is involved there**.
- `masterAmbushScript` (decompiled with housecarl; `Variable01` 0 and Aggression 0 while it waits): in its waiting state `OnActivate` by the player, `OnCombatStateChanged` (not 0), `OnHit` and `OnGetUp` (the engine's wake, guarded by `GetParentCell().IsAttached()` and `Is3DLoaded()`) go to the state `allDone`; leaving `waiting` runs `onEndState`: activate the linked ref (three times: `GetLinkedRef`, `GetNthLinkedRef(1)` and `(2)`; the first two are the same reference), then `SetAV("Variable01", 1)`, `SetAV("Aggression", 2)`, `EvaluatePackage()`.
- The draugr's own animations: `actors/draugr/animations/specialidle_alcovelayingidle.hkx` and `specialidle_alcovelayingexit.hkx` (project names `AlcoveLayingIdle` 205 and `AlcoveLayingExit` 204, 5 s, sounds `NPCDraugrAlcoveExitA/B`), `alcove_idle`/`alcove_wake` (standing alcove: `AlcoveStandingIdle`, `AlcoveStanding_Wake`), `actors/dragonpriest/animations/coffin_idle`/`coffin_awake`, and `actors/character/animations/*/special_vertcoffin*`. The draugr animation set `draugr_sleepingalcove.txt` names `IdleSleepLayEnterInstant`, `draugr_standingalcove.txt` `idleSleepStandEnterInstant` (no enter clip: it is placed lying or standing).

## 2. What was missing and what is built

Before: the draugr already lay down in their alcoves (task 0135 furniture), but with the **humanoid** bed clips (`bed_leftsleep`, not in the draugr's folder: the draugr stood in its rest pose), nothing woke them (Aggression 0: the combat AI never looked), `OnGetUp`, `OnCombatStateChanged`, `GetParentCell` and `Cell.IsAttached` did not exist, `SetAV Aggression` and `Variable01` reached nothing but the host's own table.

Built (all behind `ambush`, off by default):
- `game_skyrimse/ambush.*` (pure): `inRange`/`Watch`: a scripted sleeper wakes when the player is within 512 units horizontally and 192 vertically of its bed (OURS, `kWakeDistance`/`kWakeHeight`: the data has no distance), each placement once, in placement order, 8 a call; `aggressionFromValue`.
- `PackageBrain::sleepers` (seated, sleep pose, no clip playing, not talking; the bed's place).
- Papyrus host: `handlesGetUp` (a loaded script, or a parent, whose string table names `OnGetUp`), `onGetUp(actor, furniture)`, `onCombatStateChanged(actor, target, state)`, natives `ObjectReference.GetParentCell` (the cell the data gives) and `Cell.IsAttached` (true for a cell), and `PapyrusWorldHooks::actorValueSet` called by every actor value write (the pack uses it for Aggression only).
- `FollowerState::actorValue` also answers Variable01 (index 0x44, from the host's actor value offsets) and `SkyrimWorldView::setAmbush` gives that overlay to the package conditions, so `ambushSleepPackage` stops passing and the sandbox package passes.
- `SkyrimCombat::setAggression` (a script's number goes through the same `wouldAttackOnSight` rule as a record's; an actor read later gets it when its profile is read; 0 removes the watcher) and `setCombatStateSink` (engage and release; the pack raises `OnCombatStateChanged` with the player as target and state 1 or 0).
- Driver `applyAmbush` (every game update): sleepers of the brain whose script handles `OnGetUp` go to `ambush::Watch`; a woken one gets `OnGetUp(linked furniture)`.
- Furniture clips: a Sleep marker of `CreatureAlcoveBgMarker` has the loop `specialidle_alcovelayingidle` and the exit (quick exit too) `specialidle_alcovelayingexit` with the motion of `AlcoveLayingExit` from the draugr project (`draugrproject.txt` and `boundanims/anims_draugrproject.txt` added to the motion table). **RECALLED-VERIFY: that the Bg marker is the lying one**; only the file names and the set names tell (frames show the draugr lying in the alcove).

The wake is a Papyrus event, so the game's own script decides what a wake means; the combat AI does the rest: the actor engages on sight (aggression 2, relation neutral or enemy), the brain is suspended and the sleeper gets up with its exit clip.

## 3. In game (`tools/skyambush_check/check.sh`, five masters, BleakFallsBarrow01)

- Plan: the player waits at the cell's door until the draugr lie down, is put at (8700, -4350, -2040) facing the bed of 229557, then a picture from his eye every 0.4 s (`I:\Projects\OpenEden-work\combatai\a302\g3`, `probe.png`, `after.png`).
- Seen: draugr lying in their alcoves with the draugr idle before the wake (probe pictures from seven places), then `AMBUSH wake 000380B5`, `Papyrus event OnGetUp to masterambushscript`, the natives (`getparentcell`, `cell.isattached`, `is3dloaded`, `getlinkedref` x2, `activate`, `getnthlinkedref` x3, `setactorvalue` x2, `evaluatepackage`), `OnCombatStateChanged`, `COMBAT ai engage ... (on sight) aggression 2`, `NACTFURN use ... clip specialidle_alcovelayingexit`, `NACTFURN leave`, then attacks (normal, power, bash). The pictures show the figure standing in the alcove, walking to the camera and its enemy bar.
- Determinism: see the CLAUDE.md row.

## 4. Tests

- Written first against stubs (`testskyrimseambush.cpp` and one test pair appended to `testskyrimsenpcactivities.cpp`): **21 of 30 failed** (the 9 that passed test refusals and absence: unknown or out-of-range player, an other actor, a script without the handler ...). After: 30 of 30. Wide filter (`*Papyrus*:*Skyrim*:*Combat*:*Trap*:*Extension*:*Frozen*:*Follower*:*Package*:*Npc*`) 4,091 passed, 0 failed.
- Not covered by a unit test: the driver's `applyAmbush`, the glue's hooks, the world view's overlay and the alcove clip rule (the game run shows them).
- Existing tests: none edited. **Two tests were appended to `testskyrimsenpcactivities.cpp`** (it holds the only sleeper rig); toddhow may want them in a file of their own.

## 5. Decisions for toddhow

1. The wake distance 512 and height 192 are ours; a sneaking player is not treated differently (the sneak system's notice value is not asked).
2. Only a script that handles `OnGetUp` makes a sleeper wake by nearness. Ordinary sleepers (inn guests) still sleep until talked to.
3. `GetNthLinkedRef(1)` is the same reference as `GetLinkedRef()` in the data, so the script activates the marker twice; the default activation toggles, so an `Open` is followed by a `Close` in the same tick. A marker has no animation (`NANIM ... has no animation`), so nothing shows; a real lid would need the activation to be idempotent.
4. Tests appended to an existing file (above).

## 6. Not built, ranked

1. Coffin ambushers (`DragonPriestCoffinMarker`, 65 in the data): `coffin_idle`/`coffin_awake` (dragon priests) and the character `special_vertcoffin*` clips, and the lid (the linked ref's own animation and collision, task 0289's gate rule). Not looked at in game.
2. Standing alcoves (`CreatureAlcoveMarker`, 124): `AlcoveStandingIdle` and `AlcoveStanding_Wake` are the likely clips; the marker still gets the humanoid bed clips (the draugr is in its rest pose).
3. Other ambush kinds: falmer wall pods, spider pipes, spriggans, centurion ports, ice wraiths (same script family, other furniture and clips).
4. A draugr that is hit or activated while asleep (`OnHit` reaches the script already; the player's blow needs a target that is reachable in the alcove), `onReset`.
5. A draugr woken by another's fight (the script raises combat for one placement only), the sandbox package after the fight.
6. The sleepers wake from a list of the brain only: an ambusher in a cell that is not the player's is not looked at.
7. Saves: the script state is saved by the host, but the `ambush::Watch` memory is not (an actor that got up stays up through the script's state, a load puts the sleepers back in bed).
8. The other draugr of the barrow still lie with the humanoid bed clips (rest pose) when their marker is not the Bg one.

## 7. Stage log

See the CLAUDE.md row of 2026-11-18.
