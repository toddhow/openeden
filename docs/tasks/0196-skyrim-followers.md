# Task: Skyrim followers (milestone M-S7, first slice)

Status: toddhow chose "Followers" on 2026-11-15. Full loop in form (the same hand wrote tests and code; see the stage log
for what was and was not run red first).
Design doc section: `docs/design/skyrim-se-implementation-plan.md` (M-S3 actors, M-S7); decisions 0013 (game packs write
save chunks), 0016 (neutral world), 0017 (neutral actors).
Goal: in a Skyrim session an NPC can be told to follow the player; it keeps near him, goes with him through teleport doors
and between cells, can be dismissed, and is still a follower after a save and load. Fighting at the player's side is a
hook only (task 0190 builds the combat AI).
Inherited code touched: none outside the neutral-world code. New SDK header `follower_rules.hpp` (additive), one new
virtual `ActorRequestWriter::playerPosition` (SDK `actor_rules.hpp` 0.5 to 0.6, additive with a refusing default).
Constraints: everything behind `[Game] neutral world` + `[Game] neutral actors` + `[extension.skyrimse] actor packages`;
no new setting is on by default (two test settings, empty/0). Morrowind unchanged.
Out of scope: the combat AI (task 0190), follower commands (wait, trade, "favours"), animals as followers, dialogue
lines of followers beyond the game's own recruit lines, follower AI packages from the alias (the alias's package list),
the follower's own inventory.

## 1. Survey (read at `5b886a0bf8`)

- **The brain already follows an ACTOR, not the player.** `PackageBrain::beginFollow` / `runFollow`
  (`game_packs/skyrimse/src/package_brain.cpp`) read the PACK's PTDA target (`PackageSource::targetOf`) and accept only a
  specific reference that is another actor of the brain (`mActors.count`). The player has no handle, no brain row, and
  `ActorRequestWriter::actorPosition` (SDK `actor_rules.hpp` 0.5) answers only for actor handles. So the existing follow
  package cannot follow the player: a target position of the player is the first missing piece.
- **The core had no player position for the pack.** `PhysicsSystem::setNeutralFocus(player position)` is called every frame
  from `World::doPhysics` (`worldimp.cpp`, before `updateNeutralActors`) and kept in `mNeutralFocus`; the combat hooks have
  their own `NeutralPlayerState` but only under `[Game] neutral combat`. `NeutralActorBridge::Writer` (the writer the pack
  gets in `update()`) had `actorPosition` and `clock` only.
- **A door teleport moved only the player.** `MWClass::Neutral::activate` (`apps/openmw/mwclass/neutral.cpp`, the
  `NeutralDoorOutcome::Teleport` case) returned `MWWorld::ActionTeleport(cellId, arrival, false)`: the third argument is
  Morrowind's "teleport followers", false ("No followers: a neutral session has none", task 0115 open question 9).
  Morrowind's own follower carry is `ActionTeleport::getFollowers` over `MechanicsManager::getActorsFollowing` (Morrowind AI
  packages): useless here.
- **Moving a reference between cells keeps the reference.** `World::moveObject(ptr, newCell, pos, movePhysics, keepActive)`
  (`worldimp.cpp`): `CellStore::moveTo` does not copy the `LiveCellRef`; the new cell lists it as "moved here". Three cases:
  both cells active (rendering, physics and mechanics `updatePtr`), current active and new inactive
  (`removeObjectFromScene`, then `moveTo`) and the reverse (`moveTo`, then `addObjectToScene`). For a neutral actor the
  physics entry is keyed by the `LiveCellRefBase*` (`PhysicsSystem::NeutralActorEntry::mRef`), which does not change; when the
  actor is removed from the scene the pack hears `onActorRemoved`, and when its new cell loads it hears `onActorAdded` with a
  NEW handle for the SAME placement id (`WorldObject::id` = `RefNum::mIndex`, `neutralPlacementOf`).
- **The pack's actor state dies with the handle.** `PackageBrain::onActorRemoved` erases the actor row. So "who follows" must
  be kept by placement id, outside the actor row, or a carried follower stops following the moment the door moves it.
- **The game's own recruit path.** Quest `DialogueFollower` (form 0x000750BA; read with houseCARL on the installed data):
  aliases `Follower` (id 0), `Animal` (1), `FollowerExtra1..10` (2..11), `bladeLinked` (14). The follow-me lines are INFOs
  of that quest with a fragment script (`TIF__...`); they call DialogueFollowerScript, whose functions end in
  `ReferenceAlias.ForceRefTo(actor)` on alias 0 (and `Clear()` to dismiss). RECALLED (not read this session): the script
  also adds the actor to `CurrentFollowerFaction` / `PlayerFollowerFaction` and sets relationship and teammate state.
  The Papyrus host (`papyrus_host.cpp`, `reg("ReferenceAlias", "ForceRefTo" ...)`) already records a forced fill in
  `mAliasForced` (and saves it, `saveState()`), but nothing read it from outside, and the faction/teammate natives are
  tolerant no-ops. Reading the alias is therefore the one hook that needs no new native.
