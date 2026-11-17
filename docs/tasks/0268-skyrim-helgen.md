# Task 0268: Skyrim main quest opening at Helgen, first slice

Design doc section: `docs/design/` Skyrim plan, milestone M-S8 (main quest). Approved by toddhow (first slice).
Branch `helgen` from `457bb898b5`. Same hand wrote tests and code (tests after the code for the new parts; mutation checks
and the in-game run are the evidence, see "Red counts"). No fresh reviewer. Windows only.

Goal: a new game starts at the cart, and the game's OWN quest MQ101 "Unbound", its scene, aliases and Papyrus run the
opening as far as the engine allows; this first slice stops where the cart ride would have to move.

Inherited code touched: `game_packs/skyrimse` (`skyrimse_game_pack.cpp`, `papyrus_host.cpp/.hpp`, `papyrus_world.hpp`,
`scene_runner.cpp/.hpp`, `quest.cpp/.hpp`, new `main_quest.hpp`, `extension.json`), `apps/openmw/mwstate/neutralnewgame.hpp`,
new SDK header `sdk/include/openeden/game/new_game_start.hpp` (additive, optional interface found by `dynamic_cast`).

## 1. The data (Skyrim.esm, read with houseCARL and the decompiled scripts of `Skyrim - Misc.bsa`)

**Quest MQ101 "Unbound"** (0x03372B, flags RunOnce, priority 80; NOT start-game-enabled: nothing in vanilla starts it but the
engine's new-game path). 124 stages (0, 2, 3, 5 to 10, 12, 14, 15, 20 ... 1000), 7 objectives ("Make your way to the
Keep" 30, "Enter the Keep with Hadvar or Ralof" 50, "Find some equipment" 60, "Loot Gunjar's body" 65, optional barrel
and cage 70/75, "Escape Helgen" 100), 136 aliases, scripts `QF_MQ101_0003372B` (159 fragments), `MQ101QuestScript`,
`defaultquestrespawnscript`. Quest MQ101DragonAttack (0x0D0593, scenes of Alduin).

**Stage 0 is the start-up stage and has FIVE log entries**, each conditioned on the global `MQQuickstart` (0x04679E): 0
runs `Fragment_2` (game hour 7, `SetStage(10)`), 1 to 4 are the developer shortcuts (skip to the arrival, to the keep,
to character creation, to the end). The host ran all five at first (stage entry conditions were only checked with
`houses`); now they are checked with `main quest start` as well.

**The opening, as the data says it runs:**

| Stage | What happens (fragment / script) |
|---|---|
| 10 | `Fragment_4`: strips the player to the prisoner clothes and cuffs, `moveto`s every actor alias (Hadvar, Ralof, Tullius, Ulfric, the soldiers, the four Stormcloak prisoners, Lokir = Prisoner01, Elenwen) and both carts and horses to the start markers (REFR 0x04678E `MQ101PlayerStartMarker` at 22499.8, -94862.6, 13905.8 in Tamriel), fades in over 14 s, `ShowTitleSequenceMenu` + `StartTitleSequence` (the "Skyrim" title cards; timed by `MQ101QuestScript.OnUpdate`), cart sound, intro music, opens the north gate |
| 12 | `Fragment_175`: waits until every actor `Is3DLoaded`, `TetherToHorse` both carts, `SetVehicle` of every passenger on a cart, plays the sway idles. Set when two `MQ101StartingCellLoadRegisterScript` alias scripts got `OnCellLoad` |
| scene `MQ101Scene1` (0x0BECD4, begins on quest start, 29 phases, 13 actors, 43 actions) | the cart dialogue (Ralof, Lokir, Hadvar, the soldier, Ulfric): phase 0 "Put everyone in carts", phase 1 "Wait for cell to load", timers and dialogue; its fragments set stages 14, 15 and 35 |
| 14, 15 | 14: help message "Look"; 15: `Fragment_177`: `EvaluatePackage` on Tullius, both horses and Hadvar, `SetSittingRotation` |
| 20 | `Fragment_109`: opens the east gate, stops the intro music, `EvaluatePackage` on the horses and Hadvar |
| 35 | `Scene1.Stop(); Scene3.Start()` (Scene3 0x04679B: 20 actors, the arrival chatter) |
| horses | packages `MQ101CartHorse1Patrol1..5` / `CartHorse2Patrol1..5` (Travel to markers, picked by the quest stage; Patrol5 is the last): the horses WALK the road to Helgen; the cart is tethered to the horse by Havok. The fragment of `Patrol5` (`PF_MQ101CartHorse1Patrol5_000BF475`) sets stage 41 when it ends |
| 41 to 46 | arrival: `ExitCart(alias, seat)` plays the exit idles; `MQ101CartRiderScript` counts `ExitCartEnd` animation events and sets stages 45, 46, 52, 56, 60, 64 |
| 50 .. 100 | the Helgen square: Scene4 (0x0CD68E, 55 phases, 23 actors, 251 actions: Tullius, Ulfric, the execution list, Hadvar/Ralof), stages 65, 67, 70 (the block), 75 (`ShowRaceMenu`, character creation, `Fragment_13`), 80 (`SetInChargen(false)`, autosave, race spells), 82 to 100 |
| 100 .. 250 | Alduin (`MQ101DragonAttack` stage 200, `MQ101AlduinAttackScene1` 0x0AA7DF, 22 actors), the choice of Hadvar or Ralof (`FactionPath` 1 or 2), the keep scenes `MQ101KeepIntroSceneA/B`, `KeepScene2A/B`, `4A/4B`, `5A/5B` |
| 250 .. 1000 | the keep (cage, torture room, pool room, bear; stage 260 on the keep key, 270 on the exit), escape; stage 1000 stops the quest |

