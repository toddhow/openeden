# Task 0254: Skyrim deaths are saved (a fix of the neutral saves of task 0177; follow-up of 0184, 0250, 0174, 0195)

Status: IN PROGRESS (see the stage log at the end). Windows only, local commits on branch `deaths` from `5986a356f8`, not pushed.
Not behind a new setting: it fixes a bug in the existing `[Game] neutral saves` path (a Skyrim NPC killed in a session came back
alive after a save and a load: found by task 0250, decision 1). It needs `[Game] neutral combat` (without it nobody dies).

Goal: an actor that died stays dead across a save and a load: it lies where it fell, its loot opens, a corpse a Reanimate spell
hid is a corpse again, and an ordinary (not unique) actor stands again once its cell would have reset.

## 1. Where the state lives (decision)

Three places were possible.

- **The engine's `NWLD` record (task 0177).** It saves the difference between a placement and the pack. It could hold a Dead flag
  and the position of a dead actor. Rejected: the respawn rule needs the actor's Unique and Essential flags and the game clock,
  which only the pack knows; an actor that respawned would keep the corpse's position from the engine's record; and every
  change of the record's format is a version bump of a strict decoder.
- **The pack's own save chunk `deaths` v1 (chosen).** The pack knows who died, when (game hours), where the body lay and
  whether a Reanimate spell hid it, decides the respawn itself and needs no engine format change. The only engine addition is
  one request, `ActorRequestWriter::placeCorpse` (SDK `actor_rules.hpp`, a trailing virtual with a refusing default, version
  number unchanged, like `placeFurniture` before it), which puts the body where it lay (position and heading, no movement request).
- A save record per cell: rejected, the same objections as the first.

Chunk `deaths` v1 (`DeathsChunk`, strict): u32 count (at most 4096); per entry, in strictly ascending placement order and below the
created range: u32 placement, u64 hours (bits of a double), u8 flags (1 position, 2 raised), with the position flag 3 x u64
(x, y, z) and the u32 bits of the heading. 13 bytes an entry, 41 with a position. Created actors (summons, raised corpses) are
never noted: they are not saved either (task 0250).

## 2. What the pack does (`deaths.*`, `combat_deaths.cpp`)