- **Which actors can be recruited by dialogue** depends on the INFO conditions of the follower topics (faction
  `PotentialFollowerFaction`, level, quest state; run by `DialogueRuntime`), none of which this task changes.
- **Saves** (task 0177): neutral cells are rebuilt from the pack at every load; neutral actors are not saved ("they walk by
  the pack's schedule and are placed again at every load"); a game pack's own state travels as a chunk through
  `SaveChunkService::registerHandler` (`GPCH`, as `inventory`, `equipment`, `loadorder`). So followers need a pack chunk.
- **Combat side.** Task 0190 builds the combat AI in new `combat_ai*` files (not in this tree). Nothing there reads "whose
  side is an actor on". This task only offers the answer: `FollowerRules::sideOf`.

## 2. Design

**Who follows lives in the pack, by placement id.** `PackageBrain` keeps `std::set<WorldId> mFollowRefs`
(`setFollowPlayer(reference, bool)`, `followsPlayer`, `followerRefs`). `Actor::mFollowPlayer` is set when an actor with a
followed placement id is added (so a carried follower is still one after the core removed and added it), and cleared when
it is dismissed.

**Following the player.** New `ActorRequestWriter::playerPosition(x, y, z) const` (SDK 0.6, default refuses). A follower
(`runFollowPlayer`, every `kFollowerRepathSeconds` = 0.75 s of frame time): reads the player's and its own position, does
nothing when within `kFollowDistance` (260, the stage S6f value), else walks (`goTo`) to a point `kFollowStandOff` (140)
from the player on the line towards itself, at the run speed beyond `kFollowerRunDistance` (700), walk otherwise. A goal
already out that still leads within `kFollowerGoalSlack` (320) of the right place is left alone. A follower does not
evaluate its AI packages; a get-up clip (seated or sleeping when recruited) still plays out. Dismissing cancels the goal and
makes the actor evaluate its packages again from where it stands (its home becomes that spot). All of this is in new
functions (`enterFollowPlayer`, `leaveFollowPlayer`, `runFollowPlayer`, `setFollowPlayer`); `update()` has one branch for
them and `onActorAdded` one line. **Coordination:** a second agent may add a function to the package brain for scenes; the
two sets of changes are disjoint (mine are listed above).

**Recruiting.** (1) The game's own path: `PapyrusHost::forcedAlias(quest, alias)` (new read accessor) and the pack's
per-frame `syncFollowerAlias` (`skyrimse_game_pack.cpp`) turn a change of the `DialogueFollower` alias 0 fill into
`recruit(new)` and `dismiss(old)` (`followerAliasChange`, pure). It needs `[extension.skyrimse] papyrus = true` and
`actor packages`. (2) A test setting `[extension.skyrimse] follower test recruit = <editor id>[,...]` recruits an actor the
moment it appears, and `follower test dismiss after = <seconds>` dismisses the test followers once. Both are off by default.

**Carrying through a door.** `FollowerRules` (new SDK interface, found by `dynamic_cast` on the registered source, like
`ActorRules`): `followers(out, capacity)`, `isFollower`, `sideOf`, `onFollowerCarried`. The engine's
`TeleportWithFollowersAction` (`mwclass/neutral.cpp`, used for a neutral teleport door when `[Game] neutral actors` is on)
collects the live follower actors of the active cells within `kFollowerCarryRange` (1500) of the player and not dead
(`collectNeutralFollowers`, ascending placement id) and moves them with `World::moveObject` to the destination cell around
the arrival point (`planFollowerArrivals`: 130 units in front, spread 110 sideways, the arrival's z and facing), then
teleports the player (`ActionTeleport`, as before): the order Morrowind uses. Followers farther away stay and walk after the
player when the cell they are in is active again; they are not warped. (The destination cell is built before the move by the
door code, so `moveObject` has a `CellStore`.)

**Dismissal** is the pack's: the alias is cleared (the game's dismiss lines) or the test timer runs out; the actor stays
where it is (in whatever cell it was carried to) and takes up its packages.

**Saving.** The pack's chunk `followers` v1 (`FollowerChunk`: count, ascending non-zero u32 placement ids, caps and strict
refusal) restores the set into the brain. The alias fill is restored by the Papyrus chunk too (same ids, same set).
**Known gap, see decisions:** the neutral save rebuilds actors from the pack, so a follower that had been carried out of its
home cell is rebuilt in its HOME cell after a load, not beside the player; it follows as soon as that cell is active and it
can reach the player, but it is not placed in the player's cell. A real fix needs the follower's base and a description the
engine can spawn from (task decision 3).

**The combat hook.** `FollowerRules::sideOf(reference)` returns `FollowerSide::Player` for a follower. The combat AI (task
0190) can read it through the same `dynamic_cast` on the source. Not used here beyond a test of the default.

## 3. Stages

1. SDK additions, brain follower mode, actor table, chunk, alias sync (pack tests).
2. Engine: player position in the bridge, `neutralfollowers.*`, the teleport action (engine tests).
3. In game: recruit by the test rule in the Bannered Mare, walk, front door to Whiterun, dismiss; three runs equal; save
   and load; Morrowind unchanged.

