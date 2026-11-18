# Task 0294: Skyrim Dark Brotherhood, start of the questline (DB01 "Innocence Lost", DB02)

Status: DB01 RUNS END TO END, 2026-11-18 (section 4). DB02 not started. The survey below is from 2026-11-17.
Reason: this machine ran five agents' ninja builds at once; my full tree needed about 3,900 steps, the test targets crawled
(about 4 steps an hour at the end), and the `openmw.exe` link had not finished when I stopped. Branch is the worktree branch
from `0f4cca6e35`. The survey below is from the data (housecarl record reads and decompiled `.pex`, extracted to
`I:\Projects\OpenEden-work\ragdoll5\psc`, not committed).

## 1. Data survey (Skyrim.esm, read from the records and decompiled scripts)

* **DB01 `Innocence Lost`** (QUST 0x1EA50, flags StartGameEnabled | RunOnce, type DarkBrotherhood, priority 70). It runs from
  the first frame, so the pack's start-up quest path already starts it. Stages 0, 5, 20, 30, 200, 255 (255 shuts down).
  Objectives 5 "Talk to Aventus Aretino", 20 "Kill Grelod the Kind", 30 "Tell Aventus Aretino that Grelod is dead".
  Seven reference aliases, all `UniqueActor` except Astrid (forced ref 0x1BDE8): Aventus Aretino 0x14132, Grelod the Kind
  0x1335E, Hroar 0x13363, Runa 0x13378, Samuel 0x1337A, Francois 0x13359. Script `DB01QuestScript` (Conditional ints
  EnterOrphanage, GrelodDeadChoice, GrelodThreatened, ChildrenCheer, GrelodAlreadyDead), fragments in `QF_DB01_0001EA50`:
  stage 0 displays objective 5; stage 20 completes 5 and displays 20; stage 30 completes 20 and displays 30; stage 200 adds
  the Aretino house key to Angrenor (`AddItem`, `FormList.AddForm`), `ModFavorPoints` on Aventus, completes 30,
  `utility.Wait(5)` then `Stop()`.
* **Who sets the stages.** Stage 20: INFO fragment `TIF__0001F339` (Aventus): sets `DB01Objective` (QUST 0xBC8B7) stage 200
  when it is at 10, then `DB01.SetStage(20)`; its second fragment sets `GrelodDeadChoice`. Stage 30: the NPC script
  `pGrelodScript` (Actor, properties pDB01, pChildrenScene) `OnDeath`: stage 30 only if the quest is at 20,
  `GrelodAlreadyDead = 1` if it is below 20, then `pChildrenScene.Start()` (scene `HonorhallChildrenCheerScene`,
  `SF_HonorhallChildrenCheerScen_000214D9`). Stage 200: Aventus's `DB01AventusGrelodDeadBranch` topics. The Honorhall INFOs
  `TIF__000153B8` and `TIF__00016CA5..8` set `DB01Objective` stage 10 while DB01 < 20 and `TIF__0001F33A`/`0001F33D` set
  `GrelodThreatened`.
* `pAventusScript` (ReferenceAlias, OnUpdate): while DB01 < 10 and the player is in location `WindhelmAretinoResidence`, starts
  the Black Sacrament scene `pAventusSacramentScene` once (`DarkBrotherhood.BlackSacrament`).
* Cells: `WindhelmAretinoResidence` (0x16784), `RiftenHonorhallOrphanage` (0x16BD3). The orphanage is in RIFTEN.
* **DB02** (QUST 0x1EA51, `QF_DB02_0001EA51`, `DB02Script`): Fragment_14 sets `DB01.SetStage(200)`; stage 9 adds the player to
  `DarkBrotherhoodFaction`; uses `Game.EnablePlayerControls`, `RegisterForUpdate`, `SetEssential`, `AddAchievement`,
  `WICourierScript.RemoveRefFromContainer`, and the entrance quest script `pSleepyTime` set by the sleep event. The sleep
  trigger and the DB02A "Sanctuary" transport are not surveyed beyond this.
* Natives the Papyrus host probably lacks (unconfirmed, `OPENEDEN_PAPYRUS_NATIVE_TRACE` was not run): `Actor.ModFavorPoints`,
  `Quest.Stop`, `Game.AddAchievement`, `Game.EnablePlayerControls`, `FormList.AddForm`.

## 2. Plan I could not start (ranked)

1. Start in `WindhelmAretinoResidence` (`LOADARG="--start WindhelmAretinoResidence"`), talk to Aventus by the real key, check
   TIF 0x1F339 sets DB01 stage 20 and the journal shows "Innocence Lost: Kill Grelod the Kind". Scaffold: `tools/skydb_check`
   (copied from `tools/skythieves_check`; decimal base ids: Aventus 82226, Grelod 78686).
