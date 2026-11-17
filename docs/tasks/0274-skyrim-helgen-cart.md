# Task 0274: Skyrim main quest at Helgen, second slice (the cart ride)

Follow-up of task 0268. toddhow chose "Helgen cart and execution" on 2026-11-17. Same hand wrote the tests and the code
(tests first for the pure parts, run red against stubs); no fresh reviewer. Windows only. Branch
`worktree-agent-a89d8efb91ced1354` from `34beb9d459`. Behind the existing `[extension.skyrimse] main quest start`; no new
setting.

## 1. What the data says moves the cart (read with houseCARL and the decompiled `.pex`, `I:\Projects\OpenEden-work\bow6\helgen2\psc`)

- **The horses walk, the carts follow.** `MQ101CartHorse1Patrol1..5` and `MQ101CartHorse2Patrol1..5` are plain Travel
  packages (template `PatrolStaticPathing`, preferred speed Walk, flags MustComplete, IgnoreCombat) to the markers
  `MQ101CartHorse<n>Marker<k>`, each gated by `GetStage(MQ101) >= N` (Patrol2: 20; Patrol5: `GetStage < 60` and `>= 37`).
- **What sets stage 20 (and 22, 30, 32, 37, 40)** is not a fragment and not a scene: it is a row of **trigger slabs across
  the road**, REFRs `MQ101SetStage20`, `22`, `30`, `32`, `37`, `40`, `99` (base 0x046EFF, an XPRM **box**), each with the
  script `defaultSetStageTRIGSpecificActor`: `OnTriggerEnter(ref)` sets the quest stage when `ref`'s ACTOR BASE is the
  property `TriggerActor` (`MQ101Horse` 0x0654E5 for 20 and 22, `MQ101CartHorse` 0x0B9E1D for the others) and the quest's
  `prereqStageOPT` is done (15 for 20 and 22, 20 for 30, 10 for 40). So **the horse walking through the slab** is what the
  game's own scripts wait for. (Stage 20's slab: centre (21456.6, -79366.0, 8841.4), rotation 4.777 rad, bounds
  1351.8 x 192.1 x 348.3, a slab 2,700 long across a road that runs west into Helgen.) Stage 15 is set by scene
  `MQ101Scene1`'s fragment; stage 12's fragment (`Fragment_175`) calls `Cart.TetherToHorse(horse)` and
  `rider.SetVehicle(cart)` for every rider including the player.
- **The end of the ride:** `PF_MQ101CartHorse1Patrol5` (the end of the last package) sets stage 41; stages 41 to 64 call
  `MQ101QuestScript.ExitCart(alias, seat)`, which registers `OnAnimationEvent` for `"ExitCartEnd"` on the rider's alias
  script `MQ101CartRiderScript` and plays an exit idle; the script counts the events and sets 45, 46, 52, 56, 60, 64.

## 2. What was missing in the engine (all found by reading, then confirmed in game, see section 5)

