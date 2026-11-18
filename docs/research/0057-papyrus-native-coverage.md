# Research note 0057: Papyrus native coverage (Skyrim SE), the overlap with Fallout 4 and Fallout New Vegas

Task 0321. Status: measured on 2026-11-18 on this machine's installs (Skyrim Special Edition with its five masters, a Fallout 4 install that holds only the Creation Kit's base class files, Fallout New Vegas). Aggregates, names and counts only: no script text and no game text is in this note or in the table file.

## 1. What was measured and how

**Tool.** `apps/components_tests/skyrimse/testskyrimsenativecoverage.cpp` (gated: `OPENEDEN_SKYRIMSE_DATA`, optional `OPENEDEN_FO4_DATA` and `OPENEDEN_FNV_DATA`; `OPENEDEN_NATIVE_COVERAGE_OUT` writes the rows to a file). It loads every script of `Skyrim - Misc.bsa`, `Update.bsa`, `Dawnguard.bsa`, `HearthFires.bsa` and `Dragonborn.bsa` (later archives override earlier ones) into the Papyrus runtime and asks it for every call instruction that resolves to a native (`PapyrusRuntime::nativeCallSites`, and the new `nativeCallers`, which splits the same count by the calling script). The status of a native comes from the code: `PapyrusHost::registeredNatives()` lists what the host registered (its own `reg` calls, the library's pure natives and the common natives of this task); a handler registered through the new `regNoop` helper is a deliberate no-op. A native with no registration is "missing" (a call returns the zero of its type and is counted once as tolerant).

**What "implemented" means.** A handler is registered. It does not mean every call finds data: with no world hooks set, many handlers still answer zero. Section 6 shows the run-time view.

**Call site, not call.** The counts are static call instructions in the scripts, not how often a native runs. They rank natives by how many places in the game's own scripts depend on them.

**Categories** (`world read`, `world mutation`, `pure`, `cosmetic`, `needs an engine feature`) are a heuristic on the function name plus a short hand list of natives that need something the neutral world does not have yet (menus, physics, the animation graph, weather, equipment). They guide the plan; they are not measured.

**Main callers** are script families (a quest fragment `qf_<quest>` or a script-name prefix: `dlc` is everything named `dlc1...`/`dlc2...`), top three by calls.

**The full table** (all 504 natives, before and after status, category, callers) is `docs/research/0057-papyrus-native-coverage-table.tsv`.

## 2. Skyrim coverage, before and after

Call sites are the call instructions that the runtime resolves to a native: 63298 sites, 504 distinct natives, 14026 scripts of the five masters (Skyrim, Update, Dawnguard, HearthFires, Dragonborn).

| | implemented | no-op on purpose | registered total | missing |
|---|---|---|---|---|
| before this task | 52819 (83.44%) | 2332 (3.68%) | 55151 (87.13%) | 8147 (12.87%) |
| after this task | 53433 (84.41%) | 4155 (6.56%) | 57588 (90.98%) | 5710 (9.02%) |

Distinct natives registered: before 184, after 205 (of 504 called).

The two rows were measured on the base `bbd3bb965a`. After the rebase onto `c14f282175` (task 0320 registered more natives) the same survey gives 53,604 answered and 4,155 no-op sites: 57,759 of 63,298 (91.25 percent). The full table is from the first measurement.

### Missing call sites by category (after this task)

| category | natives | call sites | share of all sites |
|---|---|---|---|
| world mutation | 157 | 2703 | 4.27% |
| needs an engine feature | 29 | 2307 | 3.64% |
| world read | 103 | 680 | 1.07% |
| pure | 9 | 15 | 0.02% |
| cosmetic | 1 | 5 | 0.01% |

### The 40 natives still missing with the most call sites

| # | native | sites | scripts | category | main callers (script family: sites) |
|---|---|---|---|---|---|
| 1 | objectreference.blockactivation | 326 | 123 | needs an engine feature | dlc:58,playerbookshelfcontainerscript:37,dunharmugstahlleverscript:36 |
| 2 | objectreference.delete | 242 | 144 | needs an engine feature | dlc:53,byohdivertprefabsscript:18,playerpotionrackcontainerscript:18 |
| 3 | actor.setghost | 219 | 97 | needs an engine feature | dlc:77,c01:12,qf_dialoguemarkarthintroworld:10 |
| 4 | objectreference.lock | 210 | 120 | needs an engine feature | dlc:29,qf_mq201:10,tif:9 |
| 5 | quest.completeallobjectives | 171 | 157 | world mutation | dlc:53,byoh:4,qf_da16:3 |
| 6 | faction.setenemy | 131 | 70 | world mutation | dlc:43,qf_dunstonehillbarrowqst:7,mq3:6 |
| 7 | actorbase.setessential | 129 | 66 | world mutation | dlc:37,qf_db02:14,qf_dbdestroy:6 |
| 8 | actor.equipitem | 125 | 66 | needs an engine feature | dlc:22,qf_mq101:13,mannequinactivatorscript:11 |
| 9 | quest.failallobjectives | 117 | 104 | world mutation | dlc:12,qf_tgrbe:3,qf_tgrdu:3 |
| 10 | actor.showbartermenu | 109 | 109 | needs an engine feature | dlc:63,tif:44,byoh:2 |
| 11 | quest.setobjectivefailed | 94 | 47 | world mutation | dlc:11,qf_ms02:8,qf_cwsiege:7 |
| 12 | objectreference.translateto | 83 | 23 | needs an engine feature | dlc:16,dunmiddenhandsculpturescript:12,cwartilleryaimingmenuscript:11 |
| 13 | objectreference.setmotiontype | 81 | 44 | needs an engine feature | dlc:8,dunlabyrinthianmazestaffaltarscript:4,dunraldbtharpuzzlegearblockerscript:4 |
| 14 | form.unregisterforanimationevent | 77 | 35 | world mutation | dlc:26,cwcatapultscript:6,da0:5 |
| 15 | actor.setoutfit | 75 | 41 | needs an engine feature | dlc:13,qf_ms02escapescene01:6,byohrelationshipadoptionchildscript:5 |
| 16 | leveleditem.addform | 73 | 23 | world mutation | dlc:22,byohrelationshipadoptablescript:19,rpdefault:19 |
| 17 | objectreference.damageobject | 70 | 31 | needs an engine feature | qf_cwfortsiegefort:12,qf_cwsiege:11,dlc:10 |
| 18 | quest.updatecurrentinstanceglobal | 69 | 36 | world mutation | dlc:18,favorjarlsmakefriendsscript:9,ffr:9 |
| 19 | faction.setally | 66 | 37 | world mutation | dlc:30,qf_mgritual03:7,tif:4 |
| 20 | formlist.addform | 66 | 17 | world mutation | dlc:43,byohhousebuildingscript:11,rpdefault:4 |
| 21 | actorbase.setinvulnerable | 64 | 32 | world mutation | dlc:24,c00:6,da1:4 |
| 22 | actor.unequipitem | 60 | 37 | needs an engine feature | dlc:19,mqkilldragonscript:8,pf_thievesguildtrainingarche:8 |
| 23 | objectreference.reset | 59 | 36 | world mutation | dlc:15,qf_dunmarkarthwizardsquarters:7,cwfortsiegescript:5 |
| 24 | game.showtrainingmenu | 57 | 57 | needs an engine feature | tif:48,dlc:9 |
| 25 | objectreference.gettriggerobjectcount | 57 | 17 | needs an engine feature | trapfireplate:9,dlc:7,pressurereleaseplate:6 |
| 26 | actor.movetopackagelocation | 55 | 20 | needs an engine feature | cwfortsiegescript:21,dunriftenjailloadcheck:6,qf_dbdestroy:6 |
| 27 | game.incrementskill | 53 | 25 | world mutation | tif:22,qf_bardscollegedrum:6,qf_bardscollegeflute:6 |
| 28 | objectreference.placeactoratme | 51 | 19 | needs an engine feature | dlc:15,qf_cwmission04:8,dunpoisoldiersraidonstart:4 |
| 29 | objectreference.ignorefriendlyhits | 50 | 13 | world mutation | qf_mq302:26,dlc:8,qf_cwfinale:4 |
| 30 | game.enablefasttravel | 49 | 25 | needs an engine feature | dlc:25,da0:5,qf_db09:3 |
| 31 | objectreference.setlocklevel | 49 | 23 | needs an engine feature | dlc:11,qf_db09:6,qf_mq201:4 |
| 32 | objectreference.createdetectionevent | 46 | 41 | needs an engine feature | tif:21,dlc:11,db0:2 |
| 33 | actor.setcriticalstage | 45 | 22 | needs an engine feature | dlc:25,bwcnightingaleghostscript:2,da1:2 |
| 34 | actor.setrace | 45 | 17 | needs an engine feature | playervampirequestscript:20,dlc:14,c03:3 |
| 35 | actor.getcurrentpackage | 44 | 11 | world read | dlc:28,riftenbrynjolftriggerscript:4,c00:3 |
| 36 | actor.openinventory | 44 | 44 | needs an engine feature | tif:22,dlc:14,byoh:5 |
| 37 | faction.setplayerenemy | 43 | 16 | world mutation | qf_dbrecurring:20,dlc:8,qf_db09:3 |
| 38 | actor.setdoingfavor | 41 | 41 | world mutation | tif:26,dlc:13,byoh:1 |
| 39 | weather.setactive | 41 | 16 | needs an engine feature | qf_cwsiege:20,qf_mq106:3,fxalduinscript:2 |
| 40 | actor.getdialoguetarget | 40 | 40 | needs an engine feature | tif:36,dlc:4 |

### Natives this task registered

| native | sites | kind |
|---|---|---|
| sound.play | 378 | no-op on purpose |
| effectshader.play | 280 | no-op on purpose |
| visualeffect.play | 214 | no-op on purpose |
| effectshader.stop | 145 | no-op on purpose |
| actor.setalpha | 143 | no-op on purpose |
| visualeffect.stop | 132 | no-op on purpose |
| game.shakecamera | 121 | no-op on purpose |
| formlist.getat | 108 | answered |
| actor.setsubgraphfloatvariable | 100 | no-op on purpose |
| objectreference.setanimationvariablefloat | 99 | no-op on purpose |
| objectreference.getanglez | 97 | answered |
| actor.getlevel | 89 | answered |
| actor.playsubgraphanimation | 77 | no-op on purpose |
| formlist.getsize | 74 | answered |
| game.shakecontroller | 72 | no-op on purpose |
| objectreference.getanglex | 65 | answered |
| objectreference.getangley | 64 | answered |
| sound.stopinstance | 62 | no-op on purpose |
| actor.getrace | 44 | answered |
| actorbase.getrace | 38 | answered |
| formlist.hasform | 35 | answered |

Non-vanilla scripts (information only): the Data folder holds 1,204 loose and Creation Club scripts; they call 31 natives that no vanilla script calls (45 call sites; `LeveledItem.Revert`, `ColorForm.GetColor`, `Weather.GetOutgoingWeather`, `UI.*`: the library and SKSE classes of mods). They are not part of the numbers above.

## 3. Fallout 4 overlap

**This machine's Fallout 4 does not have the game's scripts.** `Fallout 4/Data` holds 29 loose base class files (`Actor.pex`, `ObjectReference.pex`, ... the Creation Kit's) and no archive, so FO4's own call sites cannot be counted here (task 0093 counted them on an earlier install: 2,399 scripts). What can be compared is what the classes DECLARE as native, and the Skyrim call sites of the same class.function.

896 natives are declared by the Fallout 4 class files that are installed; 407 of them are declared by the same class under the same name in Skyrim SE; those shared natives carry 33634 of Skyrim's 63298 call sites (53.14%).

Of the 407 shared natives: 102 implemented, 23 no-op, 282 missing (88 of the missing ones have no Skyrim call site at all). Fallout 4-only declared natives: 489.

Shared natives still missing, ranked by Skyrim call sites:

| # | native | Skyrim sites | category |
|---|---|---|---|
| 1 | objectreference.blockactivation | 326 | needs an engine feature |
| 2 | objectreference.delete | 242 | needs an engine feature |
| 3 | actor.setghost | 219 | needs an engine feature |
| 4 | objectreference.lock | 210 | needs an engine feature |
| 5 | actorbase.setessential | 129 | world mutation |
| 6 | actor.equipitem | 125 | needs an engine feature |
| 7 | actor.showbartermenu | 109 | needs an engine feature |
| 8 | objectreference.translateto | 83 | needs an engine feature |
| 9 | objectreference.setmotiontype | 81 | needs an engine feature |
| 10 | actor.setoutfit | 75 | needs an engine feature |
| 11 | objectreference.damageobject | 70 | needs an engine feature |
| 12 | actorbase.setinvulnerable | 64 | world mutation |
| 13 | actor.unequipitem | 60 | needs an engine feature |
| 14 | objectreference.reset | 59 | world mutation |
| 15 | game.showtrainingmenu | 57 | needs an engine feature |
| 16 | objectreference.gettriggerobjectcount | 57 | needs an engine feature |
| 17 | actor.movetopackagelocation | 55 | needs an engine feature |
| 18 | game.incrementskill | 53 | world mutation |
| 19 | objectreference.placeactoratme | 51 | needs an engine feature |
| 20 | objectreference.ignorefriendlyhits | 50 | world mutation |
| 21 | objectreference.setlocklevel | 49 | needs an engine feature |
| 22 | objectreference.createdetectionevent | 46 | needs an engine feature |
| 23 | actor.setcriticalstage | 45 | needs an engine feature |
| 24 | actor.setrace | 45 | needs an engine feature |
| 25 | actor.getcurrentpackage | 44 | world read |
| 26 | actor.openinventory | 44 | needs an engine feature |
| 27 | actor.setdoingfavor | 41 | world mutation |
| 28 | actor.getdialoguetarget | 40 | needs an engine feature |
| 29 | actor.getequippedspell | 40 | world read |
| 30 | actor.isequipped | 39 | world read |
| 31 | actor.setcrimefaction | 39 | world mutation |
| 32 | actor.setalert | 38 | world mutation |
| 33 | actor.equipspell | 37 | needs an engine feature |
| 34 | actor.resurrect | 34 | needs an engine feature |
| 35 | objectreference.applyhavokimpulse | 34 | needs an engine feature |
| 36 | objectreference.setdestroyed | 34 | world mutation |
| 37 | game.querystat | 33 | world mutation |
| 38 | location.ischild | 33 | world read |
| 39 | actor.removeperk | 32 | world mutation |
| 40 | objectreference.setactorowner | 30 | world mutation |

Reading: more than half of the Skyrim call sites (53 percent) go to natives that Fallout 4 declares under the same class and name, so work on a shared native pays twice. Most of the remaining shared natives need engine features (equipment, locks, activation blocking, motion types), which are the core's work, not a per-game one.

## 4. Fallout New Vegas overlap (ObScript commands)

The derived New Vegas command table has 383 commands used 67474 times in the masters.

- commands sharing something with Skyrim (a condition function name or a Papyrus native function name): **141** (36975 uses);
- condition function with the same index and name in xEdit's Skyrim table: 59; same name at another index: 14; the same opcode number is a different Skyrim function: 58;
- of the Skyrim-condition matches, evaluated by `conditions.*` today: **55**;
- commands whose name equals the function name of a Papyrus native that Skyrim scripts call: 81, of which a native of that function name is registered: 42.

Most used shared commands:

| command | NV uses | Skyrim condition | evaluated | Papyrus native | registered |
|---|---|---|---|---|---|
| disable | 3321 | none | no | objectreference.disable | yes |
| enable | 3318 | none | no | objectreference.enable | yes |
| setobjectivedisplayed | 3230 | none | no | quest.setobjectivedisplayed | yes |
| setobjectivecompleted | 2400 | none | no | quest.setobjectivecompleted | yes |
| additem | 1719 | none | no | objectreference.additem | yes |
| getdead | 1509 | same index and name (46) | yes | - | no |
| removeitem | 1458 | none | no | objectreference.removeitem | yes |
| moveto | 1453 | none | no | objectreference.moveto | yes |
| activate | 1296 | none | no | objectreference.activate | yes |
| startcombat | 931 | none | no | actor.startcombat | yes |
| getitemcount | 838 | same index and name (47) | yes | objectreference.getitemcount | yes |
| getdisabled | 827 | same index and name (35) | yes | - | no |
| getstage | 777 | same index and name (58) | yes | - | no |
| removefromfaction | 683 | none | no | actor.removefromfaction | yes |
| say | 623 | none | no | objectreference.say | yes |
| getlinkedref | 529 | none | no | objectreference.getlinkedref | yes |
| setdestroyed | 517 | none | no | objectreference.setdestroyed | no |
| resurrect | 493 | none | no | actor.resurrect | no |
| setenemy | 477 | none | no | faction.setenemy | no |
| placeatme | 468 | none | no | objectreference.placeatme | yes |
| getrandompercent | 459 | same index and name (77) | yes | - | no |
| getstagedone | 365 | same index and name (59) | yes | - | no |
| getdistance | 360 | same index and name (1) | yes | objectreference.getdistance | yes |
| setghost | 357 | none | no | actor.setghost | no |
| kill | 346 | none | no | actor.kill | yes |
| removeperk | 345 | none | no | actor.removeperk | no |
| playidle | 318 | none | no | actor.playidle | yes |
| getincell | 317 | same index and name (67) | yes | - | no |
| isincombat | 312 | same index and name (289) | yes | actor.isincombat | yes |
| lock | 302 | none | no | objectreference.lock | no |

Condition functions that exist in Skyrim's table by name and are not evaluated yet, ranked by New Vegas use (candidates for `conditions.*`, each with its own world query):

getopenstate (188), getpos (82), getdayofweek (37), getcurrentaiprocedure (32), getdestroyed (27), istalking (25), getdestructionstage (21), menumode (17), getheadingangle (12), getpermanentactorvalue (7), iscurrentfurnitureref (7), hasbeeneaten (6), getlocklevel (5), ispc1stperson (2), getlineofsight (1), isactor (1), iscurrentfurnitureobj (1), isessential (1)

What the overlap means: New Vegas has no Papyrus. Its command set shares NAMES with Skyrim's condition functions (the condition index numbers descend from the same table: 59 commands have the same index and name) and with Papyrus natives (the same verb on a record, written as `Ref.Disable` in ObScript and `ObjectReference.Disable` in Papyrus). A shared function evaluated by `conditions.*` or answered by a native serves a future New Vegas pack with the same query, if its world supplies it. The same-index count also confirms the 0116 survey's finding that the condition numbering is shared up to about 330.

## 5. Ranked plan

Ranked by call sites served, with the home of each native: **common** (the library `components/script_papyrus/`, answered once for every game pack through a game-neutral interface) or **pack** (Skyrim only). Anything where the home is unclear is common and listed as a decision.

| rank | batch | natives (sites) | home | needs |
|---|---|---|---|---|
| done | this task | Actor.GetLevel, Actor.GetRace, ActorBase.GetRace, FormList.GetSize/GetAt/HasForm, ObjectReference.GetAngleX/Y/Z (614 sites); no-ops: Sound.Play/StopInstance, EffectShader.Play/Stop, VisualEffect.Play/Stop, Game.ShakeCamera/ShakeController, Actor.SetAlpha, Actor.SetSubGraphFloatVariable, PlaySubGraphAnimation, ObjectReference.SetAnimationVariableFloat (1,823 sites) | common | `PapyrusFormQueries` (new, optional) |
| 1 | quest objectives | Quest.CompleteAllObjectives (171), FailAllObjectives (117), SetObjectiveFailed (94), UpdateCurrentInstanceGlobal (69): 451 sites, FO4 shares the class | common (interface: objective count of a quest, flags) | the quest record's objective list in the pack's quest index; the host already keeps displayed/completed flags |
| 2 | factions | Faction.SetEnemy (131), SetAlly (66), SetPlayerEnemy (43) ; ActorBase.SetEssential (129), SetInvulnerable (64): 433 sites | common natives, pack state | an override table the combat factions rule reads (task 0197 `combatfactions`), essential/invulnerable flags read by `actor_mechanics` |
| 3 | form lists and leveled lists | FormList.AddForm (66), LeveledItem.AddForm (73): 139 sites | common | a mutable overlay of the pack's FLST/LVLI index |
| 4 | actor reads | Actor.GetCurrentPackage (44), GetEquippedSpell (40), IsEquipped (39), Location.IsChild (33), Actor.GetDialogueTarget (40), Cell.IsInterior (6): 202 sites | common (queries) | the package brain's current package, the inventory model's equipment, the location index |
| 5 | skills and stats | Game.IncrementSkill (53), Game.QueryStat (33), Actor.RemovePerk (32) | pack (progression service) | `ProgressionService::raiseSkill` exists (task 0200) |
| 6 | activation, lock, delete | ObjectReference.BlockActivation (326), Delete (242), Lock (210), SetLockLevel (49), Reset (59): 886 sites | common natives over core services | the engine: a flag on a neutral object that its activate action honours, lock state (task 0203 has the lock model in the pack), deletion of a placement (the NWLD record has "deleted") |
| 7 | actor state | Actor.SetGhost (219), EquipItem (125), UnequipItem (60), SetOutfit (75), SetRace (45), Resurrect (34), SetAlert (38): 596 sites | common natives over core services | core: actor flags, equipment on the neutral actor, outfit swap, race swap (a rebuild), resurrect (the death ledger) |
| 8 | menus and dialogue | Actor.ShowBarterMenu (109), Game.ShowTrainingMenu (57), Actor.OpenInventory (44) | pack | the services windows exist (tasks 0189, 0219); the natives should open them |
| 9 | physics and motion | ObjectReference.TranslateTo (83), SetMotionType (81), DamageObject (70), GetTriggerObjectCount (57), ApplyHavokImpulse (34) | common natives, core services | a mover for scripted placements (task 0289 has the animation path), triggers (task 0256) |
| 10 | the long tail | 244 natives with fewer than 30 sites each (1,665 sites in all) | by use | rank by the scenario that needs them |

A shared native done in the common library with a query or mutation interface is written once; the pack answers over its own records. The FO4 overlap table in section 3 gives the order for those that matter to both games.

## 6. What this task built, and the interface

**`PapyrusFormQueries`** (`sdk/include/openeden/script/papyrus.hpp`): an OPTIONAL capability of a `PapyrusWorld`, found with `dynamic_cast` like `PapyrusAttachKeyCheck`. It is not a new virtual on `PapyrusWorld` or `PapyrusContract`: a pack built against the earlier header has a smaller `PapyrusWorld` vtable, so a virtual added there could not be called safely (the header comment of `PapyrusAttachKeyCheck` records this). Every function has a default body that refuses. `PapyrusContract::kVersion` stays 1.2 (the existing pin tests pass untouched). The functions: `formIdOf` (a value's form, an attached script's form), `actorLevel`, `raceOf`, `formListEntries`, `referenceRotation` (radians).

**`ScriptPapyrus::FormNatives`** (`components/script_papyrus/papyrusformnatives.*`): registers the nine query natives (only when the world has the capability) and twelve cosmetic no-ops. A native the game pack registered first stays the pack's (reported as a failure, like `PureNatives`). Meanings are RECALLED-VERIFY (Creation Kit wiki): `FormList.HasForm` looks only at the list's own members; `GetAt` out of range is None; angles are degrees; `Sound.Play` returns the instance id 0.

**Skyrim pack:** `PapyrusHost::Impl` also derives from `PapyrusFormQueries`; three new defaulted virtuals on the pack-internal `PapyrusGameData` (`actorLevel`, `actorRace`, `formListEntries`) are answered by `PackPapyrusData` over `SkyrimWorldView` (`actorLevelOf`, `actorRaceOf`, the existing `formListEntries`); angles come from the host's script-set angles, else the data. `PapyrusHost::registeredNatives()` is the registration list the survey reads.

**Developer switch:** `OPENEDEN_PAPYRUS_NO_COMMON_NATIVES=1` skips the common registration, so one executable gives the "before" run of the in-game comparison.

**In game** (`I:\Projects\OpenEden-work\ragdoll5\natives3\run.sh`, the start-up scripts of a Tamriel start at Helgen, 20,000 frames, 3 runs each, byte-identical within each group): tolerant natives at the end of the run before 15, after 14 (`actor.getlevel` gone, its one call is answered); the start-up scenario touches few natives, so the dramatic numbers are the static ones of section 2. The static call-site coverage by registration rose from 87.13 percent to 90.98 percent (answered 83.44 to 84.41, no-ops 3.68 to 6.56).

## 7. Decisions for toddhow

1. **The new optional interface** `PapyrusFormQueries` (additive, no version bump; chosen over a new virtual on `PapyrusWorld` for the layout reason above). If you want it in the version number, a minor bump of `PapyrusContract` needs the two pin tests (`testpapyruscontract.cpp`, `testpapyrusnatives.cpp`) edited.
2. **Cosmetic no-ops live in the common library** (a sound, a shader, a particle effect, a camera shake, an animation-graph variable have nothing to drive in a neutral world). `Actor.SetAlpha` and the three animation-graph natives are in the list on the same reasoning; say if any should stay tolerant so a later renderer feature notices it.
3. **`Actor.GetLevel` of the player is the stand-in level** (`player level` setting, default 1), not the progression service's level (task 0200); the host has no path to it yet. A level-gated script on the player sees the stand-in.
4. **`Actor.GetRace` of the player is refused** (the stand-in player has no race record in the pack): the call stays at the tolerant zero.
5. **The Fallout 4 comparison is by declaration only**, because this install lacks the game's scripts. If you want call-site counts, the Fallout 4 game archives (`Fallout4 - Misc.ba2`) need to be installed.
6. **No existing test was edited.**
