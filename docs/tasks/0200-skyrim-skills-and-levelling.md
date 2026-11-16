# Task: Skyrim skills, levelling and perks for the player (task 0200)

Status: BUILT (Windows only, local commits on branch `skills` from `3885a4091b`, not pushed); see the stage log at the end.
toddhow asked for as much Skyrim work as possible overnight (2026-11-16).
Design doc section: `docs/design/skyrim-se-implementation-plan.md` (M-S5 combat, M-S6 UI), task 0179 (perks, `PerkEngine`),
task 0155 (the stand-in player), task 0187 and task 0194 (story events: LEVL could not be raised), decision 0015 (UI).
Goal: the player gains experience in the skills it uses, the skills raise the character level, a level up is spent on a
vital and gives a perk point, a perk point buys a perk whose effects feed the combat's `PerkEngine`, an RmlUi window shows it
all, the story event LEVL is raised on each level, and everything is saved.
Inherited code touched: none (all of it is the pack's, beyond `apps/openmw` data files for the window).
Constraints: behind the new pack setting `[extension.skyrimse] player progression` (off by default, declared in
`extension.json`). With it off every number is the stand-in's, byte for byte as before. No existing test is edited.

## 1. Survey (read at `3885a4091b`; data = the five Skyrim SE masters, aggregates)

- **The stand-in player.** `PlayerStandIn` (`speech.hpp`): level 1, every skill 15, set by the pack settings `player level`,
  `player speech`, `player gold`; `SkyrimWorldView::playerFighter()` (`world_view.cpp`) gives combat a profile with
  `mSkills.fill(15)`, 100 health, magicka and stamina; the dialogue runtime holds a copy built in `ensureDialogue`
  (`DialogueRuntime::setPlayer`); barter reads `SkyrimWorldView::standInPlayerSpeech()` (`startBarter` in
  `skyrimse_game_pack.cpp`).
- **AVIF (skills).** `Skyrim.esm` has 20 AVIF with skill data: the 18 skills (form ids 0x44C to 0x45D in the order of actor
  values 6 to 23; the editor ids are `AVOneHanded` ... `AVMarksman` (Archery) ... `AVMysticism` (Illusion) ...
  `AVEnchanting`) and two rate mods. Layout, VERIFIED on the data: `CNAM` (4 bytes, category 1 combat, 2 magic, 3 stealth),
  `AVSK` (16 bytes, four floats: use mult, use offset, improve mult, improve offset; One-Handed 6.3, 0, 2, 0; Sneak
  11.25, 0, 0.5, 120; Lockpicking 45, 10, 0.25, 300), then the perk tree, per node `PNAM` (the first rank's PERK; 0 for the
  root), `FNAM`, `XNAM`/`YNAM` (grid column and row), `HNAM`/`VNAM` (floats: the node's place in the sky), `SNAM` (the
  skill), `CNAM` x n (the node indices it leads to), `INAM` (its own index). 219 nodes, 7 to 16 a skill. The other 129 AVIF
  are plain actor values.
- **GMSTs** (VERIFIED values): `fSkillUseCurve` 1.95, `fXPLevelUpBase` 75, `fXPLevelUpMult` 25, `fLevelUpCarryWeightMod` 5,
  `iAVDSkillsLevelUp` 8 (not used), `iXPRewardKillOpponent*` (not used: no kill reward yet).
- **MGEF DATA +12** is the associated skill (VERIFIED: values 18 to 22 for the five schools, -1 for 397 of 650 effects).
- **Perks** (`perks.hpp`): `PerkRecord::mNext` (NNAM) chains the ranks, `mPrerequisites` (CTDAs before the first PRKE) are
  what gates learning: `GetBaseActorValue(skill) >= n` (fn 277) and `HasPerk(previous)` (fn 448), both answered by
  `PerkConditionWorld` over a participant's `mValues` and `mPerks`.
- **Where a skill is used today.** `SkyrimCombat::applyHit` (`combat.cpp`; melee, and bow shots through the same
  function), `computeBlock` for a block, `SkyrimCombat::tryCast` and `tickConcentrations` (`combat_magic.cpp`) for spells,
  `BarterService::buy` / `sell` (`barter_service.cpp`) for trade.
- **Story events.** `PapyrusHost::raiseStoryEvent(const StoryEvent&)` (`papyrus_host.hpp`); an event of type `LEVL` has 9
  quests under it in the five masters (task 0194).

## 2. Design

All pure, in `progression.{hpp,cpp}`; the glue is `progression_service.*`; combat's part is `combat_skills.cpp`.

- `decodeSkill` (AVIF), `SkillTable` (18 records by editor id), `settingsFrom(SettingsTable)`.
- `PlayerProgression`: level, 18 skills with experience, character experience, perk points, vital choices, owned perks
  (first-rank form and ranks owned), `useSkill(slot, amount)`, `chooseLevelUp(vital)`, `buyPerk`, a canonical versioned
  save blob. RECALLED-VERIFY formulas (named, tunable): gain = use mult * amount + use offset; the next level needs improve
  mult * level ^ fSkillUseCurve + improve offset; a skill increase gives character experience equal to the new level; a level
  up needs fXPLevelUpBase + fXPLevelUpMult * level, adds 10 to the chosen vital (stamina also adds fLevelUpCarryWeightMod
  carry weight) and one perk point. The level is NOT raised by sleeping as in Skyrim: the skills window offers it when
  ready (decision for toddhow).
- Amounts a use reports (ours, RECALLED-VERIFY): a weapon hit, the damage dealt after armour and block; Block, the damage
  the block took off; Heavy or Light Armor (any main piece heavy, else light), the damage taken; a spell or a held spell
  step, the magicka paid, to the school of the first effect with an associated skill; a barter trade, the gold moved, to
  Speech. Not built: lockpicking, sneak, pickpocket, smithing, alchemy, enchanting (the crafting task may call
  `ProgressionService::skillUsed`), speech challenges.
- Perk purchase: the next rank of a perk is bought when a point is left and the next rank record's prerequisites hold (the
  skill, the previous rank). The player's `mPerks` for combat are the highest rank of each owned perk; `HasPerk` answers true
  for every rank up to the owned one.
