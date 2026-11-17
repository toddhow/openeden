# Task 0275: Thieves Guild, second slice (follow-up of task 0271): the real actions

Design doc section: `docs/design/` Skyrim plan, milestone M-S6/M-S8 (quests). toddhow chose "Thieves Guild next steps" on 2026-11-17.
Branch `worktree-agent-a0ba11a406fb4fc6c` from `34beb9d459` (main). Same hand wrote tests and code; the pure tests were written first and run red
against stubs (counts below); no fresh reviewer. Windows only.

Goal: replace the harness calls of task 0271 (the ring moved by the inventory binding) with real play: a lock picked by hand, the ring
taken through the container window, the ring planted on Brand-Shei through the pickpocket window (reverse pickpocketing), and the real
activate key finding the actor behind Brynjolf's stall.

Inherited code touched: `game_packs/skyrimse/src/pickup.*`, `pickup_service.*` (task 0208), `perks.*` (task 0179), `skyrimse_game_pack.cpp`
(`startPickup`), `apps/openmw/mwworld/neutralpick.*` (task 0233), `worldimp.cpp` (`getFocusObject`), `mwphysics/physicssystem.*` (one
query), `files/data/scripts/openeden/skyrimse/pickup_{global,player}.lua`, `tools/skythieves_check` (aids only). No SDK or contract file.
No new setting (everything sits behind the settings that already guard it: `inventory`, `crime`, `sneak`, `quest alias packages`,
`[Game] neutral inventory`).

## 1. What was found by running it (Skyrim.esm data, the real game's scripts)

* **The stall.** `TG00BrynjolfStall` (`b838487`) is not what blocks the key. The neutral player is a stub Morrowind NPC whose eye is
  **149.7 units** above its feet (`FOCUS render ray: eye ...z 11261.7` against feet 11112), above a Skyrim actor's head (about 128). The key's
  ray, level at pitch 0, passes over Brynjolf's head and ends on what hangs at eye height: the stall's awning (`ACTKEY focus:
  TG00BrynjolfStall, distance 46.7` from the west; `focus: nothing` from the start spot). A player had to look down a few degrees to talk to
  ANY neutral actor. With the pitch 25 degrees down the same position hits Brynjolf (`distance 124`).
* **Madesi's strongbox is behind two things.** `TG00MadesiChest` (REFR 0x76F14, base 487187) stands on the floor of Madesi's stall at
  (173050, -97338, 11120), locked (XLOC level 0, key 0xB8F42, the lock `neutralLockState` reads). The stall's front has a latticed
  **Sliding Door** (`RTMarketStallDoor01`, REFR 0xCC530, level 0, the same key) that hides it, and a glass-lidded **Display Case** (a Door,
  REFR 0xB8F39, level 25) stands on the table above it. From the south at pitch 55 the key finds the Sliding Door (a lock window opens);
  the lock was picked by hand (3 times: `LOCKBOOKS lock 000cc530 picked`), and with `[Game] neutral object animations` the door slides
  open (frame in `thieves2\runs\g5\...\screenshot010.png`, the strongbox's iron fitting is visible in the dark).
* **TG00PickpocketHelper** (PERK 0x10F21E) is entry point 56 (Mod Pickpocket Chance), function Add, value 30, with two tabs of conditions
  (printed by the gated test): tab 0, the player: `GetStage(TG00) >= 10`, `GetStage(TG00) < 30`, `HasPerk(0xBE124) == 0` run on the
  reference 0x14; tab 1, the victim: `GetIsID(Brand-Shei) == 1`. The perk engine answered every quest stage 0, so the perk never
  applied.
* **Brand-Shei lives in `RiftenHaelgasBunkhouse`** (REFR 0x19DDC at (-284, -194, 65)), not in the market; a Lua teleport into that interior
  loads it (the pack builds the cell) and he is there, 84 units from the entry, awake (Wujeeta, Haelga and Svana too).

## 2. What is built

### 2.1 Reverse pickpocketing (`skyrimse.pickup` stays 1.0; `plant` is an additive Mutate function)

`PickupService::plant(owner, item, count)`: the same chance rules as `take` (value, weight, the Pickpocket skill, the perk bonus), a seeded roll
of its own (`owner xor kPlantSalt`, the global attempt counter: no clock), an aware victim always catches the hand (task 0227), success
moves the items from the player to the NPC's list (the inventory's change sink then tells the Papyrus host, so the game's own
`TG00RingScript.OnContainerChanged` runs), a failure is the same crime as a caught take (`Kind::Pickpocket`, victim marked detected). Each
try trains Pickpocket. The Lua window (`pickup_player.lua`) turns a click on **Put** into the event `SkyrimPickpocketPlant`; a state of
`planted` shows the message like a success.