Aliases of note: `Hadvar` (unique actor 0x02BF9F, persistent REFR 0x02BFA2 at (20044, -79478), 15 km from the start),
`Ralof` (0x02BF9D, REFR 0x02BF9E, same place), `Alduin` (unique actor 0x032B94, REFR 0x032DB7 at (22508, -85386, 12037)),
`GeneralTullius`/`Ulfric`/`Elenwen` (forced references), `Prisoner01` (Lokir), four anonymous Stormcloak prisoners,
the carts and cart horses.

## 2. Ranked engine gaps (what stops the full opening)

1. **Moving an actor or an object into a place the world has not loaded.** `ObjectReference.MoveTo`/`TryToMoveTo` of an
   actor was refused by the engine ("actors are placed by the actor system"), and an object of another, unloaded cell
   "reaches no loaded cell". Hadvar and Ralof stand 15 km away; the quest moves them to the cart. Without this nothing of
   stage 10 happens and the scene never starts (no actor loaded). **Built in this slice** (section 3.2).
2. **A scene that begins on quest start gave up at once when its actors were not loaded.** **Built** (3.3).
3. **Cart vehicles: moving carts, tethered horses, passengers riding.** `TetherToHorse` and `SetVehicle` are tolerant
   no-ops; the cart (an activator/moving static) must follow the horse kinematically and carry its passengers with their
   sit idles. Needs: a vehicle table in the pack (cart, horse, passengers with their offsets), a move per frame through
   the world service, an engine path for moving a neutral ACTOR every frame (`ActorRequestWriter::placeFurniture` is a
   teleport for furniture only), and the horse patrol packages running on a creature over the exterior navmesh
   (`PackageBrain` runs Travel for people; horses use the creature profile of task 0212 but no brain). **Not built.**
4. **Package-end fragments (`PF_*`).** `Patrol5`'s end fragment sets stage 41 (arrival). The brain does not run package
   fragments. **Not built.**
5. **Scripted camera and player control.** `DisablePlayerControls`, `SetPlayerAIDriven`, `ShowFirstPersonGeometry`,
   `SetSittingRotation`, the walking-camera idles and the sit/furniture camera of the cart and the block are no-ops.
   The player body is the stub player; there is no sitting player and no forced third-person cart camera. **Not built.**
6. **Title sequence and fades.** `StartTitleSequence`, `ShowTitleSequenceMenu` (the four "Skyrim" cards over the black fade)
   and `FadeOutGame` have no UI. Natives are now registered as no-ops. **Not built.**