- `SkyrimCombat::setSkillUseHook` (one report hook per skill use) and `setPlayerProgression`: the fighter profile of the
  player takes the progression's skills, level, vital maximums (a gain fills the current stamina and magicka, and the
  engine's health through a zero damage report) and perks, re-read when its revision moves; armour is recomputed.
- Speech and bribe checks and barter read the player's real level and skills (`SkyrimWorldView::setProgressionPlayer`
  updates the running dialogue runtime).
- Story event LEVL: `ProgressionService` takes the levels reached and calls the pack's hook, which raises
  `StoryEvent{ "LEVL", value1 = level }` through the Papyrus host (needs `papyrus = true`).
- Save chunk `progression` v1 (a length and the blob). A restore before the records are read is held and applied at build.
- Script namespace `skyrimse.progression` 1.0 (see `progression_service.hpp`) and the window `skills_windows.lua`
  (player script `skills_player.lua`, key K; global `skills_global.lua` runs the Mutate calls). Perk names are the editor ids
  split into words (the PERK FULL string tables are not read: decision for toddhow).
- Accessor for the Papyrus host: `ProgressionService::actorValue(index, out)` (skills 6 to 23; level through
  `progression()->level()`).

## 3. Stages and acceptance

1. Pure module and tests (red first). 2. Data (AVIF into the combat index, MGEF skill). 3. Combat hooks and sync. 4. Service,
chunk, bindings, setting. 5. Window. 6. LEVL. 7. In game: a skill rising in a fight, a level up, buying a perk with real
clicks, save and load, three equal runs, setting off equals the parent, Morrowind unchanged.

## Stage log

Windows only, local commits on branch `skills` from `3885a4091b`, not pushed. Same hand wrote tests and code; no fresh reviewer.

- **Tests.** `testskyrimseprogression.cpp` (pure module, 24 tests): written first against a stub, **21 of 23 red** (the 2 that
  passed test absence), all green after; `reset` added later with its test. `testskyrimsecombatskills.cpp` (7) and
  `testskyrimseprogressionservice.cpp` (8) were written AFTER the code (not run red; the perk entry point test failed
  once on a test mistake and was fixed). Full `components-tests` without the Slang, complement and property groups: 11,196 pass.
  The script bindings (`declare`) and the Lua window have no unit test (no bindings host or headless UI test written): they are
  checked in game only.
