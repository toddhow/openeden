# Task 0326: Skyrim Civil War, the rest of the Legion join (Fort Hraggstad, the report, the oath)

Branch `civilwar2-0326`, rebased onto local `main` at `872713d15a` (from `9852336cc7`). Follow-up of task 0298. Same hand wrote tests and code; the
tests first where they could be (see section 3); no fresh reviewer. Windows only, not pushed. Everything is behind task 0298's setting
`[extension.skyrimse] civil war` (needs `papyrus`, `quest alias packages`, `actor packages`, `[GUI] rmlui`); no new setting.

## 1. The game's chain after stage 20 (decompiled `QF_CW01`, `CW01Script`, `CW01BanditScript`, `TIF__*`; Skyrim.esm read by a small script)

1. CW01A stage 1 fills `Bandit1..25` from the location's `CWFortMonster` references: Fort Hraggstad lists **13** (13 aliases filled, 12 stay empty).
2. Each bandit alias runs `CW01BanditScript.OnDeath`, which calls `CW01Script.checkLocRefTypeAliveCount`: `Fort.GetLocation().GetRefTypeAliveCount(CWFortMonster)`;
   when it is 0 the quest goes to stage 100 (`Fragment_2`: objective 1 done, objective 100 "Return to Rikke").
3. Rikke's greeting at stage 100 (`CW01RikkeGreetTopic`, INFO `0E7A29`, a Blocking branch like the one of task 0298: "Welcome back, soldier ...").
   Ending the conversation plays its walk-away INFO `0D5EE9` (ENAM flags 0x81, Goodbye and Walk Away; `TIF__000D5EE9` = `SetStage(160)`).
4. Stage 160 (`Fragment_5`): objective 160, the map-table scene stops, `Alias_Tullius.TryToEvaluatePackage()`: Tullius' alias package force-greets
   ("In joining the Legion, you'll be taking an oath ... Are you prepared?", INFO `02D752`), choices "Actually, I'm not sure" and "I'm ready to
   take the Oath." (`MQ102JoinLegionYes`), then `MQ102ALegionOath1..4` (the four lines you repeat) and Oath4's INFO `099D18` ends the conversation
   (`TIF__00099D18` = `SetStage(200)`).
5. Stage 200 (`Fragment_4`): achievement call, objective 160 done, `CW01AOutfitImperial.SetStage(1)` (objective "Get Imperial gear from Beirand"),
   `CW02A.SetStage(10)` (quest "The Jagged Crown" in the journal), `CW.SetStage(11)`, `CW.PlayerTookOath(1)`, the map-table scene, `Stop()`.
   Beirand's own dialogue (not built) sets `CW01AOutfitImperial` stage 10, 11 or 12 by the player's armour skill: `Fragment_1..3` add
   `CWPlayerUniformImperialLight`, `...Medium` or `...Heavy` (leveled lists) to the player.

## 2. What was missing, found by running it

| Gap | Cause | Fix |
| --- | --- | --- |
| the quest reached stage 100 at the FIRST bandit's death | `Location.GetRefTypeAliveCount` (the Papyrus native the script calls; the condition function 592 is another path) was a tolerant no-op returning 0 | new native over `PapyrusGameData::refTypeAliveCount`; `PackPapyrusData` counts the location's LCSR references of the type that are not placed dead and not killed in this session |
| a kill never counted (the condition function 592 of task 0298 counted placed-dead only) | no kill state reached the condition world | `SkyrimConditionWorld::setDeadLookup` (also `DialogueRuntime`, `SkyrimWorldView`), fed by the pack's death sink (`mKilledRefs`) and the saved death ledger |
| Rikke's walk-away INFO `0D5EE9` never ran, so stage 160 was never set | task 0315's walk-away runs only an INFO's TWAT topic; this INFO is in the greeting topic itself with flag 0x80 and no TWAT | `DialogueRuntime::setWalkAwayInfos` (set by `civil war`): at the end the last response's topic is asked for its first passing INFO, which runs its fragments when it carries WalkAway |
| (aid) `summons test kill` named one actor | the bandits share leveled stand-ins | `testKillMatches`: a comma list and a trailing `*`, one kill an update, not cleared after the first kill |

## 3. Tests (none of the existing ones edited)

- `apps/components_tests/skyrimse/testskyrimsecivilwar2.cpp`: `SkyrimFortKills` 4 tests (**1 of 4 failed against the stub**: the other 3 pin absence and the off state),
  `SkyrimTestKillList` 3 tests (**link-red**: the function did not exist), `SkyrimFortNative` 1 test (the host native over a fake `PapyrusGameData`; written with the
  code, **not run red**: the red evidence is the in-game run before the native existed, section 4).
