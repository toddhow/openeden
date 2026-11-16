# Task 0228: text tags in dialogue, the player's chosen name, and race powers

Status: BUILT; Windows only, local commits on branch `polish` (from `main` at `47af8bf7f0`), not pushed. Not run: the Linux gate, clang-format 14, clang-tidy, the Lua integration tests, a fresh review (see "Not run").
Design doc section: `docs/design/skyrim-se-implementation-plan.md` (dialogue, character creation, magic), tasks 0141/0143 (conversation, subtitles), 0217 (journal alias names), 0219 (services), 0225 (character creation), 0186 (magic runtime).
Goal: three loose ends of tonight's merges: dialogue shows `<Global=RoomCost>` unexpanded; the name the player chooses is kept only in the pack chunk; the chosen race gives only resistances and weapon speed, no power.
Inherited code touched: none under `components/`. Engine: `apps/openmw/mwworld/worldimp.{hpp,cpp}` (one driver, a few lines).
Constraints: no new setting. Tags follow the existing dialogue; the name and the powers are behind `[extension.skyrimse] character creation` (which needs `menus` and `player body`); Morrowind unchanged (the engine driver is made only with `[Game] neutral world` and finds nothing without the interface).
Out of scope: `<Alias.Race=>`, `<Global.Day=>`/`MonthWord`/`Year` tags, spell descriptions (`<mag>`, `<dur>`), area effects for ordinary spells, a power selection window.

## 1. Survey: tag kinds in the five masters

A Python walk over the `*_english` string tables of the five masters (`Skyrim - Interface.bsa`; counts of tag occurrences, no text kept). Responses (INFO NAM1, `.ILSTRINGS`) carry almost no tags (six stage directions such as `<Laughter>`); the tags are in `.STRINGS` (topic names, prompts RNAM, names) and `.DLSTRINGS` (quest log text, books, descriptions).