- **Data (aggregates, Skyrim.esm).** 20 AVIF with AVSK (the 18 skills and two rate mods); layouts in section 1; MGEF DATA +12:
  18 to 22 for 76, 74, 183, 67 and 153 effects (the five schools), -1 for 397. In game: 180 perk nodes of the five masters.
- **In game (Bannered Mare, player against Hulda, `progression test amount` multiplies every use; sounds off in the final runs).**
  Skill up from fighting: One-Handed 15 -> 31 over ten hits, damage 7.525 -> 7.98 (the player's real skill is used; the stand-in
  would keep 7.525). Real clicks through `OPENEDEN_RMLUI_INPUT_SCRIPT`: K opens the window, Health +10 (level 2, story event LEVL
  raised, health maximum 110), Armsman rank 1 bought (a second click: refused, no point). The next hits show `COMBAT perk attack
  damage (entry point 35): 4.72 -> 5.664`. Frames: `skills/keep/final_009.png` (level up offered), `final_012.png` (Level 2,
  Armsman 1/5, "perk bought"), `final_023.png`.
- **Save and load.** Two processes (save in one, `--load-savegame` in the next): the second logs `PROGRESSION ready: level 2` and
  `SKSTATE level 2 xp 276 points 0 one-handed 31 perks 000babe4x1` before any hit, and the combat reads level 2, stamina 110, 1 perk.
- **Three runs.** One-sided fight (the player swings at Hulda, no hostile), progression on: three traces of 153 lines, byte
  identical (the earlier two-sided fights vary from run to run: an existing nondeterminism of two sides swinging, see
  task 0184, and one run reached One-Handed 33 instead of 31 for that reason).
- **Setting off equals the parent.** Same one-sided scenario, progression off: 130 lines, identical in two runs and identical to
  a build of `3885a4091b` (diff empty). Morrowind: `tools/worldobjects_check` 110 lines OK against the `combat4` build, and the
  fixed Balmora frame hash `1c5d47ad0b5dc32d` equal for the `combat4` build, the parent build, this build and this build
  with the neutral combat settings on.
- **Not run:** the Linux gate and GCC `-Werror`, clang-format 14 (not installed: wrapped by hand to 120 columns), clang-tidy,
  `openmw-tests`, the Lua integration tests, a fresh review, barter or speech in game (hooks are unit-tested only for the
  combat side; the barter `mTraded` hook and `setProgressionPlayer` have no test), the first runs of the day had no `[Sound]
  master volume = 0.0` (the final ones did).

## Limits and decisions for toddhow

1. **Health maximum.** The pack raises the player's health maximum in the combat profile and reports the new maximum and health
   to the engine (a zero damage report), but the bridge only clamps and writes `healthAfter`; the engine's own maximum
   (`subjectState().healthMax`) stays what the engine says. Not changed (engine side).
2. **Level up without sleeping** (Skyrim needs a bed): the window offers it when the experience is there. Say if you want a rest.
3. **Amounts per use and character experience** (damage dealt, damage blocked, damage taken in armour, magicka spent, gold
   traded; a skill increase gives its new level; level up at 75 + 25 * level) are RECALLED-VERIFY; named in `progression.hpp`.
4. **Perk names** are editor ids split into words (PERK FULL strings are not read); the window is a list ordered by row and
   column, not Skyrim's star map.
5. **Not built:** experience from lockpicking, sneak, pickpocket, smithing, alchemy, enchanting and speech challenges (the
   crafting task may call `ProgressionService::skillUsed(slot, amount)`; `actorValue(index, out)` answers skills 6 to 23
   for the Papyrus host). Both sit on the service object the pack owns: nothing hands it to another module yet (one accessor
   on the pack class would do).
6. **Combat edits** are one hook call per skill use (`reportHitSkills` in `applyHit`, `reportSpellUse` in `tryCast` and
   `tickConcentrations`) and `syncProgression` in `combatUpdate`; no new virtual on the combat writer, so `HudWriter` and
   `AiDamageWriter` needed no change. The world is paused while the window is open, so the combat re-reads the progression after
   it closes.