### 2.2 The perk bonus (Mod Pickpocket Chance)

`chanceWithBonus` (percentage points, clamped to the chance's own range) and the hook `Hooks::mChanceBonus(victim)`; the glue
(`pickpocketPerkBonus`) runs entry point 56 of the perks `Actor.AddPerk` gave the player (`PapyrusHost::perksOf`) through a `PerkEngine` with
the player as owner, the victim's base as target, **`PerkConditionWorld::setQuestStageSource`** (GetStage reads the quest state:
`PapyrusDriver::questStage`) and **`PerkConditionWorld::setPlayerRole`** (a condition run on the reference 0x14 asks the player's
participant). Both setters are additive; without them the world answers as before. The bonus also applies to taking.

### 2.3 Picking an actor behind an obstacle (`World::pickNeutralActorBehindObstacle`, `neutralpick.*`)

With `[Game] neutral inventory` the crosshair also picks a living neutral actor by a box round its feet (radius 26, height 128 x scale plus
**32 of headroom**, the stub eye's overshoot) when the rendered ray hit nothing activatable and the actor's head (85 percent of its
height) is in clear view of the eye (`PhysicsSystem::neutralLineBlocked`, static geometry only: a counter is lower than the eye, a wall is
not). The crosshair only (not the mouse in a window). The same mechanism as task 0233's furniture box. `OPENEDEN_FOCUS_TRACE=1` (a
developer aid, like `OPENEDEN_ACTIVATE_TRACE`) logs the rendered ray and the picks.

## 3. Red counts (tests first)

* `components-tests`: `SkyrimPlantBonus` 2 of 3 failed, `PlantFixture` 7 of 9 failed against stubs (the other 3 pin refusals and the
  seeded draw), `SkyrimPerkWorldTg` (written when the perk's conditions were found) 2 of 5 failed before `resolveRunOn` was changed
  (the stage source setter existed already); gated `SkyrimThievesGuild2Real` prints the perk's shape and passes.
* `openmw-tests`: `NeutralActorPick` 4 of 6 failed against stubs; the headroom test was added after the in-game finding (it fails against the
  old box).
* Not red first: the engine wiring (`getFocusObject`, `PhysicsSystem::neutralLineBlocked`), the Lua windows and the glue (`startPickup`
  hooks): they are proven by the in-game runs only.
* No existing test was edited.

## 4. In game (`tools/skythieves_check` with `STEPS`, `ENVF` scripts in `I:\Projects\OpenEden-work\combat5\thieves2`)

Real input through `OPENEDEN_RMLUI_INPUT_SCRIPT` (the Space key, SDL mouse clicks and the controller buttons); a new game in Riften's market;
the game's own quests, dialogue, scenes and Papyrus.

1. The game's forced greeting, seven turns, the second talk opened by the REAL controller A at the start spot (Brynjolf at 182.7 units:
   `ACTKEY focus: Brynjolf`; **before the change the same press gave `focus: nothing`** (run b1)), "I'm ready": TG00 stage 10 (the perk is
   added by `Fragment_4`).
