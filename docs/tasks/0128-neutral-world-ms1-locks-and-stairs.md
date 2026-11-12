# Task: M-S1 close-out, locked doors and containers, and stairs in a neutral Skyrim interior

Status: **APPROVED** under toddhow's blanket "go with your recommendations" of 2026-11-11 (decision D13: full loop for engine work). Branch `ms1locks`.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` (milestone M-S1, "walk a lit interior and leave through a door"); `docs/design/neutral-world-model.md` 10; tasks 0110 (collision from visual triangles) and 0115 (doors).
Goal: (1) a locked door or container in a neutral session opens when the player holds its key and the opening stays for the session; (2) the player can walk up and down real stairs in a Skyrim interior.
Inherited code touched: `apps/openmw/mwclass/neutral.cpp` (`Neutral::activate`, which task 0115 added for doors only); `apps/openmw/mwworld/neutraldoor.*` is NOT changed (its tests stay unedited). Stairs: to be found; Morrowind's actor step settings stay as they are.
Inputs / outputs: `LockComponent` (`sdk/include/openeden/game/world_model.hpp`: `locked`, `level`, `key`, a KEYM base id) read from the cell snapshot; no SDK change. New `apps/openmw/mwworld/neutrallock.{hpp,cpp}` (pure). One new l10n string (`NeutralLockUnlocked`). New developer aid `OPENEDEN_NEUTRAL_KEYS` (not a setting).
Constraints: main thread only; no allocation per frame; the lock memory is not saved (saving stays refused, N6); no Morrowind behaviour change.

## Findings that shape the design (read in the tree at `7287dbed33`)

- Items in a neutral cell are inert statics until stage N5 (`Neutral::insertObjectRendering`, task 0110), and `ContainerStore` holds only the typed Morrowind and ESM4 item stores, so **the stub player's inventory cannot hold a Skyrim key** (task 0115 section "Lock", option (c) "impossible until items are live things"). A key check "through the inventory" therefore has no inventory to look in.
- The engine's lock picking (`game_packs/morrowind/src/security.cpp`, `class_door.cpp`) reads Morrowind GMSTs (`fPickLockMult`, `iLockpickMax`...), the Security skill, and a `Lockpick`/`Probe` item `Ptr` from the inventory. None of those exist for a Skyrim lock (lock levels are Skyrim's own numbers, `LockComponent::level`).

## Decisions (recommendations taken)

1. **Lock picking is not applied; recorded as a gap.** A lock with no key held stays locked and shows the existing "The door is locked." message. The lock level is kept in the snapshot for a later Skyrim lock picking rule in the game pack.
2. **Key = a session key ring, until N5.** `MWWorld::NeutralLockState` (main thread) holds keys as (game id, KEYM base id) and the locks opened as (game id, placement id). Until items can be carried, the ring is filled only by the developer aid `OPENEDEN_NEUTRAL_KEYS=skyrimse:<hexid>,...`; it is the stand-in for "the player's inventory as the neutral model allows". When N5 lets a neutral item be carried, its pickup calls `giveKey` and nothing else here changes. The ring is cleared on a new game.
3. **Opening persists for the session**: the first activation with the key records the placement as unlocked; later activations need no key. Not saved (N6).
4. **Containers**: a locked container refuses (same message) or unlocks the same way. An unlocked container does what it did before (nothing: there is no neutral inventory UI before N5).
5. `OPENEDEN_NEUTRAL_UNLOCK_DOORS` keeps opening everything.

## Acceptance criteria