- `DeathLedger`: the entries (placement to `DeathEntry`), capped, ordered.
- `DeathKeeper`, three calls the cell source makes:
  - `noteDeaths` after the combat update (the core's own writer): every dead fighter of the combat gets an entry with the game
    hour (`day * 24 + hour` from `ActorRequestWriter::clock()`, stored by the actor update) and its position
    (`subjectState`); the position is read again every 0.5 s for 20 s, because a ragdoll moves the body while it settles.
  - `placeCorpses` in the actor update: an actor with an entry that has appeared (its handle is known and its combat numbers are
    read) either respawns (the entry is erased, the actor the pack built stays alive: `deathRespawns`) or is put where it lay
    (`placeCorpse`) and the package brain is told to hold it still (`onConversation(ref, true)`).
  - `restoreDeaths` at the top of the combat update, with the core's OWN writer: `SkyrimCombat::restoreDead` marks the fighter dead
    (health 0), plays its death clip at the fastest rate the core accepts (10 times) so that it lies in the clip's last pose
    a moment after the load, and reports the death to the core (`reportDeath`: the dead list, so that the loot of task 0184 opens).
    The report does NOT go through the wrappers of the script, crime and HUD hooks: no OnDeath, no story KILL, no murder
    is raised a second time.
- `actorGone` (a cell unloaded): the entry waits to be put back when the actor comes back, so the same code also keeps a corpse
  across a cell reload inside one session.
- **The raised corpse of task 0250.** `SkyrimCombat::setCorpseRaisedSink` (one call in `reanimateInSights`) marks the entry
  `mRaised`; the raised actor itself is not saved. The placement is DISABLED in the engine's `NWLD` (task 0250), so after a load
  the keeper enables it again (`WorldSourceService::setObjectEnabled(true)`, once the chunk was read): the original corpse comes
  back, visible, and is put back dead like any other.
- **Respawn.** Skyrim resets a cell after `iHoursToRespawnCell` game hours (240 = 10 days; RECALLED-VERIFY: the GMST value is read
  from the data, `SkyrimWorldView::hoursToRespawnCell`, its use as the cell reset is recalled). An actor respawns when it is
  neither Unique nor Essential (the NPC's own ACBS flags, `FighterProfile::mUnique/mEssential`, `FighterView`) and that many hours
  have passed since it died. The test is made when the actor appears after a load: a cell that stays loaded never resets
  (Skyrim does not reset the cell the player is in). The ACBS "Respawn" flag (0x08) is NOT read: the spec said "non-unique".
  **Not built:** the respawned actor keeps whatever its inventory changes were saved (the loot taken stays taken).

## 3. Engine side (edits outside the new files)

`ActorRequestWriter::placeCorpse` (SDK), `NeutralRequestSink::placeCorpse` and `NeutralActorBridge::Writer::placeCorpse`,
`PhysicsSystem::NeutralRequestApplier::placeCorpse` (sets the Ptr's position and heading like `placeSeated`, clears the movement
request). The capsule stays a WORLD body, as the dead of the no-ragdoll path always did, so that loot can still be
activated.

## 4. Found on the way: a corpse walked on by its package

The package brain is not told about deaths. In the first run the body that died at (336, -416) stood at (380, -344) a minute later.
`noteDeaths` now holds every newly dead actor (`onConversation(ref, true)`); the position refresh goes on every 5 s after its first 20 s.

## 5. Edits outside the new files (list)

- `sdk/include/openeden/game/actor_rules.hpp`: `ActorRequestWriter::placeCorpse` (trailing virtual, refusing default; the version number is
  NOT bumped, the pins stay).
- `apps/openmw/mwphysics/neutralactorbridge.{hpp,cpp}`, `physicssystem.{hpp,cpp}`: the sink function, the bridge writer function, the applier.
- `game_packs/skyrimse`: `combat.hpp` (`FighterProfile::mUnique/mEssential`, `FighterView::mUnique/mEssential`, `restoreDead`, `readView`,
  `setCorpseRaisedSink`, the sink member), `combat.cpp` (two lines in `fighterView`), `combat_summons.cpp` (one hook call in
  `reanimateInSights`), `world_view.{hpp,cpp}` (the two flags, `hoursToRespawnCell`), `neutral_world.hpp` (five hook lines: `actorGone`,
  the clock and `placeCorpses`, `restoreDeaths`, `noteDeaths`, `setDeaths`), `skyrimse_game_pack.cpp` (the keeper, the chunk and their
  hooks), both source lists of `CMakeLists.txt`, `CLAUDE.md`.
- `game_packs/skyrimse/include/game_skyrimse/perks.hpp`: **the base commit `5986a356f8` did not compile** (`MagicEffectRecord::mAssociatedItem`
  was declared twice, by the merged tasks 0248 and 0250: the same field, DATA +8); the second declaration is removed. `perks.cpp` still
  assigns it twice with the same value (left alone). Whoever merges this branch should know the break exists on `main`.
- `apps/components_tests/CMakeLists.txt`, `apps/openmw_tests/CMakeLists.txt`: the two new test files and the two pack sources.
- `tools/skydeaths_check/` (new), `docs/tasks/0254-...` (new).
- No existing test was edited or deleted. No contract version moved; no virtual was added to `CombatRequestWriter`.

## 6. Stage log (Windows only; `I:\Projects\OpenEden-work\ragdoll5\deaths`, frames in `keep\`)

**Tests first.** `testskyrimsedeaths.cpp` (38 tests: the rules, the ledger, the chunk, the keeper with a fake `CombatData` and recording
writers) was written before the code and run against stubs of `deaths.cpp` and `combat_deaths.cpp` (the stubs refuse and do nothing):
**26 of 37 failed, the 11 that passed test refusals and absence** (`red_run.txt`). With the code, 29 passed and 8 failed, all for one real
fault the tests found: an idle fighter is never read by the combat, so its numbers (`FighterView`) were never there (`readView` added; a
test of the late refresh rate and one of the hold were added after). Then 38 of 38 pass (`green_run.txt`). `testneutralplacecorpse.cpp`
(5 tests, `openmw-tests`) was written AFTER the bridge code and not run red (the SDK default refuses, so a bridge without the override
would fail the first test; not demonstrated); one of its tests was wrong (a bridge update with no live actor never calls the pack) and
was fixed. Wider runs: `components-tests` `*Skyrim*:*Summon*:*Death*:*Combat*:*Extension*:*Frozen*:*Neutral*` 3,231 pass;
`openmw-tests` `*Neutral*:*Extension*:*Combat*:*Physics*` 849 pass.

**In game** (`tools/skydeaths_check/check.sh`: the Bannered Mare; `summons test kill = Hulda` kills her 3 combat seconds after the data
is read; a Lua script logs her; a menu script saves and quits; a NEW process loads the save, logs her and activates her):

| run | setting | result |
| --- | --- | --- |
| `r2`, `d1` to `d3` | ragdolls off | save: `DEATHS noted actor 108142 hours 24.03`; load: `DEATHS placed actor 108142 at 336 -416 69`, `COMBAT restored dead actor`; her position (336.0, -416.0, 69.0) is the one before the save; `container opened 0001a66e` (the loot window lists her 8 stacks, frame). **The runs `d1`, `d2`, `d3` give the same 8 lines (md5 `e9f243d16b78fdacc69ddecaf7ea7727`).** The pose after the load equals the one before the save (`keep\r2_save_pose.png`, `r2_load_pose.png`, `r2_load_loot.png`). |
| `r4` | ragdolls on | she lies on the floor before the save (`keep\r4_ragdolls_save.png`) and lies on the floor after the load (the ragdoll falls again: `Ragdoll started` in the load run, `keep\r4_ragdolls_load.png`); loot opens |
| `r11` | Raise Zombie | the player casts it on the corpse (`COMBAT reanimate ... raises actor 108142 as placement 4286578689`), the corpse is hidden (`enabled false`), the zombie's time ends and it falls dead; saved; after the load `DEATHS enabled the hidden corpse 108142`, the corpse is back (`enabled true`), dead, at its place (`keep\r11_raise_load_corpse_back.png`), loot opens; the zombie (a created actor) is not there |

The first build (`r1`) showed the body at (380, -344) after a minute: the walking corpse of section 4. A first version skipped the ragdoll
for a restored corpse; with ragdolls on that left it standing in the clip's pose after it had lain flat before the save
(`keep\r3_ragdoll_lying_before_fix.png`, `r3_restored_standing_before_fix.png`), so the skip was removed.

**Morrowind unchanged.** `balsum.sh` (Balmora, `--random-seed 7`, frame 600): SHA-256 prefix `1c5d47ad0b5dc32d`, the value of every earlier
build; `tools/worldobjects_check` against the `combat5` build (not the exact parent): 110 lines, OK; `mwsaveload.sh` in both directions with
that build: `MW-ROUNDTRIP-EQUAL` twice (the script's digest counters differ by 1 and 55, as in earlier tasks' runs).

## 7. Decisions for toddhow

1. **With ragdolls on a restored corpse falls again at every load** (about 7 s of settling) instead of lying down at once; skipping the
   ragdoll left it standing in the clip's pose. A third way would save the ragdoll's final pose: not built.
2. The death clip is played at 10 times its speed (the fastest the core accepts) so the body lies in the clip's last pose almost at once;
   without ragdolls that pose looks upright and leaning (the no-rig look the game already had).
3. Respawn: not Unique and not Essential, after `iHoursToRespawnCell` (240 hours). Skyrim's own flag "Respawn" (ACBS 0x08) is not read; a
   respawned actor keeps its changed inventory and stands at its home position, not at the corpse's.
4. A restored corpse keeps the capsule on the WORLD layer (a standing capsule blocks the player where a body lies), as the no-ragdoll
   death always did; making it DEBRIS would lose the loot activation.
5. A Papyrus `Disable()` of a corpse is not undone; only the Reanimate spell's hiding is.
6. The ledger holds 4,096 dead actors at most (the 4,097th is not remembered).
7. `ActorRequestWriter::placeCorpse` is an SDK addition with no version bump (a bump would break pinned tests).
8. `main` at `5986a356f8` does not compile without the `perks.hpp` fix of section 5.

**Not run:** the Linux gate and GCC `-Werror`, clang-format 14 (formatted by hand), clang-tidy, the Lua tests, a fresh review, a respawn in
game (unit tested only: it needs 240 game hours), an exterior corpse, a creature's death, a follower's death, saving while the death clip
still plays, a cell that unloads and loads again inside one session (unit tested only).
