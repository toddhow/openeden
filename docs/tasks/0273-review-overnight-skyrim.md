# Task 0273: fresh review of the overnight Skyrim merges (0c6ff3fd19..34beb9d459)

Reviewer: a fresh reader who did not write the code. Read-only review of the diff `git diff 0c6ff3fd19..34beb9d459`
(163 commits, about 1,260 files; the formatting commit `2c59b27f69` was not read). Nothing was built or run: the
worktree was not configured (the task allowed it; with this much code the time went into reading). Every finding
below was confirmed by reading the code at `34beb9d459`, and says so. Nothing in shared code or an existing test was
changed.

**Coverage, said plainly.** About 225,000 lines were added; this review read the engine seams in full (`worldimp.cpp`,
`extensionhost.cpp`, `neutralsave.*`, `neutralcorpseposes.*`, the SDK header diffs, the forced greeting path, the
Papyrus host's state restore, the writer wrappers, the worker threads of the services) and sampled the pack. It did NOT
read `combat*.cpp`, `crafting*.cpp`, `crime*.cpp`, `progression*.cpp`, `music.cpp`, `perks.cpp`, the HUD or the Lua
windows in depth. "No finding" for those areas means "not read", not "clean".

What held up well: `neutralsave` encode and decode (canonical, capped, strict, version 1 to 3 all read, version 3
only when poses exist); the pack chunk decoders read (`werewolf`, `deaths`, `houses`: strict, capped, ordered);
`vmad` hostile-input caps; the services' worker threads (state flag with release and acquire, joined in the
destructor); all four `CombatRequestWriter` wrappers forward every virtual including `cancelProjectile` and
`playSound`; no wall clock or unseeded random in simulation code (the `steady_clock` uses are timing only).

## Findings, most severe first

### 1. A load or new game leaves scene speech, speakers, mouths and projectile handles pointing at freed objects

- File: `apps/openmw/mwworld/worldimp.cpp:604` (`World::clear`) against `:1772` (`mNeutralSpeakers[...] = line.mPtr`),
  `:1760` to `:1810` (the sink), and `apps/openmw/mwworld/worldimp.hpp:102` to `:104`.
- Defect: `World::clear()` resets `mCorpsePoses`, `mCorpseRest` and `mNeutralMusic` but not `mNeutralSceneSpeech`,
  `mNeutralSpeakers`, `mNeutralMouths` or `mNeutralProjectiles`; `NeutralSceneSpeech::remove` and
  `NeutralProjectiles::clear` have no caller at all. `mNeutralSpeakers` is a `std::map<const void*, Ptr>` that is only
  ever inserted into, so each `Ptr` keeps a raw `LiveCellRefBase*` for the life of the process.
- Failure scenario: a scene or bark queues lines (up to 32 per actor, a scene such as the Helgen cart ride queues many).
  The player quickloads, loads another save, or walks away so that the speaker's cell unloads, while lines are still
  queued. The next `doPhysics` runs `mNeutralSceneSpeech->update(sink)`, which calls `sink.active(actor)` ->
  `sound->sayActive(mSpeakers[actor])` and `sink.subtitle` -> `ptr.getClass().getName(ptr)` on a `Ptr` whose
  `LiveCellRef` was freed with its cell. That is a use after free in the main loop. The same stale map also feeds
  `driveNeutralMouth(it->second, speaker->second)` while a mouth driver is open. Separately, projectile handles
  (`mHandles`) from before a load are never marked `remove`.
- Confirmed: CONFIRMED by reading (no call to `remove`, `clear` or reset of those members anywhere in `apps/openmw`;
  `grep` shows only the inserts and the update).
- Fix: in `World::clear()` also `mNeutralSceneSpeech.reset(); mNeutralSpeakers.clear(); mNeutralMouths.clear();
  if (mNeutralProjectiles) mNeutralProjectiles->clear();`. Better: store the key only and look the `Ptr` up when
  needed, or drop a speaker when its cell unloads (`NeutralSceneSpeech::remove`). Add a test that queues a line, calls
  the reset and checks nothing is played.

### 2. SDK ABI: two virtuals were inserted BEFORE the last one of `ActorRequestWriter`

- File: `sdk/include/openeden/game/actor_rules.hpp:208` (`playerPosition`, task 0196) and `:221` (`placeCorpse`,
  task 0254), above `clock()` at `:232`.
