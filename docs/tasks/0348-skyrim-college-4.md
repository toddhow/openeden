# Task 0348: Skyrim, the College of Winterhold questline, fourth slice (follow-up of tasks 0292, 0304 and 0327)

Same hand wrote tests and code. Windows only. Not pushed. Local branch `college4-0348` (from `69f549168d`). Everything new sits
behind the existing `[extension.skyrimse] college` (off by default; it needs `papyrus`, `quest alias packages`, `actor packages`,
`journal`, `[Game] neutral combat`, `neutral inventory` as in 0292, 0304 and 0327). No new setting. The runs use two test aids
(`story test quest = 01F251:30@20`, as 0327, and `summons` + `summons test magicka = 1000`, a stand-in magicka for the player
because the ward costs 34 a second and the stand-in's 100 magicka ends it in 3 s) and the hand-written harness steps named in
section 3.

## 1. Survey (decompiled with the housecarl tool from the pex files of `Skyrim - Misc.bsa`; records read with the work folder's Python)

* `MG01WardScene` (SCEN 0x9152B, 6 phases, 6 actions). Phase 0 "Over there, please" (done in 0327). Phase 1 "Now, cast the ward
  spell, and keep it up" and phase 2 "Hold still..." each complete when `GetActorValue(WardPower)` of the player is above 0 (actor
  value index 63: the CTDA `fn 14`, parameter 0x3f; RECALLED-VERIFY that 63 is WardPower; this scene is its only use here). Phase 3
  has ONE package action, `MG01Stage50TolfdirCastAtPlayer` (PACK 0x9152D, template UseMagic 0x504F5), and completes when
  `PlayerHit == 1 or TimerInit == 2` (quest script `MG01QuestScript`). Phase 4 ("You have to keep your ward up", start condition
  `PlayerHit == 1`) and phase 5 ("Excellent work!", start conditions `PlayerHit == 0 and TimerInit == 2`) are the two outcomes.
  The scene's phase 2 completion fragment (`Fragment_2`) gives Tolfdir the spell `MG01TeachFirebolt` (SPEL 0x9152C, cost 23, one
  effect `MG01FireDamageFFAimed` magnitude 15, **flags 0x10221804: Detrimental and NOT Hostile**); its phase 5 completion
  fragment (`Fragment_0`) starts `MG01WardPostScene` (0x9152F), whose last phase sets MG01 **stage 200**.
* The package's VMAD has an **OnBegin** fragment (`PF_MG01Stage50TolfdirCastAtPl_0009152D.Fragment_0`: `TimerInit = 1`); the pack
  parsed only OnEnd fragments of packages (task 0274). Its data: the first PTDA is type 1 (Object ID) = the spell, the second
  type 0 (specific reference) = the player (0x14).
* Scripts that decide the outcome: `MG01TolfdirScript` on the Tolfdir alias (`OnSpellCast(MG01TeachFirebolt)`: wait 5 s, and when
  `TimerInit != 2` set `PlayerHit = 1`), `MG01PlayerScript` on the **Player alias** (`OnWardHit(caster, spell, status)`: when
  `TimerInit == 1`, the caster is the Tolfdir alias and the spell is `MG01TolfdirSpell` (= 0x9152C), set `TimerInit = 2`).
  MG01's Player alias is a **unique actor alias (ALUA) of the Player NPC_ 0x00000007**, not a specific reference to 0x14.
* Stage 200 (`QF_MG01.Fragment_18`): objective 50 done, Tolfdir's favour, `MG02.Start()`, `MGCollegeLectures.Start()`, `Stop()`.
  MG02 "Under Saarthal" (QUST 0x1F252): stage 0 (`Fragment_0`) moves Arniel to Saarthal and calls `SetStage(10)`; stage 10
  (`Fragment_2`) shows the objective and the Saarthal map marker; stage 20 (`Fragment_4`: the lecture scene, unlocks the Saarthal
  door) is set by Tolfdir's stage 10 branch topic `MG02TolfdirStage10BranchTopic` ("I'm ready. Let's go."); the amulet scene,
  the vision and the Psijic monk (stages 25 to 60) are in the dungeon.

## 2. What was missing and is built (all behind `college`)

Found by running the scenario against the 0327 code (`tools/skycollege_check`, the work folder's scripts); each cause was shown by
the traces before it was fixed.

1. **WardPower of the player as a condition answer.** `SkyrimConditionWorld::setPlayerWardSource` and
   `SkyrimWorldView::setPlayerWardSource`: GetActorValue index 63 of the player reads the combat's ward power (`FighterView`
   `mWardPower`), set on the scene's quest world at every evaluation. Without it phase 1 never completed (baseline run).
2. **UseMagic packages.** `PackageActivity::UseMagic` (a UseMagic procedure and nothing that classifies it otherwise);
   `PackageBrain::setUseMagicHook` asks once, when the package begins, for a cast of the package's first Object ID target;
   `SkyrimCombat::castSpellAt(handle, spell)` queues it and `runScriptedCasts` (in `updateMagic`) faces the player and calls the
   ordinary `beginCast`: the cast clip, magicka, the projectile, the effects are the combat's own (log `USEMAGIC` and
   `COMBAT scripted cast`).
