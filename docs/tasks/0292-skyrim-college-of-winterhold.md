# Task 0292: Skyrim, the start of the College of Winterhold questline (MG01 "First Lessons")

Milestone M-S6/M-S7 (a questline start through the game's own quests, dialogue and Papyrus, as 0264, 0271 and 0270 did).
Same hand wrote tests and code. Windows only. Not pushed. Everything new sits behind the new pack setting
`[extension.skyrimse] college` (off by default, declared in `extension.json`, README line in the test
profile) and the settings the earlier questline starts already need (`papyrus`, `quest alias packages`,
`actor packages`, `journal`).

## 1. Survey (the data, read with houseCARL on the user's load order and by decompiling the shipped `.pex`)

The quest is **MG01 "First Lessons"** (0x01F251, Skyrim.esm), flags StartGameEnabled | RunOnce, startup stage 0, type
MageGuild. Stages 0, 3, 10, 20, 21, 22, 23, 24, 30, 40, 50, 200. Objectives: 10 Join the College of Winterhold; 20 Cast a
Mage Light spell; 21 Summon a Flame Atronach; 22 Cast a Firebolt spell; 23 Cast a Fear Spell; 24 Cast a Healing Hands
spell; 30 Report to Mirabelle Ervine; 40 Tour the College of Winterhold; 50 Listen to Tolfdir.

The game's own fragments (`QF_MG01_0001F251`, decompiled):

| stage | what the fragment does |
| --- | --- |
| 0 | `Fragment_2`: Onmund, Brelyna, J'zargo to their class markers, Faralda's alias to `NiryaWaitMarker` |
| 10 | objective 10 displayed |
| 20, 21, 22, 23, 24 | objective 10 done; objective 20, 21, 22, 23 or 24 (the spell Nirya asked for) displayed; `MG01Pointer` (the quest marker quest) set to 200 |
| 30 | objectives 20 to 24 done, 30 displayed; the player joins `CollegeFaction` (rank 0); `AddAchievement(12)`; the gate stone is unlocked, `TempGateRef.SetOpen(true)`; `DialogueWinterholdCollegeQuestScript.GateOpen = 1` |
| 40 | objective 30 done, 40 displayed; the tour scene starts (`MG01TourScene`) |
| 50 | objective 40 done, 50 displayed; the tour scene stops; Tolfdir's class |
| 200 | objective 50 done; Tolfdir favour points; `MG02` and `MGCollegeLectures` start; MG01 stops |

Aliases (14): Mirabelle (0x1C1A0), Nirya (0x1C19B), Tolfdir (0x1C19E), Onmund, Brelyna, J'zargo, the gate stone
(`MG01GateStoneRef` 0x51190, alias 6, script `MG01StoneScript`), the classroom doors, `MGFrontGateRef` (alias 9), Ancano,
Faralda (0x1C197; packages: Nirya travel and cast Mage Light 1 to 5, `MG01NiryaForcegreet`, `MG01FaraldaShoutForcegreet`,
`MG01FaraldaBridgeForcegreet`, `MG01FaraldaWaitOutsideCollege`), the Player alias (13, script `MG01PlayerScript`) and
`MG01PlayerCastMarker`.

The scripts that listen for the player's spells (decompiled):

* `MG01StoneScript` (alias 6, the stone): `OnHit(akAggressor, akSource ...)` with `akSource` a spell: Mage Light at
  stage 20, Firebolt at stage 22, Fear at stage 23 set `NiryaSpellSuccess = 1`, play `PlayAnim01` on the stone and
  `EvaluatePackage` Nirya and Faralda.
* `MG01NiryaScript` (alias 1, Nirya): `OnMagicEffectApply(akCaster, akEffect)`: at stage 24 a Healing effect from the
  player sets `NiryaSpellSuccess`.
* `MG01TolfdirScript` (alias 2): `OnSpellCast(akSpell)`; `MG01PlayerScript` (alias 13): `OnWardHit`; both belong to
  Tolfdir's ward lesson (stage 50).
* `MG01TestCastTriggerScript` (a trigger box): at stage 3 the player entering it makes Tolfdir evaluate his package.
* `MG01AtronachTriggerScript` (Flame Atronach, objective 21).

The dialogue (96 DIAL records of the quest; the topics named in the data): Faralda at the bridge `MG01Faralda1WhyAreYouHere`,
`MG01FaraldaEntryBranchIntro` ("May I enter the College?"), `MG01FaraldaHereResponse1` to `3`; Nirya's
`MG01NiryaGreetingBranch`, `MG01NiryaStage10SellSpell` ("Okay, this is for the spell. (Give 30 gold)"),
`MG01NiryaStage10DontKnowSpell`, `MG01NiryaStage20BranchTopic` ("There, I cast the spell."),
`MG01NiryaStage30BranchTopic`; Mirabelle `MG01MirabelleStage30BranchTopic` ("I was told to come see you."),
`MG01MirabelleStage30TakeTour`; Tolfdir's scene branches `MG01TolfdirStage50Scene...` and the ward lesson
(`MG01TolfdirWardResponse1` to `3`, `MG01TolfdirClassroomScene2BranchTopic`).

## 2. What the host lacks (before this task)

Found by reading the host and the scripts:

1. **No spell event reached a script.** `onActorHit` raised `OnHit` with a `None` source (no spell); a spell that ended on
   the world, an effect applied (`OnMagicEffectApply`) and a cast (`OnSpellCast`) reached no script at all, so the stone
   and Nirya's alias could never see the player's spell.
2. `Lock`, `ModFavorPoints` and `GlobalVariable.GetValueInt` are tolerant no-ops (they do not matter for the first
   stages).
3. The rest is for the runs (section 4).

## 3. Built (all behind `[extension.skyrimse] college`, off by default; the earlier draft name `spell script events` was renamed)

1. **Spell events** (`SkyrimCombat::SpellEvent`, `setSpellEventSink`; `PapyrusHost::onSpellHit/onMagicEffectApplied/onSpellCast`): Cast, Hit (on an actor, the player or where a shot ended on the world; the script gets `OnHit` with the spell as `akSource`; a world hit reaches every scripted reference or alias reference within 256 units) and EffectApplied (`OnMagicEffectApply`). This is what `MG01StoneScript` and `MG01NiryaScript` listen for.
2. **fn_630 GetVMScriptVariable** is answered from the live source (`SkyrimConditionWorld::scriptVariable`); the live lookup also accepts `::X_var` as `X`.
3. **Persistent placements' scripts** (`SkyrimWorldView::scriptsOfCell`): a world's persistent placement stands in a square (loadCell already did that) but its scripts and trigger volume were never attached. The Winterhold bridge box `MG05WinterholdTriggerRef` (0x50785) is persistent; the base 0x50783 carries the script.
4. `college` turns on relocation (quests move actors near the player, as `main quest start` does) and box triggers.

## 4. Results (Windows, test game off-screen, real clicks)

* Start `@Tamriel,110000,105500,-8300`; the player is placed on the bridge by a harness step (`abs`, a test aid; walking in from the slope was not possible: the approaches are cliffs). MG01 stage 0 runs, Faralda is relocated to `MG01NiryaWaitMarker`, the bridge box fires `OnTriggerEnter`, `BridgeWarning` becomes 1, Faralda's `MG01FaraldaBridgeForcegreet` package passes (`PKTRACE ... passes`) and the game's own forced greeting opens, voiced, subtitled ("Cross the bridge at your own peril ..."), frame `I:\Projects\OpenEden-work\bow6\mgrun\a1\user\screenshots\screenshot000.png`. A click on Goodbye closes it (the controller A did not).
* Talking to Faralda afterwards (activate by Lua) gives only the generic greeting and the training topic.

## 5. Not reached (ranked gaps)

1. **Faralda's quest branch is never offered.** `MG01FaraldaInitialBranchTopic` (0xAF0DD; INFOs 0xAF0E3 and 0xB8114: GetStage MG01 < 30, IsInDialogueWithPlayer, FaraldaProgress == 0, GetIsAliasRef 11) is never even selected, and "May I enter the College?" (0xAF0D8) needs FaraldaProgress == 1, which only the INFOs of the Initial branch set (TIF__000AF0E5, E6, EC, B810E, B8110). How the game enters that branch is not known (not a HELO topic, not a package of the alias, not a link of the bridge INFO). Nothing after this was run: stages 3 to 50, Nirya's spell sale, the test casts, Mirabelle's tour, Tolfdir.
2. Spell events are unit tested only; no spell was cast in game (`combat test player spell` was never run against the stone).
3. `Lock`, `ModFavorPoints`, `GlobalVariable.GetValueInt` are tolerant no-ops (stage 30).
4. Controller A did not activate the conversation box; mouse clicks did.
5. The harness places the player on the bridge (the approaches are cliffs).

## 6. Tests

New: `SpellEvents` (6, combat), `SkyrimPapyrusSpellEvents` (6, host), `SkyrimScriptVariable` (2), `SkyrimWorldViewScripts` (1). Red runs: spell events 4 of 6 and host 4 of 6 failed against empty functions; script variable 1 of 2 and persistent scripts 1 of 1 failed against a mutated build (the live lookup and the persistent merge disabled). Green: `*Skyrim*`, `*Papyrus*`, `*Dialogue*`, `SpellEvents.*` 3,279 tests and `openmw-tests *Neutral*` 827 pass. No existing test edited.
Not run: Linux gate, clang-format 14, clang-tidy, Lua tests, a fresh review, `tools/worldobjects_check` and the Balmora frame (no reference build was made; the changes are pack-only and behind settings or in pack code Morrowind never loads), three-run determinism.