2. The strongbox's lock window (the event the engine sends for a locked container: **a stand-in for aiming**, see section 5), 16 clicks on `<`
   and 4 on Turn: `LOCKBOOKS lock 00076f14 picked`. The container window (again the engine's event); a REAL click on **Take** at the ring:
   `INV transfer 00076f14 -> 00000000 item 00076f12 x1: ok`, `PAPYRUS quest 00021556 set to stage 20` (the game's own `TG00RingScript`),
   `CRIME theft not witnessed` (nobody in view; the rule is task 0204's).
3. A teleport into `RiftenHaelgasBunkhouse` (a stand-in), the controller's left stick for the sneak stance, the REAL Space key:
   `ACTKEY focus: Brand-Shei`, the pickpocket window; a click on **Put** at the ring: `PICKPOCKET the perks add 30 percent`,
   `PICKPOCKET 00019ddc planted 00076f12 x1 at 54%`, then `PAPYRUS quest 00021556 set to stage 30` (`Fragment_8`) and
   `TG00SpectatorQuest` stage 20 (the arrest scene). Without the perk (before 2.2) the chance was 24 percent and the first roll (25.87)
   failed: `caught trying to plant ... at 24%`, `CRIME pickpocket witnessed by brandshei ... bounty +25` (run h7).
4. **Three runs** of the final scenario (to the plant, 6,000 frames): see the changelog row for the result.

## 5. What was hand-written (not the game's own)

* The harness (`tools/skythieves_check/luatest/scripts/mq`): the player's placement (`STEPS`: `abs`, a teleport onto the spot of a floor, with
  a yaw and a camera pitch held by `mq/player.lua`), the strongbox's two events (`event`: **the lock window and the container window are
  opened by the same events the engine's `Neutral::activate` sends, because the aim at a chest inside a cupboard was not reproducible: the
  camera pitch of the first look after a teleport changes the focus distance by about 15 units from run to run (106.3 or 121.7 to the
  stall front or the door) and the door slides shut again after 6 seconds (task 0232), so a scripted press at the chest missed;** the Sliding
  Door itself WAS opened by the real key and the real clicks), the teleport into the bunkhouse and the game hour 12.
* The perk bonus rule is the game's (entry point 56, Add 30 and its conditions: data); the chance formula, the roll and the 5 and 90
  percent limits are task 0208's numbers (ours).
* The headroom of the actor box (32) is ours, named in `neutralpick.hpp`.

## 6. TG01 and what is still missing (ranked)

TG01 ("Taking Care of Business", quest 0x1F326) starts by TG00's stage 200 (`Fragment_10`: `TG01.SetStage(10)`), which needs Brynjolf's
after-plant forced greeting and the outro topics (task 0271 reached it with the plant done by the harness). Its own data (`QF_TG01_0001F326`
decompiled): stage 10 displays objective 10 (find Brynjolf at the Ragged Flagon), later stages pay Haelga, Bersi Honey-Hand and Keerava's
debts (`pTG01Gold`), with `TG01PlayerAliasScript`, `TG01HaelgaStatueScript`, `TG01BersiDwemerUrnScript`, scenes and the assault monitors.

1. **After the plant in the bunkhouse the forced greeting did not come back** (runs j2 to j4): the player returned to the market by a
   Lua teleport, `FORCEGREET` lines appeared for two prison guards only, never for Brynjolf, and the real key at 181.8 units opened no
   conversation. The PKTRACE shows `TG00BrynjolfForceGreetPlayerAfter fails [fn58 no]` until stage 30 and, after the return, only the
   package list. Candidate cause (NOT verified): the first package of his list is `TG00SPBrynjolfCauseDistractionStatic`, a scene package
   whose scene (`TG00SPArrestScene`, started by stage 30) waits for actors that are not loaded (Brand-Shei and the guard are in interiors).
2. **Aiming at the strongbox.** The cupboard's door closes again 6 seconds after it opens, the container is reached through a slot in the
   front, and the first-person camera pitch is not reproducible by a script; a human could do it, an input script could not. The door could be
   kept open (the real game's door toggles and stays), which would also need `neutral object animations` on.
3. **The Display Case** (a glass-lidded door above the strongbox) is picked at lower pitches and offers an Apprentice lock; what is in it is
   not known.
4. **Detection while pickpocketing** works at the first judgement only: the victim and bystanders notice within a second of sneaking
   (`SNEAK ... detected`), and an aware victim always catches the hand, so the plant succeeds only in the first 0.5 s of play (the window
   pauses the world). Skyrim's own rule (facing, light, the Muffled/Light-Fingers perks) is not built.
5. Brynjolf walking up to the player, TG01's debt collection (assault and persuasion rules, the urn and the statue scripts), the Ratway
   and the Ragged Flagon (interiors with doors for NPCs), the arrest scene: not run.

## Decisions for toddhow

1. The actor pick has 32 units of headroom because the stub player's eye is above Skyrim heads. The alternative is to lower the player's
   camera height for a Skyrim session (a first-person arms rig has its camera node at 120.5, task 0245), which would change every ray the
   player casts; I did not do that.
2. `plant` was added to `skyrimse.pickup` 1.0 without a version bump because an existing test pins 1.0.
3. Planting stays a crime when caught (the same `Kind::Pickpocket` bounty as taking).
4. `PerkConditionWorld` gained two optional setters; no existing call changes.

## Stage log

Not run: the Linux gate and GCC `-Werror` (hand-checked), clang-format 14 (not installed; formatted by hand to 120 columns), clang-tidy, the
Lua integration tests, a fresh review, TG01 beyond its first stage, the arrest scene, the Ragged Flagon. Scratch output of the runs is in
`I:\Projects\OpenEden-work\combat5\thieves2` (the BSA extract was deleted).