| Tag | Occurrences (all tables) | Filled now |
| --- | --- | --- |
| `<Alias=X>` (also `alias=`) | 801 + 10 | yes (the journal's alias name, shared) |
| `<Alias.ShortName=X>` | 166 | yes (same name) |
| `<Global=X>` (also `global=`) | 271 + 4 | yes (GLOB value by editor id, whole numbers without decimals) |
| `<BribeCost>` / `bribecost` | 46 + 9 | yes (our rule: 20 gold per speaker level, `speech.hpp`) |
| `<BaseName>` | 12 | yes (the speaker's NPC name) |
| `<CrimeGold>` | 6 | yes, through a hook (0 unless the pack gives a source; the crime service is not wired to it: decision) |
| `<Alias.Pronoun>`, `.PronounObj`, `.PronounPos`, `.PronounPosObj`, `.PronounRef`, `.PronounCap` | 20, 35, 6, 13, 4, 5 | yes (he/she/they by the alias NPC's sex) |
| `<Alias.Race=X>` | 4 | no (left out) |
| `<Global.Day/MonthWord/Year=GameDaysPassed>` | 12 | no (left out) |
| `<mag>`, `<dur>`, numbers (`<25>`) | 460, 312, about 400 | no: spell descriptions, not dialogue |
| HTML-like (`<p align=>`, `<font>`, `<br>`, `<b>`) | about 3,000 | no: books and notes, not dialogue |
| stage directions (`<Laughter>`, `<humming>`) | 6 | removed (left out) |

## 2. Design

* **One expander for both.** `journal::expandTags` gained a `TagHooks` overload (`journal.hpp/.cpp`); the old two overloads delegate to the same implementation with their old results (`<Alias=X>` without a resolver still shows `X`, any other tag `...`). The conversation passes `unknown = ""`, so a tag nothing fills is left out.
* **Alias names are shared**: the lambda that `papyrusAccess` built for the journal (task 0217) became `SkyrimWorldView::aliasDisplayName`, used by both; `aliasFemale` is new (the alias NPC's sex, -1 unknown).
* `DialogueRuntime::expandText` (new, public) fills the response lines (`fillTurn`, which also gives the subtitle of task 0143 and the scene lines), the topic prompts and the topic names; the quest context is the topic's quest. `DialogueTagSources` (alias name, alias sex, crime gold) are set by the world view. Globals are looked up by editor id (`DialogueIndex::globalByEditorId`, a scan, case-insensitive); `<BribeCost>` uses the new `SkyrimConditionWorld::speakerLevel()`.
* **The name.** New additive SDK header `sdk/include/openeden/game/player_identity.hpp` (DRAFT 0.1): `PlayerIdentitySource::playerIdentity(state)` with a revision and a name, found by `dynamic_cast` on the registered source (the pattern of `PlayerBodySource`). `SkyrimWorldView::setPlayerName` (bumps the revision only on a change) is called by the character service on finish and when a saved choice is read (so a save made at once has it). The engine side is `mwworld/neutralplayername.*`: `NeutralPlayerNameSync` (pure: acts on a new revision, never for 0 or an empty name) and `NeutralPlayerNameDriver` (finds the source, calls `MechanicsManager::setPlayerName`, the engine's own call that the Morrowind chargen uses); `World::update` runs it every frame, also while paused (the creation window pauses the world), only with `[Game] neutral world`.
* **Powers.** `SkyrimCombat::racePowers(spells)` lists the SPLO spells whose SPIT type is 2 (power) or 3 (lesser power) with a support class; `castPlayerPower(id)` queues a cast that goes through `beginCast` (the cast clip's timing, then `fireCast`); a power with an area (`SpellEffect::mArea`, feet, 21.33 units a foot) now reaches every other living fighter within it (`applyPowerEffects`); effects with conditions the pack cannot evaluate are taken as met for powers (OURS). The character service lists them (`power_count`, `power_name`, `power_support`, `power_ready`, `use_power`; namespace `skyrimse.character` 1.1), allows one use a game day per power (`kPowerCooldownSeconds` = 86400 game seconds, OURS: the records give no timing) and saves the day in its OWN chunk `character_powers` v1 (the `character` chunk stays version 1: an existing test pins it). The player script `character_player.lua` uses the first ready power on the key `P` and sends the request to the global script (only a global script may call Mutate functions).

## 3. Powers of the ten playable races (the gated survey, `SkyrimPowersRealData`)

All ten races have one power; every SPIT type is 2 or 3 (VERIFIED: the type is at SPIT +8 and the survey found all ten through it). Support classes are what the magic runtime does:

| Race | Power | MGEF archetype / value | Class |
| --- | --- | --- | --- |
| Nord | Battle Cry | 7 Demoralize (4 conditions), 33 Stagger | **works** (area 60 ft: the others flee for 30 s and stagger) |
| Argonian | Histskin | 34 Peak on value 27 (health rate) | status only (no regeneration rule reads it) |
| Breton | Dragonskin | 34 Peak on value 83 | status only |
| High Elf | Highborn | 34 Peak on value 28 (magicka rate) | status only |
| Redguard | Adrenaline Rush | 34 Peak on value 29 (stamina rate) | status only |
| Dark Elf | Ancestor's Wrath | 35 (a cloak) | none |
| Imperial | Voice of the Emperor | 6 (Calm) | none |
| Khajiit | Night Eye | 1 | none |
| Orc | Berserker Rage | 1 | none |
| Wood Elf | Command Animal | 1 | none |

So one of ten works, four are cast and kept as a status for their duration with no effect, five are cast with nothing happening (they are listed as `none` and `use_power` refuses them).

## 4. Tests

* `testskyrimsetexttags.cpp` (11): the pure expander (globals, aliases, pronouns, base name, bribe cost, crime gold, case, strays, journal overloads unchanged) and the runtime on a generated plugin (greeting with `<BaseName>`/`<BribeCost>`, topic name and response with `<Global=RoomCost>`, alias tags through the sources, none without sources).
* `testskyrimsepowersname.cpp` (17): the name hook (finish, restore), the namespace 1.1 and its functions, the daily cooldown, refusals, the separate chunk (`character` stays version 1), the damaged chunk, `SkyrimWorldView` identity revisions, `racePowers` classes, queueing, the cast through the magic runtime, the area reaching only those within it.
* `testskyrimsepowersreal.cpp` (1, gated): the table above.
* `openmw-tests` `testneutralplayername.cpp` (2): the engine's decision.
* **Red evidence: honest account.** The tag code and the name/power code were written BEFORE their tests (the pure expander was written first), so no stub-red run exists; instead mutation checks (section 6; 5 tests failed under three mutations). No existing test was edited or deleted.

## 5. Stage log (2026-11-16)

* **Hulda's rent line.** `tools/services_check` (task 0219's scenario): the conversation shows "I'd like to rent a room. (10 gold)" where the frame of the earlier build had `<Global=RoomCost>` (the task 0219 log); the rent window follows as before.
* **The name.** `tools/namepower_check/check.sh` (a copy of task 0225's check): the creation is finished with the name Ria; the log says `CHARACTER finished: Ria`, `Neutral player name: 'Ria'`, and the Lua `types.NPC.record(self).name` reads `Player` before and `Ria` after. The save written afterwards says `for character 'Ria'`, and the pause menu's Load Game list shows the line "Ria Level 1 WhiterunBanneredMare ..." (frame viewed).
* **The power.** The real key `P` (synthetic SDL event through the input script) queues the Battle Cry, the cast begins, `cast cost 0.000 magicka`, six NPCs of the Mare are demoralized for 30 s and staggered; the power is `ready=false` afterwards. Frame viewed (`np/r3/.../screenshot002.png`): a neighbour at the far door is leaning back in the stagger clip; the player has no cast clip yet. Three runs of the whole scenario (creation, name, save, Battle Cry, Load Game list) give a byte-identical 20-line trace (md5 prefix `9e4156ec6583`; screenshots are not byte-compared).

## 6. Mutation evidence

Three one-line mutations of the new code were built together: the pronoun sex swapped (failed `SkyrimTextTagsPure.pronounsFollowTheAliasSex` and `SkyrimTextTagsRuntime.aChosenTopicFillsAliasesThroughTheTagSources`), the cooldown boundary `>=` made `>` (failed `PowerFx.aPowerCanBeUsedOnceAGameDay` and `PowerFx.theDayIsSavedInItsOwnChunkAndTheCharacterChunkIsStillVersionOne`), the area test flipped (failed `PowerCombat.aPowerWithAnAreaReachesTheOthersWithinItAndNotTheCasterOrTheFarOnes`): 5 tests in all. A fourth mutation (the name's no-change check removed) did not apply and was not run. The sources were restored from git and the 28 tests pass again.

## Decisions for toddhow

1. **Four powers are status only and five do nothing**: the regeneration rates and the cloak, calm, night eye, berserk and command animal archetypes have no rule. Which to build first (regeneration rates are the cheapest: four of the ten)?
2. **The Battle Cry also staggers and frightens peaceful NPCs** (every living fighter within 60 ft, no hostility test; the record's own conditions are not evaluated). Alternatives: only hostile ones, or evaluate the 4 conditions.
3. **The power key is `P`, the first ready power**; there is no list or selection window and no shout/power menu (the `shouts` branch may bring one).
4. **The cooldown is one game day** by our rule (86,400 game seconds), saved in its own chunk. A clock set back frees the power.
5. **`<CrimeGold>` shows 0**: the crime service's bounty is not wired to the dialogue (a source function exists, `DialogueTagSources::mCrimeGold`).
6. The player's name is written into the engine record by the engine's own `setPlayerName` (the Morrowind chargen's call). The save, the load list and the HUD read it from there.

## Edits outside the new files

`game_packs/skyrimse/{CMakeLists.txt (journal.cpp joins the world library: the conversation runtime now uses the expander), include/game_skyrimse/{character_service,combat,dialogue,dialogue_runtime,journal,neutral_world,world_view}.hpp, src/{character_service,combat_magic,dialogue,dialogue_runtime,journal,neutral_world,skyrimse_game_pack,world_view}.cpp}`, `apps/openmw/mwworld/{worldimp.hpp,worldimp.cpp}`, `apps/openmw/CMakeLists.txt`, `apps/components_tests/CMakeLists.txt`, `apps/openmw_tests/CMakeLists.txt`, `files/data/scripts/openeden/skyrimse/{character_player,character_global}.lua`, `CLAUDE.md` (changelog row). No setting added, no existing test edited, no `CombatRequestWriter` change.

## Not run

The Linux gate and GCC `-Werror` (the traps of the root guide were checked by hand), clang-format 14 (formatted by hand), clang-tidy, the Lua integration tests, `openmw-tests` as a whole beyond the neutral, extension and Lua groups, a fresh review, other races' powers in game (only the Nord's; the others by the gated survey), a power cast while the player is in third person with a visible clip (the player has no cast clip yet: `cast begin ... token 0`), the cooldown across a real save and load in game (unit tested), `<Alias.Race>` and the date tags.