7. **Execution scene (stage 70 to 75).** The block: the player kneels at the stump (`PlayerToChoppingBlock` package, the
   KillMove idle of the headsman), Alduin's roar interrupts it. Needs a kill-move idle, the headsman package, `ShowRaceMenu`
   (task 0225's creation window exists, `ShowRaceMenu` is not bound) and the player forced to a place and heading.
8. **Alduin flying over, perching on the tower, breathing fire; collapsing buildings.** Task 0253's dragons exist as
   combat creatures; scripted flight paths (`MQ101AlduinScript`, `SF_MQ101DragonAttack*`) need flying by animation
   events (`FlightLanding`, `TowerLandImpact`), `Alduin.GetActorRef().PlayIdle`, and destruction stages
   (`SetDestroyed` / `ClearDestruction`, `DestructibleObject`). Collapsing geometry has no engine path at all.
9. **Forced dialogue and follow packages in the keep** (Hadvar/Ralof leading the player, `MQ101FriendScript`, the locked
   cage and levers, `MQ101LeverScript`, `SetLockLevel`/`Lock` natives unregistered), door and gate natives (`SetOpen`).
10. **Scene timing**: scene dialogue lasts a character-count estimate (task 0194), not the length of the voice file.

## 3. What this slice builds

### 3.1 `main quest start` (new pack setting, off by default, `[extension.skyrimse] main quest start`)

A new game starts at the cart instead of where `--start` says. The pack tells the core through the new optional SDK
interface `openeden::sdk::game::NewGameStartSource` (`new_game_start.hpp` 0.1, DRAFT, implemented by `SkyrimCellSource`;
the frozen `WorldCellSource` is untouched). `NeutralNewGame::run` asks the registered source; a non-empty answer wins over
`--start` (a game with an opening of its own). The text is `@Tamriel,22499.8,-94862.6,13905.8`, the place of
`MQ101PlayerStartMarker`. When the host runs, the pack starts MQ101 once (`PapyrusHost::startQuestForTest`, refused when
the quest runs or is done, so a load does not start it again). Nothing of the opening is hand-written beyond that.

### 3.2 Moving what the world cannot move (hand-written glue, near the player only)

`PapyrusHost::setRelocation(true, 7000)` (on with `main quest start`): a `MoveTo` whose destination is within 7,000 units
of the player makes a NEW reference of the same base at the destination through `PapyrusWorldHooks::relocateObject`
(`WorldSourceService::placeActor` for an actor, `placeObject` for an object of another cell), hides the old reference,
records the new one as a created reference, and `QuestState::replaceReference` points every alias that held the old
reference at the new one, so the quest's own scripts (`Alias_Hadvar.GetActorRef()`) and its scenes find the live actor.
A later `MoveTo` of the old reference moves the new one (the host keeps a redirect). Created actors are NOT saved
(task 0250's rule): after a load the relocated cast is missing.

### 3.3 Scenes wait for their actors

`SceneRunner::Options::mRetryQuestStart` (on with `main quest start`): a begin-on-quest-start scene refused because no actor
is loaded is asked again every second while its quest runs.

### 3.4 Natives and events

`ObjectReference.Is3DLoaded` (asks the world: an actor is loaded when the actor system holds it), no-ops for
`Game.SetHudCartMode`, `StartTitleSequence`, `ShowTitleSequenceMenu`, `HideTitleSequenceMenu`, `ObjectReference.SetOpen`
and `TetherToHorse`; `PapyrusHost::replayAliasCellLoad`: the quest starts after the cells loaded (the game starts in the
cell, the quest a moment later), so the pack tells the quest's alias scripts `OnCellLoad` once (the two
`MQ101StartingCellLoadRegisterScript` instances set stage 12).

## 4. The cart (exact answer)

**The cart does not move in this slice.** The carts, horses and passengers are placed at the start markers by the quest's
own stage 10 and stand there; the ride's dialogue plays while they stand. The cart ride is gap 3 and 4; arrival at Helgen
is therefore NOT reached by the game's scripts in this slice (see "Results").


## 5. Results (Windows, build tree `I:\Projects\OpenEden-work\ragdoll5\build`, five Skyrim masters)

In game (`tools/skymainquest_check/check.sh`, real new game, `OPENEDEN_QUIET_WINDOW`, volumes 0, fixed frame time):

- The new game starts on the cart (`Neutral start: the game 'skyrimse' starts a new game at '@Tamriel,...' (not '...')`), MQ101
  starts, stage 0 runs only its own entry `Fragment_2` (hour 7, stage 10), stage 10 runs `Fragment_4` and `Fragment_79`:
  every cast member alias is relocated to the cart (22 `relocate ... made FF8000nn` lines, Hadvar and Ralof from 15 km
  away), 48 cell-load events set stage 12 (`Fragment_175`).
- `MQ101Scene1` begins (after waiting for its actors), phases 0 to 3 pass (the player's `HasLoaded3D`, stage 12 done, a
  45 s timer), then phases 4 to 19 say the game's own lines in order, each by the right relocated actor, with the voice
  file found (`SCENESPEECH voice voice/skyrim.esm/malenord/mq101__00101aea_1.fuz` ...) and the subtitle: "Ralof: Hey, you.
  You're finally awake.", "Lokir: Damn you Stormcloaks...", "Ralof: We're all brothers and sisters in binds now, thief.",
  "MQ101ImperialSoldierCartDriver: Shut up back there!" ... to "Lokir: Rorikstead. I'm... I'm from Rorikstead." (phase 19).
  The scene then WAITS at phase 20 for `GetStageDone(MQ101, 20)`; nothing in this slice sets stage 20 (the cart's progress;
  the setter was not identified), so the scene stays there. Frames:
  `I:\Projects\OpenEden-work\ragdoll5\helgen\keep\` (`cart_start.png`: the cart and a prisoner at the start;
  `scene_line1.png` and `journal_empty_during_ride.png`: subtitles over the cart view).
- **The cart does not move and the quest does NOT reach the arrival at Helgen** (gaps 3 and 4). The carts, horses and
  prisoners stand at the markers the quest gave them (the nearest objects print the same positions in every sample).
- **The journal** lists nothing during the ride (`journal_empty_during_ride.png`, "No active quests"): MQ101 has no log text
  and displays no objective before stage 160. With `main quest test stage = 160,8` the journal shows "Unbound - running -
  [ ] Make your way to the Keep" (`journal_unbound_test_stage160.png`).
- Three runs of the ride (6,000 frames each): runs 1 and 2 give byte-identical `SCENE`, `SCENESPEECH`, `PAPYRUS quest`,
  `relocate` and `MoveTo` lines (75 lines, md5 `97a31251f0e1`); run 3 has the same 75 lines plus 12 more (it ran further in the
  same number of frames: the Papyrus host starts when a worker has read the dialogue data, and how soon varies). So the
  prefix is identical, the length is not.
- Morrowind unchanged: `tools/worldobjects_check` 110 objects OK and the fixed Balmora frames `1eb68236352dc472` and
  `c819db116f4af8a6` equal to the reference build (`integ2`, a build of main, not of this commit's exact parent); this task
  changes no Morrowind path (`neutralnewgame.hpp` is the neutral start only).

### Tests

New `components-tests` (`testskyrimsemainquest.cpp`, 12: relocation 8, scene wait 3, constants 1) and `openmw-tests`
(`testneutralnewgamestart.cpp`, 4). **Not run red first**: the pack tests were written after the code. Evidence instead:
three one-line mutations built into `components-tests` and reverted (replaceReference returns 0, the scene retry off, the
relocation off): 5 of the 12 tests failed (`aMoveToNearThePlayerMakesANewReference...`, `aWorldThatRefuses...`,
`aSecondMoveOfTheOldReference...`, `replaceReferenceChanges...`, `aWaitingSceneBeginsWhen...`); the other 7 are controls that
pin absence (off by default, far from the player, no player known, a dropped scene, constants, `is3DLoaded`). The engine
test could not compile before `chooseNeutralStart` existed. No existing test edited. After the mutations were reverted
the 12 pass, `components-tests` (`*Skyrim*:*Scene*:*Papyrus*:*Extension*:*Neutral*:*Frozen*`) 1,070 pass and `openmw-tests`
(`*Neutral*:*Extension*:*Lua*:*Skyrim*`) pass.

### Not built or not run

The cart ride (gaps 3 and 4), arrival at Helgen, the execution scene and character creation at the block, Alduin, the keep
(none of the stretch); saves of the relocated cast; the Linux gate and GCC `-Werror` (hand-checked for the traps),
clang-format 14, clang-tidy, the Lua tests, a fresh review; the alias cell-load replay has no unit test.

### Decisions for toddhow

1. A `MoveTo` near the player of an actor, or of an object of another cell, makes the thing again instead of moving it
   (a new reference id, the old one hidden). It is on only with `main quest start`; other quests' start-up moves far from
   the player are unchanged. Alternative: a real engine path that moves an actor across cells (keeps the id, saves).
2. `main quest start` replaces `--start` for a new game in a Skyrim session (the pack's answer wins). A save made from this
   start cannot restore the relocated actors (task 0250's rule for created actors).
3. The ride's stage 20 and the cart arrival need vehicles; which engine path (a kinematic cart following the horse, or a
   skip that runs the horse package's own end fragments) is yours to choose.
4. The sky was still night in the start frames although `GameHour.SetValue(7)` ran: the neutral game clock and the global
   `GameHour` are different things; not investigated.