- [ ] `decideNeutralLock`: no lock / unset lock = NotLocked; locked + no key = Locked; the matching key = UnlockedByKey; another key = Locked; a lock naming no key is never opened by a key (and the key check is not asked); an earlier unlock = Open without asking for the key; the developer aid = Open (tests `testneutrallock.cpp`, written first, red against stubs).
- [ ] `NeutralLockState`: keys and unlocks per game and id, `clear`, `giveKeysFromText` (hexadecimal, malformed entries skipped, the null id refused).
- [ ] In a game (`Dawnstar`/`WhiterunBanneredMare`-class interior with a locked door, found from the data): without the key the door is refused with the message; with `OPENEDEN_NEUTRAL_KEYS` it opens, the player arrives at the destination, and the way back (the other side's door) and a second activation work without the key. A locked container likewise.
- [ ] Stairs: the player walks up and down real stairs of a Skyrim interior with the visual-triangle collision (frames and positions); if not, the root cause is found and fixed in the neutral path only.
- [ ] With the setting `[Game] neutral world` off, or in a Morrowind session, nothing changes: `tools/worldobjects_check` dump equal to the parent build's, and the fixed-frame Balmora frame has the same SHA-256.
Out of scope: Skyrim lock picking, key items in an inventory (N5), lock levels, ownership and crime, traps, door sounds and animations, saving.

## Stage log

(see the end of this file, filled in as the work goes)

### Stage log (2026-11-12; same hand wrote tests and code, no fresh reviewer; Windows only; branch `ms1locks`, not pushed)

Built in `I:\Projects\OpenEden-work\ms1\build` (RelWithDebInfo, OpenCS, launcher and wizard off). A baseline build of `main` (`7287dbed33`) was copied to `ms1\base` before any edit.

- **Tests first, red against stubs:** `testneutrallock.cpp` against a stub `neutrallock.cpp` (everything returns "not locked", "no key"): 12 of 18 failed, the other 6 test absence (`NotLocked` cases, malformed text, the null id). A 19th test (`resetNeutralLockState`) was added with the glue and was red only as a compile error. All green after (`NeutralLock*` and `NeutralDoor*`: 36 pass, 1 skipped real-data test of the existing suite); the whole `openmw-tests` 2,051 pass, 2 skipped (both real-data, existing).
- **Mutations** (`ms1\mut.sh`, five single-line changes of `neutrallock.cpp`, each built and run): ignoring `alreadyUnlocked`, letting the null key through, `clear` keeping the unlocks, ignoring the developer aid, reading key ids as decimal: each failed a named test (`an_earlier_unlock_stays_open...`, `a_lock_that_names_no_key...`, `clear_forgets...` and `a_new_game_forgets...`, `the_developer_aid_opens_everything`, `keys_from_text_reads_hexadecimal_ids`).
- **Locked door with a key, in game** (`tools/neutral_session_check/check.sh ... Angarvunde01 9000 keylocked`, `OPENEDEN_NEUTRAL_KEYS=skyrimse:8715F`, a quiet window): the door (placement 185905, XLOC key 553311 = 0x8715F, level 255) without the key is refused (`NLOCK ... -> 3`, frame of the message earlier in task 0115); with the key `NLOCK -> 2`, the message "You unlock it with the key." shows (frame viewed: `run_key\user\screenshots\screenshot001.png`), the player arrives in `Angarvunde02` at 2720,-7488,-992; the way back (the other side's door, same base) returns to `Angarvunde01`; activating the first door again gives `NLOCK -> 1` (open earlier) and teleports again: the unlock persisted. 0 crash dumps, 0 unexpected errors.
- **Locked container:** no master container has a key reachable in the first cells tried; the Bannered Mare strongbox (placement 960311, key 0, level 25: a lock with no key) is refused (`NLOCK -> 3`, frame viewed: "The door is locked." before the container text was added, then `NeutralContainerLocked` = "It is locked."). The container-with-key path shares all code with the door (the same `decideNeutralLock`) but was **not run in game**.
- **Stairs: they already work, nothing needed fixing.** `tools/neutral_session_check` "stairs" (new, `luatest/scripts/n3a4b/stairs.lua`): the Bannered Mare's `WRIntFloorDirtStairs01` (open-riser wooden stairs, frame viewed). A height map from rays shows the stair running along +x between y -216 and -288; the player running up it from (-1340,-252) gains z from 33.4 to 268.4 over 240 frames (about 1 unit of z per unit of x, steady, 216 units horizontally, never stopped), and running down from the top (z 309.7) reaches 63.3 in 240 frames. The step settings are Morrowind's and unchanged. One staircase, both directions; the second stair model, Dragonsreach and Jorrvaskr were not walked.
- **Morrowind unchanged:** `tools/worldobjects_check` on the new build against the baseline (`ms1\base`): 110 object lines identical ("OK"); the fixed-frame Balmora screenshot (`OPENEDEN_FIXED_FRAME_TIME`, seed 7, frame 600) has SHA-256 prefix `1c5d47ad0b5dc32d` for the baseline and for the new build, `[Game] neutral world` off and on, as in tasks 0115 and 0114.

### Decisions for toddhow

1. A lock with no key (Skyrim lock levels, `key 0`: for example the strongbox) cannot be opened at all before Skyrim lock picking exists in the game pack; the lock level is carried in the snapshot for that. The Morrowind security rules were not reused (decision 1).
2. The key "inventory" is a session ring filled by `OPENEDEN_NEUTRAL_KEYS` until N5 makes items carriable; the N5 pickup of a key item should call `neutralLockState().giveKey`.
3. XLOC present = locked (RECALLED-VERIFY, task 0099); some locks have level 0 and key 0 (the Angarvunde draugr chests) and are now refused, which may be wrong if level 0 means "unlocked".
4. An opened lock is not saved (saving is refused until N6).

### Not run

The Linux gate and GCC `-Werror` (Docker is not used here), clang-format 14 (not installed here; lines wrapped by hand to the file's style), clang-tidy, the Lua integration tests, `components-tests`, sanitizers, a fresh reviewer, a locked container that has a key in game, other staircases, a real keypress (activation by `object:activateBy`).
- 2026-11-12 (toddhow: yes): a lock of level 0 that names no key is not locked (the Angarvunde draugr chests); a level 0 lock with a key still needs it. Test red first, then 20 of 20 NeutralLock tests.