## 4. Acceptance

- [ ] A follower walks to within 260 units of the player and stays; a far one runs. (pack tests, in game)
- [ ] A follower goes through a teleport door with the player and is still a follower. (engine tests for the pure parts, in
  game)
- [ ] Dismissing ends it and the actor takes up its packages. (pack tests, in game)
- [ ] The follower set round-trips through the pack chunk; damaged chunks are refused. (pack tests, in game)
- [ ] With every new setting at its default: Morrowind and a Skyrim session without followers are unchanged
  (`tools/worldobjects_check`, the fixed Balmora frame hash against the parent).

## 5. Stage log

**Order of work, honestly.** The code was written first and the tests after it (the full loop asks for the reverse): the pack and
engine tests passed at the first run. To show they can fail, four one-line mutations were applied to the new code (the speed
comparison, the follower flag on add, the strictly ascending check of the chunk, the test rule matching any editor id),
`components-tests` rebuilt and run: 4 of the 26 tests then failed (`theSpeedIsRunBeyond...`, `aPlacementRecruitedBeforeItAppears...`,
`damagedChunksAreRefusedAndChangeNothing`, `theTestRuleRecruitsByEditorId...`); the flag-on-add mutation was NOT caught by the test written for it (the follower surviving a remove and
add) was NOT caught, so that test got an assertion on the goal's place (it now fails for it by reading, not run). Existing tests
edited: none.

**In game** (RTX 4070, `OPENEDEN_FIXED_FRAME_TIME`, five Skyrim masters, `[Game] neutral actors/idles/locomotion` and
`actor packages` on, `follower test recruit = Hulda`; the numbers are the `N7F` lines of `runs/f1` to `f3`):

| time (sim s) | what | distance player to Hulda |
| --- | --- | --- |
| 0 to 9 | the Mare, she stands at the bar | 603, then 461 after the player stands at (-60, -180) |
| 12 to 14 | the player walks to (100, -500) | 346, 265, 251 (she follows and stops inside 260) |
| 20 | the player at the front door | 746 |
| 21 | the door is used: `Neutral follower 108142 carried`, new hook handle 108142/9 | 130 in Whiterun |
| 29 to 36 | the player walks 300 units south | 218, then 206 (she follows) |
| 45 | the test dismisses her | |
| 58 | the player is sent 1,266 units away | 1,266 for the next 60 s (she does not follow) |

Three runs: the 140 lines are byte-identical (md5 `25b3c711...`); screenshots are not byte-identical in any pair (HUD and idle
animation; not a decision of this task). Frames viewed: Hulda behind the bar in the Mare, Hulda beside the player on Whiterun's
street at night, and after a load. **Save and load:** the player outside with her beside him, saved by the menu script; a new process
loads it: `Neutral follower 108142 carried`, she stands 130 units in front of the player (frame viewed, `runs/s1/user/screenshots`);
the load run has no error line.

**Morrowind unchanged:** `tools/worldobjects_check` (110 lines, OK) and the fixed Balmora frame `1c5d47ad0b5dc32d` for the `combat5`
build and this build (twice); nothing new runs without a neutral player cell.

**Not run:** the Linux gate and GCC `-Werror`, clang-format 14 (formatted by hand to 120 columns), clang-tidy, the Lua tests,
sanitizers, a fresh review, the real dialogue recruit (the alias path: `followerAliasChange` is tested, the host accessor
`forcedAlias` is not, and no follow-me line was spoken in game), a follower whose home is an exterior (the load restore asks
`WorldModel::findNeutralCell`, which needs an interior), more than one follower, animals, a follower farther than 1500 units when
the player uses a door.

**Decisions for toddhow**
1. **The real recruit path is wired to the alias only.** DialogueFollowerScript's other calls (faction, relationship, teammate) are
   tolerant no-ops. If you want the follower to also be in `CurrentFollowerFaction` (other lines check it), that is a native for the
   Papyrus host (a task 0176 area).
2. **Followers farther than 1500 units are not carried through a door** (they stay and follow when their cell is active; Skyrim
   warps a far follower to the door when the player enters). Say if you want them carried from any distance.
3. **The saved follower is brought back from its HOME cell**, which the pack keeps in the chunk (from the placed-reference
   index, so only a follower whose home is an interior the pack can build). The alternative, saving the follower's base and
   spawning it, needs the engine to register a base that was never built (a `Store<NeutralBase>` entry) and was left alone.
4. **Dismissed actors stay where they are** and take up their packages around that spot (their home becomes it), as the game does
   until the next cell reset; they are not sent back to their original place.
5. **Distances** (260 stay, 140 stand-off, 700 run, 1500 carry range, 130 in front on arrival) are estimates.
6. **The brain change is in new functions** (listed above) so that the second agent working on the brain for scenes and the combat
   agent do not collide; `package_brain.hpp`/`.cpp` still have one shared edit point each (`update()` and `onActorAdded`).
7. **The combat hook** is `FollowerRules::sideOf`; whether an enemy of the player is an enemy of a follower, and how a follower
   chooses a target, is the combat AI's.