3. **A package's OnBegin fragment runs** (`PackageRecord::mBeginScript/mBeginFunction`, `PackageBrain::setPackageBeginHook`, the
   package's QNAM `mQuest` for `GetOwningQuest()` when no quest alias gives the package: a scene's package has none), through the
   same host call as the OnEnd fragment.
4. **OnWardHit.** `SkyrimCombat::SpellEvent::Kind::WardHit` is emitted when a ward takes magnitude of another caster's spell;
   `PapyrusHost::onWardHit` sends `OnWardHit(caster, spell, 0)` to the scripts that hold the target (log
   `PAPYRUS ward of ... held spell ..., OnWardHit sent to N script(s)`).
5. **A ward holds a Detrimental spell that is not flagged Hostile** (`wardable` in `applyEffect`: Hostile or Detrimental, another
   caster, not a constant effect). MG01FireDamageFFAimed is Detrimental only, so before this the bolt went through the ward
   (health 100 to 85). 49 of 950 effect records are Detrimental and not Hostile; nearly all are abilities, diseases and traps
   (caster = target or constant, which the rule excludes). RECALLED-VERIFY the game's own rule.
6. **The Player alias of a quest holds the player.** `PapyrusHost::setPlayerAliasFill`: a unique-actor alias whose actor is the
   Player NPC_ (0x00000007) answers 0x14 (`aliasReferenceOf`), so the scripts on it hear the player's events (before, the 18
   scripts held by 0x14 did not include `MG01PlayerScript`: the alias had no reference).
7. **A scene phase whose start conditions fail is skipped** (task 0308's `setSkipFailedPhases`, now also with `college`; the pack's
   `enableSkipFailedPhases`). Before, phase 3 completed (`TimerInit == 2`) and the scene waited for phase 4's start condition
   (`PlayerHit == 1`) for ever; the CONDDEBUG trace showed the list true and the next list false every update.

## 3. Hand-written or stood in (named)

* `story test quest = 01F251:30@20`: MG01 stage 30 (as 0327; 0304 reached it by the real chain).
* `summons = true` and `summons test magicka = 1000`: the player's maximum magicka (the ward costs 34 a second).
* `combat test player spell = WardLesser` and the real attack control held by an input script from frame 25,500 to 36,000: the
  player casts the ward (the lesson's own ward spell comes from a dialogue line that gives it; the aid makes the attack key cast
  `WardLesser` instead).
* The 0327 harness steps (`cell`, `back`, `bring`, `abs`) and two new ones in the same script: `bring` takes the cell name
  `exterior` (the start cell) and the step list ends with `back` (the player at Saarthal's door, 92400, 91450, -12700) and
  `activate` (the player talks to Tolfdir; the real Space key finds him too, not tried). Tolfdir is TELEPORTED from the Hall of the
  Elements to Saarthal (92200, 91600): the pack does not simulate actors in unloaded cells (0327 section 3).
* The clicks are real mouse events on the conversation box; their frame numbers are tuned to this machine's timing.

## 4. Not reached (ranked)

1. **MG02 beyond stage 10** (section 6 has what happened at Saarthal's door).
2. The amulet scene (stage 40), the vision with the Psijic monk, the Saarthal dungeon, the lecture inside (stages 25 to 60).
3. The gate does not open in the world (`TempGateRef.SetOpen` is a tolerant no-op: 0327), Mirabelle and the tour start from a
   teleport (0327).
4. Tolfdir's cast has no visual (the combat spawns the projectile; the neutral renderer draws the Firebolt model only when the
   effect model exists: not checked in a frame) and the ward has none either (0186).
5. `OnWardHit`'s `aiStatus` is always 0; 1 (broke) and 2 (unharmed) are not told.

## 5. Tests

`apps/components_tests/skyrimse/testskyrimsecollege4.cpp`, `SkyrimCollege4` (15) and `ScriptedCast` (6): UseMagic is an activity of
its own; a UseMagic package asks for one cast of its spell, not again, none without the procedure, none without hooks; the begin
fragment is told once and not without one; the ward source answers only for the player and only for index 63 and a non-number is
0; a unique-actor alias of the Player NPC_ holds 0x14 only when told; OnWardHit reaches the scripts on the player's reference
with caster and spell (a script that declares `spell AkSpell`), not those of other references; an actor asked to cast launches
at the player, an unknown spell or actor is refused; a ward that takes a bolt gives one WardHit with caster, target and spell
and no damage; no ward gives no event and damage; a Detrimental-only spell is held by the ward.
**Red against the code before (the API as stubs): 9 of the first 18 failed** (the 9 that passed pin refusals and absence);
the Detrimental test was written after the first run and failed against the old rule (1 of 1); the Player-alias tests and the
pack's package parsing were written with the code (compile-red only: no stub run).
Green: the Skyrim, Papyrus, Dialogue, Quest, Scene, Package, Travel, Combat, Magic, Spell and Ward filters of `components-tests`
(4,843 tests, 0 failed, the rest skipped); no existing test edited.

## 6. Runs (work folder `I:\Projects\OpenEden-work\combat3\s348\run`)

See the CLAUDE.md row for the numbers. The scenario is `env_f.sh` / `in_f.txt` of that folder: 26,500 frames to the ward lesson
(as 0327), the ward held from frame 25,500, and in the longer variants the player and Tolfdir at Saarthal.
