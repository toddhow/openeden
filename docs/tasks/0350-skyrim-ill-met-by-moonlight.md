# Task 0350: Skyrim Daedric quest "Ill Met by Moonlight" (Hircine, DA05), first slice

Follow-up of tasks 0310, 0332, 0338, 0342 and 0345. Same hand wrote tests and code; the scene option's test was written first and
run red (1 of 2 failed against a stub option); the SetRace forwarding test was written after the code (no red run). No fresh
reviewer. Windows only. Local branch `moonlight-0350` from `3180c74263`, not pushed. Behind the existing
`[extension.skyrimse] daedric quests`; **no new setting**.

## 1. Survey (Python reader of Skyrim.esm, houseCARL records, `.pex` of `Skyrim - Misc.bsa` decompiled)

- `DA05` (QUST 0x2A49A, StartGameEnabled, RunOnce, priority 60, 26 aliases, stages 0 to 210). Stage 0 shows "Speak to Sinding".
  Sinding (NPC_ 0x6C1B7, ACHR 0x6C1B8) stands in `FalkreathBarracksJail` (a well pit with waist-deep water, `FarmBWell01`).
- **Start trigger.** `DA05LocationCheck` (story node, CLOC) starts on every location change; its fragment calls
  `DA05QuestScript.CheckForSceneStart`, which starts the funeral scene (`SF_DA05FuneralScene`, 8 phases, 13 actions: Mathies,
  Indara, Urunil) in Falkreath between 6 and 22 o'clock. The scene's begin fragment moves its actors out of their houses.
- **Sinding's talk** (the game's own topics, chain of real choices): greeting "Come to gawk at the monster?", "Why are you
  imprisoned here?", "What ring?", "What kind of transformations?", then "What will you do now?", "Why did this make you attack
  the girl?", "I'll take the ring to Hircine." (`TIF__0002BE54`: ring into the inventory, equipped, `SindingEscape.Start()`),
  "Who is Hircine?".
- **The escape scene** (`SF_DA05SindingEscape`, 4 phases): phase 0 walks Sinding to his home spot (package
  `DA05SindingMoveToHomeSpot`), phase 1 casts `WerewolfChangeFX` (a visual spell) and **waits until "Sinding is a werewolf"
  (GetIsRace on alias 0) OR the player is out of the jail cell**, phase 2 `SetRace(WerewolfBeastRace)`, phase 3 sets **DA05 stage 20**
  (`Fragment_2`: Kill the great beast, the white stag `DA05EncWhiteStag` 0x104F46 enabled).
- Later (surveyed, not run): the stag's `OnDeath` (3 s, ghost and talking activator moved to the body), the ghost's topics
  ("It shall be done as you ask." sets stage 50, `TIF__00015C64`), Bloated Man's Grotto (`dunBloatedMansGrottoQST`), the hunters
  (`DA05HuntersScript`, stage 70 to 80), Sinding's death (stage 60/61), the skin (stage 65) and Hircine's ring.

## 2. What runs in game (`tools/skymoonlight_check`, real clicks on the conversation box, frames viewed in `I:\Projects\OpenEden-work\followers\keep350`)

New game on the road south-east of Falkreath, hour 12, `daedric quests` with `quest alias packages`, `actor packages`, `actor cell moves`:

1. The story manager starts `DA05LocationCheck`; **the funeral scene begins** (8 phases, its begin fragment moves Mathies and the others
   into Falkreath's exterior, cells (-9, -21)).
2. In the jail: Sinding's forced greeting and the real chain above (six turns, real clicks), frames viewed. The last turn's response
   says "Oh my. You would do this for me? Here, take it."
3. Goodbye runs `TIF__0002BE54`; **the escape scene begins**, Sinding goes to his home spot (see fault 2), then waits (phase 1).
4. The harness puts the player out of the jail (a stand-in for walking out); **phase 2 sets Sinding's race, phase 3 sets DA05 stage
   20** by the game's own fragment, the scene ends.

Three runs of the whole scenario give the same 33 normalised lines (timestamps and poll numbers removed: md5
`b908595fd5a9e7cd8b68537d08f01f36`, exit 0, no dump, no error line).

## 3. Faults found by running it and fixes

| # | Fault | Cause | Fix |
| --- | --- | --- | --- |
| 1 | The funeral scene was refused ("none of its actors is loaded") | its actors stand in houses nobody loaded; its begin fragment is what moves them (the same shape as task 0333's actionless scene) | `SceneRunner::Options::mBeginFragmentStartsUnloaded` / `setBeginFragmentStartsUnloaded`: a scene with a begin fragment starts although no actor is loaded; on under `daedric quests` (a scene with none stays refused) |
| 2 | Sinding's escape scene waited 60 s in phase 0 | Sinding is a neutral actor that fell to z -512 below his well pit (cause not found, see 6), so the walk to a marker 416 above him has no path and the package never ends | `enableSceneWalks()` (task 0277's ONE mechanism: a scene's Travel package that cannot be walked ends with the actor at its destination) is now on under `daedric quests` |
| 3 | Phase 2 and stage 20 never came | the phase tests `GetIsRace(WerewolfBeastRace)` on alias 0; `Actor.SetRace` was a tolerant no-op | `Actor.SetRace` native (the host keeps the race of a reference; `PapyrusWorldHooks::setActorRace`), the pack keeps `mActorRaces`, `SkyrimConditionWorld::setRaceLookup` makes GetIsRace on a placed reference answer it (`SkyrimWorldView::setRaceLookup` hands it to the runtime); `Actor.GetRace` reads it too |

## 4. Stand-ins (hand-written, named)

1. Walks are Lua teleports: the road to Falkreath (`go`), into the jail (`cell`), **out of the jail** (`world 60`): leaving the
   cell is what the scene waits for in phase 1, so it is a stand-in for walking out.
2. `activateBy` of the Lua test script stands for the key press at Sinding.
3. The snap of fault 2 stands in for a walk that the engine cannot do.

## 5. Tests

`apps/components_tests/skyrimse/testskyrimsemoonlight.cpp`: `SkyrimMoonlightScene` (2: a scene with a begin fragment is refused
unloaded without the option and starts with it, its fragment runs; one without a fragment stays refused whatever the option; **1 of 2
failed first against a stub option**) and `SkyrimMoonlightRace` (1: the hook forwards the race, written after the code).
No existing test edited. `components-tests` filter `*Skyrim*:*Papyrus*:*Dialogue*:*Quest*:*Package*:*Scene*:*Extension*` 4,269 pass;
`openmw-tests` filter `*Neutral*:*Skyrim*:*Extension*` 1,190 pass (2 data-gated skips). The glue (`mActorRaces`, the lookup, the
`enableBeginFragmentScenes` and `enableSceneWalks` calls) has no unit test; the game run is its evidence.

## 6. Not reached (ranked)

1. The white stag: its `OnDeath` chain, the ghost and Hircine's talking activator, "It shall be done as you ask." (stage 50). The
   kill aid `summons test kill = DA05EncWhiteStag@N` did not kill it in three tries (the stag is enabled by stage 20 at
   `(-10348, -84411)`, 18 km from Falkreath; why the aid did not fire was not found).
2. Bloated Man's Grotto, the hunters, Sinding's death and skin, stages 60 to 210, the ring's power.
3. Mathies' funeral talk topics ("Who died?"): the scene runs, the player is not near it.
4. **Sinding falls through his pit (cause not found):** `NACTBODY ... grounded start z -128 rest z -512 frames 66`; the player
   stands at -127 on the same spot (probed with three teleports), so the floor exists for the player and not for the actor's probe
   ray (`NeutralActor` ray from 24 above the feet). A well model whose floor faces the wrong way for a ray is a guess.

## 7. Decisions for toddhow

1. `daedric quests` now also turns on `enableSceneWalks()` (scene packages that cannot be walked end by placing the actor): it
   changes the earlier Daedric slices only where a forced scene walk failed. Their harnesses were not re-run.
2. A scene with a begin fragment starts although no actor is loaded (`daedric quests` only): other quests' scenes with such a
   fragment may now start in those sessions.
3. `Actor.SetRace` keeps the race for conditions only (no body or skin change for an NPC).

## 8. Not run

The Linux gate, clang-format 14 (by hand), clang-tidy, the Lua tests, a fresh review, the three earlier Daedric harnesses after
decision 1, the stag/ghost/grotto part, saves in the middle of the quest. Morrowind unchanged against `integ2`'s build:
`worldobjects_check` 110 objects OK, Balmora frame `1c5d47ad0b5dc32d` for both.