2. Kill Grelod in `RiftenHonorhallOrphanage`, expect `OnDeath` on the actor script, stage 30, the children scene, then talk to
   Aventus for stage 200 and the reward.
3. DB02's sleep trigger.

## 3. Not run / decisions

Nothing built or run: no red counts, no frames, no Morrowind comparison, no Linux gate. No existing test edited. The scaffold
`tools/skydb_check` is unrun; the `--start @WindhelmWorld` default inside its `check.sh` is a placeholder, pass `LOADARG`.

## 4. Results (2026-11-18, Windows, build tree `I:\Projects\OpenEden-workagdoll5uild`, five Skyrim masters)

Harness: `tools/skydb_check/db01.sh <name> [frames]` (a copy of `tools/skymarriage_check` with marriage off and `quest alias packages`
on; plan steps `talk` and `cell`; input script from `tools/skydb_check/clicks.py`; `OPENEDEN_PAPYRUS_NATIVE_TRACE=1`, `story trace`).
Settings on: `quest alias packages`, `summons`, `summons test kill = Grelod`, `combat trace`, `controller menus`, journal.

**The surprise: no new native or pack piece was needed for DB01.** The game's own scripts, dialogue and scene run it:

1. A new game in `WindhelmAretinoResidence`; DB01 (`StartGameEnabled`) is started by the start-up path (the Aventus alias is filled).
2. Real mouse clicks on the conversation box with Aventus Aretino (voiced, real INFOs): "You've come at last. I knew you would!" ->
   "Are you all right?" -> the Black Sacrament answer -> "Contract?" -> his story (frame viewed). `TIF__0001F339` runs and, **once the
   box is closed** (the conversation pauses the world, so the host runs the queued fragment on the next tick), sets
   `DB01Objective` 200 and **DB01 stage 20**: `QF_DB01.Fragment_5`. The journal (J) lists "Innocence Lost - running: The mother of
   Aventus Aretino recently died ... [ ] Kill Grelod the Kind" (frame viewed).
3. A harness teleport to `RiftenHonorhallOrphanage`; the test aid `summons test kill = Grelod` kills her (`COMBAT test kill`);
   `pGrelodScript.OnDeath` sets **stage 30** (`Fragment_7`) and starts `HonorhallChildrenCheerScene`: Runa, Samuel, Francois and Hroar
   speak their real voiced lines ("Grelod is dead! Aventus did it!"). Journal: "[x] Kill Grelod the Kind [ ] Tell Aventus Aretino
   that Grelod is dead" (frame viewed).
4. A harness teleport back; talk to Aventus: "Well? Grelod the Kind. Is she... you know?" -> "Grelod the Kind is dead." -> his reward
   line; closing the box sets **stage 200**, `Fragment_9` (the house key and gold go to the inventory: `PAPYRUS item 0001F6D4`),
   the quest stops and the journal lists it under Completed (frame viewed).

**Found and fixed: `Utility.WaitGameTime`.** It was not registered, so the game's `WIDeadBodyCleanupScript.OnDeath` (a Grelod death
starts it) looped on the tolerant zero-return 66,666 times and ended with `[budget exceeded]` (two error lines per run). It is now a
latent native (`papyrus_host.cpp`, next to `AddAchievement`): the script parks for `hours * 3600 / time scale` script seconds (the
host's `mTimeScale`, default 20), at most three game days. Tests (`apps/components_tests/skyrimse/testskyrimsedb01.cpp`, 2): **2 of 2
red** against the unregistered native, green after. No setting added: it changes only a native that did nothing useful.

Not built (ranked): `ActorBase.IsUnique` and `Quest.UpdateCurrentInstanceGlobal` are tolerant zeros (seen in the 200 fragment's
scenes; harmless here); the stage 200 reward `ModFavorPoints` and `FormList.AddForm` were not in the tolerant list of this run, so
they either ran or were skipped on a none receiver (not traced); the Black Sacrament scene (`pAventusScript`) and the real kill by
the player (the test aid kills her) are not shown; **DB02 (the sleep abduction and Astrid) is not started.**

Evidence: four runs of `db01.sh` (h, h4, h5, h6; h was before the fix) give 85 lines of the story, dialogue, scene, journal and trace
filters with md5 `d52e36acdea428dc08e3a02bf0ca804c` each (timestamps, frames, polls cut); `budget exceeded` 2 before the fix, 0 in the
three runs after. Morrowind: `tools/worldobjects_check` 110 objects OK against `integ2`'s build; fixed Balmora frame
`1c5d47ad0b5dc32d` for both. Frames: `I:\Projects\OpenEden-work\ragdoll5\db\keep\` (aventus_talk, journal_stage20, journal_stage30,
aventus_reward_box, journal_completed). Decisions for toddhow: the wait's time scale is the host's `mTimeScale` (20), not the game's
live value; the test aid `summons test kill` stands in for the player killing Grelod (her death is the pack's death report, the same
path a real kill takes).