| Gap | Before | Now |
|---|---|---|
| Box trigger volumes (XPRM type 1) | only spheres, only for the player | boxes (oriented by the REFR's Z rotation, bounds taken as HALF extents: RECALLED-VERIFY) for any "subject" actor; boxes fire only with `mAcceptBox` (set by `main quest start`) |
| `TetherToHorse`, `SetVehicle` | tolerant no-ops | natives that tell the pack; `CartRides` (pure) |
| `PlayIdle` / `RegisterForAnimationEvent` | no-ops | registrations kept; the pack announces an exit idle's end event after 3 s |
| A script-created actor's cell unloads when the player leaves the grid | the horse and riders vanish 2 cells into the ride | a neutral cell that holds an enabled script-created actor is not unloaded by a grid change (`MWWorld::unloadAfterGridChange`) |

## 3. What is hand-written (said plainly)

1. **The cart is a trailer of its horse** (`CartRides`): it keeps the distance it had at `TetherToHorse` behind the horse, on
   the line of the horse's recent travel direction (turning at most 1.5 rad/s), turned by the angle the line has turned;
   no Havok constraint. Riders keep their offset in the cart's frame.
2. **Riders are seated by the furniture machinery** (`useFurniture` with the chair idle, then `placeFurniture` every
   frame); the cart idles (`IdleCartPrisoner*`) are not played. The player is carried by `RidingWriter::carryPlayer`
   (third person, view fixed to the cart's heading).
3. **Exit:** an exit idle gets the rider off at once (stood 130 units to the right of the cart) and the game's script
   hears `ExitCartEnd` after 3 s of script time (`mainquest::kCartExitSeconds`).
4. The horses are watched for the trigger slabs only when they are tethered.

(Results and the ranked gaps follow in the stage log below.)

## 4. Stage log and results (honest state)

Built and unit tested: box/subject triggers, `CartRides` (trailer, riders, `isHorse`), cart brain (reference-alias
packages, end hook), tether/vehicle/idle/animation-event natives, enable-children propagation, the cell pin rule and the
mount limits for cart horses (`RidingRules::usesMountLimits`, SDK riding_rules 0.3). New tests: 25 + 5 + 4 + 1 (the
cart-ride tests were written before the code and run red against stubs; counts in `helgen2/red.txt`); the filter
`SkyrimCartRides.*:SkyrimTrigger*:SkyrimCartBrain.*` 46 pass, `NeutralKeepCell.*` 4 pass.

In game (`tools/skymainquest_check`, `quest alias packages`, `creatures` now in its default settings): both carts, both
horses and the riders appear (cart2 and horse2 through the XESP enable children), the cart ride scene plays, horses
walk their `MQ101CartHorse1Patrol1..` packages and drag the carts, riders and player are carried, and trigger slab
`000B8ED6` fires `OnTriggerEnter` for the horses.

**Not reached:** stage 20 and everything after it. Trigger 000B8ED6 is entered and left by both horses and sets no stage
(its prerequisite stage or base-actor test is not met: not established, the native trace did not show the
`GetStageDone` call); in the latest runs the horses wander near (22000, -92300) instead of following the road. Arrival
at Helgen, stages 41 to 64, the block and the execution are NOT reached. The ranked gaps: (1) why the horse keeps
turning round near the first road chunks (patrol path / navmesh agent radius for a horse), (2) which prerequisite the
first slab waits for, (3) package end for the last patrol, (4) exit idles and the arrival scene, (5) block and execution
(Alduin), untouched.

## 5. Second round (coordinator's order: investigate to the root cause)

Causes found by tracing, each fixed:
1. **Horses stopped at the first road bank:** a floor of nz 0.486 under a mount (limit 0.5): `kAnimalMinFloorNormalZ` is now 0.45.
2. **Horses stopped for good at 21221,-89465:** `Patrol1` needs `GetStage(MQ101) >= 15` and the scene's end fragment sets stage 14, which the pack took as the new current stage (the package then failed). `QuestState::setStage` now keeps the higher current stage (the lower one is still done and runs its fragment; RECALLED-VERIFY, but the game's own gates need it; it also explains task 0272's C00 race). New test `SkyrimQuestState.aLowerStageIsDoneButDoesNotLowerTheCurrentStage`.
3. **No slab ever fired a stage:** `GetBaseObject` handed out a `Form`, so `GetActorBase() == TriggerActor` cast to None; an NPC_ base now has the class `ActorBase` (`formClass`, preloaded class) and `GetBaseObject` uses it. The slab at 22474,-83662 (script `defaultSetStageTRIGSpecificActor`) is Alduin's (base 0x32B94) and correctly does not fire for a cart horse.
4. The C4834 warning (`sessionContent` nodiscard) is fixed; `OPENEDEN_PACKAGE_TRACE` now prints operator and value, and `PKTRACE` also logs patrol idles and link misses.

Result: both cart horses walk their whole Patrol1 chain (about 12,000 units, from (22200,-93600) to (24959,-82688)) with the carts, riders and player, and stop at the chain's end. **Stage 20 is still not set:** `MQ101SetStage20` (REF 0x84058) and `MQ101SetStage22` (0xC1F80) want base `MQ101Horse` (0x654E5), the officers' horses (`MQ101HadvarHorse` 0x654E1, `MQ101TulliusHorse` 0xA9DE7, `MQ101ElenwenHorse` 0x527F1), and here those have only `DefaultStayAtCurrentLocationSkipFallout` (no package moves them: whatever rides or leads them in the game, a rider's `MQ101StayOnHorse` package, is not run). Next step: find what moves them (riders' alias packages / `MQ101StayOnHorse`) and give a mounted-NPC mechanism (the horse follows its rider's goal).

Determinism: the trigger, stage and rider lines of runs r33 and r35 are byte identical (md5 7d26f082...); a third run was slower (machine load) and ended before the horse reached the chain's end, so its horse samples differ. Morrowind unchanged: `tools/worldobjects_check` 110 objects OK against the integ2 build; `coverage.sh` OK (no new setting). Tests: `*Skyrim*:*Extension*:*Neutral*` 3,101 and openmw `*Neutral*:*Mover*:*Extension*` 853 pass.