- Defect: at the base commit `clock()` was the last virtual (`:207` there). The root guide and the header's own
  policy say a new virtual goes at the END. Both new virtuals sit in the middle, so the vtable slot of `clock()`
  moved down by two. Task 0254's comment even says "additive, version unchanged".
- Failure scenario: nothing breaks while the core and every pack are built together, which is why no test sees it.
  A pack library built against the earlier header (a third-party game pack, or a stale `skyrimse` DLL beside a newer
  `openmw`; the pack's CLAUDE.md already notes that "building `openmw` leaves a stale `skyrimse.dll`") calls what it
  thinks is `clock()` and runs `playerPosition` instead. It would read garbage or crash, and the version number
  (`kActorRulesVersion` 0.6) does not warn, because only 0.5 to 0.6 was bumped for `playerPosition`, and
  `placeCorpse` did not bump it.
- Confirmed: CONFIRMED by `git show 0c6ff3fd19:sdk/include/openeden/game/actor_rules.hpp` against HEAD.
- Fix: move both functions below `clock()` (the layout then matches what a 0.5 consumer had, plus the new ones
  at the end), bump `kActorRulesVersion` to 0.7 for `placeCorpse`, and extend `CI/check_extension_sdk.sh` with a
  vtable-order pin (a mirror interface test, as `world_source.hpp` has).

### 3. Forced greetings: one request at a time, found by base only (head-of-line blocking and the wrong actor)

- File: `game_packs/skyrimse/src/forcegreet.cpp:140` (`pendingBase`: the first Waiting entry of a map ordered by
  reference id) and `files/data/scripts/openeden/skyrimse/forcegreet_global.lua:15` to `:44` (`find` by base id).
- Defect: the Lua script serves only the first waiting request, and finds the actor as "the nearest object of that
  BASE in the player's cell". Two things follow.
  (a) If the first request's actor is not within 3,000 units of the player, `onUpdate` returns early and every other
  request waits until the first one times out (a brain request after `kBrainMaxAsks`, a scene request after
  `kTimeoutChecks` scene updates, both counts and not time). A scene greeting (Companions) can be held up by a
  brain request (Thieves Guild) in another cell.
  (b) The request carries a reference, but the script matches `:b<base>`. Many NPCs share one base (guards, bandits,
  generic draugr). The nearest actor of that base is activated, not necessarily the one whose package asked.
  `beginConversation` then asks `topicFor(speaker.mReference)` for the wrong reference, gets nothing, and opens a
  PLAIN conversation with the wrong NPC; `askedSerial` is now set, so it is not retried, and the real request stays
  Waiting until it times out.
- Confirmed: PLAUSIBLE on (b) for the quests shipped so far (Kodlak, Brynjolf are unique bases, so the first two
  clients are fine); CONFIRMED by reading on (a) and on the design.
- Fix: publish `(reference, base)` pairs; `pending_reference` as a decimal placement id, and look the object up by
  `skyrimse:o<placement>` or by reference; iterate all waiting requests, not only the first. Test: two waiting
  entries, the first unreachable.

### 4. Tolerant Papyrus native calls now allocate and take a map insert on every call

- File: `components/script_papyrus/papyrusinterp.cpp:827` to `:829`.
- Defect: `++mStats.unregisteredByName[label()]` builds the label string and inserts into a `std::map` on EVERY call
  of an unregistered native, before the `logOnce`. Task 0093's allocation tests pin 0 allocations for REGISTERED
  natives only.
- Failure scenario: the real Skyrim scripts call tolerant natives constantly (the logs list dozens of distinct ones
  and the thousands of calls per scenario: "calls 121 to 35"); with the host on, every one costs a heap allocation
  plus a log-n lookup in the script tick. The map grows to the number of distinct natives (bounded), so the cost
  is time, not memory.
- Confirmed: CONFIRMED by reading.
- Fix: increment a counter in a per-native record that already exists (`NativeInfo`), or build `label()` only the
  first time (inside the `logOnce` branch) and keep a pointer into the interned record afterwards.

### 5. `mCorpseRest` is keyed by a raw `LiveCellRefBase*` and is only swept while ragdolls exist

- File: `apps/openmw/mwworld/worldimp.cpp:1839` (the guard `actorRagdollCount() != 0`), `:1880` and `:1958`.
- Defect: stale entries of finished ragdolls are erased only inside `updateNeutralRagdolls`, which runs only while
  at least one ragdoll is alive. A new ragdoll for a different actor whose `LiveCellRef` is allocated at the same
  address (a cell unloaded and another built) finds `mCaptured == true` or an old `mFrames` and never captures its
  pose.
- Failure scenario: kill an NPC, let its ragdoll settle (pose kept), leave the cell, kill an actor in a new cell.
  If the allocator reuses the address, `trackNeutralCorpseRest` returns at `rest.mCaptured` and the new corpse has
  no saved pose, so after a load it falls back to the death clip's pose (the 0254 behaviour) with no log line.
- Confirmed: PLAUSIBLE (needs address reuse inside the window; the code path is certain).
- Fix: erase the entry when the ragdoll ends (`endNeutralRagdoll` path, `!alive`), and clear on cell unload.

### 6. Duplicated forced-conversation mechanisms (not one mechanism)

- Files: `game_packs/skyrimse/src/dialogue_runtime.cpp:571` (`beginForced`) and `:604` (`beginForcedTopic`);
  `game_packs/skyrimse/src/world_view.cpp:2402` to `:2437` and `:2499` to `:2512`.
- Observation: tasks 0264 and 0271 are on one `ForceGreet` request path (good, nothing of the old
  `requestForcedGreeting` / `mPendingName` remains). But the arrest conversation of task 0258 still has its OWN hook
  set (`mArrest`: `mWants`, `mBegan`, `mEnded`, `mFailed`, and `mForcedReference`) beside `mForcedGreet`
  (`mTopic`, `mBegan`, `mEnded`, `mPlainWhenNone`, and `mForcedGreetReference`), and the two runtime functions are
  about ninety percent the same text (set speaker, selected greeting, `fillTurn`, speaker name, `offerLinks`); only
  the INFO selection differs (`selectBarkInfo(subtype)` against `selectInfo(topic)`). `endConversation` has the two
  matching clean-up blocks. A third forced conversation (the Helgen execution, which task 0268 already lists) would
  copy a third time.
- Fix (not urgent): one `beginForcedWith(speaker, selectedInfo, out, error)`, one hook set with a kind tag, one
  `mForcedReference`.

### 7. The `Lua` `find` and the `forcegreet`-style "scan the cell by recordId" pattern is repeated per task

- Files: `files/data/scripts/openeden/skyrimse/forcegreet_global.lua` and the `crime_global.lua` / `rest_global.lua`
  / `pickup_global.lua` family. Several scripts walk `player.cell:getAll()` with a `pcall` per object each time a
  request is pending. The call is gated on a request, so it is not per frame, but each does its own matching rule
  (`:b(%d+)$`). Factor one helper (`openeden.skyrimse.find`) so that finding 3 is fixed once.

### 8. Test edit of an existing file (listed for approval)

- `apps/components_tests/extension/test_world_source_service.cpp:130`: the existing `theVersionIsOneZero` now asserts
  minor 3 (it was 1) while keeping its name. The root guide forbids editing an existing test without approval; the
  changelog row of task 0202 says the edit is awaiting it. The name no longer says what it checks. The other
  modified test files in the range only add lines.

### 9. Smaller notes

- `apps/openmw/mwworld/worldimp.cpp:1761` the `static std::uint64_t speechFrame` and the riding-trace `static` locals
  (`:~420`) are process-wide mutable state inside lambdas; harmless today (trace only), but they survive a new game.
- `World::clear` also keeps `NeutralDisplayNames` (a function-local `static` in `mwclass/neutral.cpp`) across sessions,
  as the 0150 task noted; names of a previous game stay until the process ends.
- Relocated and created actors (MQ101's cast, summons) are not rebuilt after a load, while the Papyrus state (version
  3 and later) restores aliases that name them (`papyrus_host.cpp:~2610`, `mCreated`): a quest alias can point to a
  created reference that no longer exists. This is the documented limitation of 0250 and 0268; it should be a loud
  log line at restore time. Not checked whether it is.
- `riding.cpp:updatePaired`: `!(mPhaseTime >= mPairedInfo.duration)` never ends the clip if the data gives a NaN
  duration (a hostile or damaged clip). The info is not validated on the pack side (`paired.cpp` was not read in
  depth).

## What was not done

No build, no test run, no in-game run. The areas listed above under "did NOT read" are open. A second review pass over
`combat*.cpp` (the largest source of per-frame work), `crime*.cpp` (the save ledger), `crafting*.cpp` and the HUD Lua is
recommended before these tasks are called done.
