# Task 0294: Skyrim Dark Brotherhood, start of the questline (DB01 "Innocence Lost", DB02)

Status: SURVEY ONLY, 2026-11-17. Nothing built in the pack, no tests written, nothing run in game.
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