- `apps/components_tests/skyrimse/testskyrimsewalkaway2.cpp`: `SkyrimWalkAwayInfo` 4 tests (**1 of 4 failed against the stub**; helpers copied from 0315's file).

## 4. In game (`tools/skycivilwar_check/check.sh`, section "Task 0326" of its header; Skyrim neutral session, real mouse clicks through
`OPENEDEN_RMLUI_INPUT_SCRIPT`)

The session starts beside Fort Hraggstad (`@Tamriel,-111950,115200,-4950`); the aid starts CW00A at stage 20 (which starts CW01A: 16 aliases filled, 13 bandits).
- Before the native existed (`f3`, the build of task 0298 plus the kill aid): CW01A reached stage 100 at the FIRST of the 13 deaths. With the native
  (`f4`, `h1`): stage 100 only after the 13th death.
- The player is sent to Castle Dour (a Lua teleport, hand-set) and talks to Rikke: her stage 100 greeting (voiced, frame viewed), Goodbye: the walk-away INFO sets
  stage 160 (`f10`: without `setWalkAwayInfos` the conversation closed and nothing happened; `f11`: stage 160), Tullius force-greets ("... Are you prepared to make that
  commitment?"), "I'm ready to take the Oath.", the four oath lines (the repeat-after-me lines, frames viewed), Goodbye: stage 200, `QF_CW01.Fragment_4`: CW02A (journal
  "The Jagged Crown"), CW stage 11, the armour quest (journal "Get Outfitted: [ ] Get Imperial gear from Beirand"), Rikke's next forced greeting ("Welcome to the Legion,
  auxiliary. Listen up."), the journal (J): Quests (2), Completed (1).
- The Legion armour: the armour quest's stage 11 is **hand-set** (`story test quest = ...,0C349B:11@55`); the game's own `Fragment_2` then adds the leveled list's pick
  (item `000C3499`) to the player (`PAPYRUS item 000C3499 x1 from 00000000 to 00000014`); no frame of the item itself (no inventory window opened).
- **Three runs (after the rebase) give identical traces**: 175 lines (`NTALK`, `PAPYRUS quest`/`death`/`item`, `FORCEGREET`, `SCENE ... begins`, `COMBAT test kill`),
  md5 `7780c4877eb789eb5e111052c6845b8d`; three runs before the rebase gave identical traces among themselves too (md5 `339c8594...`, 176 lines).
- Morrowind unchanged against `I:/Projects/OpenEden-work/integ2/build`: `tools/worldobjects_check` 110 objects OK, the Balmora frame `1c5d47ad0b5dc32d` for both.
- `components-tests` filter `Skyrim*:*Papyrus*:*Summon*:*Combat*` 4,146 pass, `openmw-tests` filter `*Neutral*:*Skyrim*:*Extension*` 1,160 pass; `tools/testprofile/coverage.sh` OK.

## 5. Decisions and limits for toddhow

1. **The 13 bandits are killed by the test aid**, one an update (`summons test kill = lvlbandit*@45`), not in a fight (the bandits did engage the player on sight in these
   runs, but a real clear of the fort was not tried). The aid's new list syntax changes nothing for a single name.
2. A killed placement counts as dead for `GetRefTypeAliveCount` for the session (the set is not saved; after a load the saved death ledger answers).
   The condition-world answer of task 0298 (function 592) and the Papyrus native share the same lookup.
3. `setWalkAwayInfos` is on only with `civil war`; the Companions (task 0315) are unchanged. The rule: with no TWAT, the first INFO of the last response's topic that
   passes runs its fragments if its flags carry Walk Away (0x80). RECALLED-VERIFY: the engine's real walk-away rule is not read, only this INFO's data.
4. Hand-set: the teleport to Castle Dour (the player does not walk from the fort), the armour quest stage (Beirand's dialogue is not built), the quest start (CW00A stage 20).
5. The quest the game calls `CW02A` here is form `0002D75C` whose fragment script is named `QF_MQ103A_0002D75C` (the data's own naming); its stage 10 ran its fragment.

## 6. Not built / not run

- Beirand's armour dialogue (stage 10 to 12 by the player's armour skill), CW02A's own gameplay (the Jagged Crown), the faction rank and ally/enemy (`faction.setally`
  and `setenemy` are still tolerant no-ops), a real fight at the fort, the 12 unfilled bandit aliases (the data lists 13), a save and load in the middle.
- Not run: the Linux gate and GCC `-Werror` (the hand check of the traps only), clang-format 14, clang-tidy, the Lua tests, a fresh review, `SkyrimFortNative` red.
- No existing test edited.
