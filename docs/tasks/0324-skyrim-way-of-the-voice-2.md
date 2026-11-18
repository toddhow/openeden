# Task 0324: the Way of the Voice, second slice (Einarth's push trial, Arngeir's talk, Lydia as a follower)

Branch `mq105b-0324` from `9852336cc7` (task 0318), rebased onto local `main`. Windows only, not pushed. Behind the existing
`[extension.skyrimse] main quest whiterun` (off by default) except the general fixes in section 3. Same hand wrote the tests and the
code; the new tests were written first but only the compile failure against the old code is the red evidence (section 5); no fresh reviewer.

## 1. Survey (houseCARL on Skyrim.esm; scripts decompiled from `Skyrim - Misc.bsa`, work folder `I:/Projects/OpenEden-work/mq324/psc`)

- **The push trial.** `MQ105ShoutTargetScript` (an alias script) sets stage 25 in `OnMagicEffectApply` when the PLAYER's `PushEffect` hits Einarth,
  Wulfgar or Borri. `QF_MQ105.Fragment_16` (stage 25): if stage 30 is done, `SetStage(50)` (objective 25 "Speak to Arngeir"). `Fragment_9`
  (stage 5): `GreybeardFaction.SetAlly(PlayerFaction)`. Stage 20 (`Fragment_96`) starts `EinarthTeachScene` (its phases set stages 56, 58, 59,
  60, 75, 80); stage 70 `TeachWord(WordRo)`, 76 `UnlockWord(WordRo)`, 80 starts `PushTrialScene`, 255 and 265 teach and unlock Dah.
- **Arngeir after the trial** is the Blocking branch `MQ105ArngeirIntroTopic` (0x2EC64): its INFO tests `GetIsAliasRef(Arngeir)` and
  `GetStage(MQ105) == 50`, so "I am Master Arngeir ... why have you come here?" is the post-trial greeting (stage 55 on the first choice).
  The answer "I'm answering your summons" before the trial is a different INFO of the same quest (stage 30).
- **Lydia.** The housecarl NPC `HousecarlWhiterun` (0xA2C8E, placed as 0xA2C94 in Breezehome) is in `PotentialFollowerFaction` and
  `PlayerHousecarlFaction` (rank 0), and her RELA with the Player NPC_ (0x7) is Ally. Her package `HousecarlWhiterunGreetPlayer` is a
  ForceGreet (topic subtype HELO) with the condition `GetTalkedToPC == 0`. The recruit line is `DialogueFavorGenericFollowBranchTopic`
  (0xB0EE6, "Follow me. I need your help."), whose INFOs test `GetRelationshipRank(player) >= 1`, `PotentialFollowerFaction`,
  `CurrentFollowerFaction == 0` and others.

## 2. What the runs showed was missing (found by running; in game, real clicks, the five masters, player level 10)

1. **Spell events did not reach the scripts** with `main quest whiterun` (task 0292's sink exists only with `college`), so the shout hit Einarth
   (frame: his bar and name) and no stage 25 followed.
2. **The Greybeards fought back** (Einarth hit the player after the push): `Faction.SetAlly` is a tolerant no-op and the combat AI has no
   rule for a blow by the player on an ally.
3. **Lydia said only "Yes?":** (a) `GetTalkedToPC` was only true inside a conversation, so her greeting package never ended and she greeted
   again six seconds after a Goodbye; (b) the recruit INFOs read `GetRelationshipRank` with a TARGET OF 0, which the pack never mapped to the
   player, so the rank was always 0 and every one of the 15 INFOs failed (`dialogue debug topic` showed it).

## 3. What was built

1. **Spell events with `main quest whiterun`** (`skyrimse_game_pack.cpp`): the sink of task 0292 is installed for `college` OR `main quest whiterun`
   (one sink; the college block no longer owns it).
2. **`Faction.SetAlly` / `SetEnemy`** (new natives, `papyrus_host.cpp`) write `FollowerState::setFactionRelation` (symmetric, last wins, NOT saved:
   a load keeps the Greybeards neutral until the quest's stage runs again; `data()` and the chunk are unchanged). `SkyrimCombat::setAllyOfPlayerHook`
   (new): a blow of the PLAYER on a victim whose factions the hook calls allied does not provoke it (`aiProvoke`, `aiNoteDamage`); installed with
   `main quest whiterun`; empty elsewhere. After the change Einarth takes the push and stands still (no `hit on the player` lines).
3. **Quest greetings and the fragment flush with `main quest whiterun`** (the mechanism of tasks 0304 and 0310, set once, not a second one): a quest's
   nameless branch start can be the greeting. Nothing in this task needed it (Arngeir's greeting is a Blocking branch), see gap 4.
4. **GetTalkedToPC outside a conversation** (`SkyrimConditionWorld::setTalkedLookup`, `SkyrimWorldView::mTalkedTo`, only with `quest alias packages`):
   a reference whose conversation began is "talked to" for the package world, so a ForceGreet package ends. Lydia greets once.
5. **GetRelationshipRank with a target of 0 is the player** (`relationKnown`, `relationshipRank`; general: decision 1).
6. Developer aids: `[extension.skyrimse] dialogue debug topic = <hex>` (every INFO of that topic says why it fails and every condition list is printed;
   greeting candidates and Blocking starts are reported) with `conditions::setConditionDebugAll`. Harness: `WATCH`, `ACTIVATE2`, `ACTIVATE3` in
   `tools/skywhiterunmq_check`.

## 4. In game (harness `tools/skywhiterunmq_check`, work folder `I:/Projects/OpenEden-work/mq324`, runs in `runs/<name>`)

- **Push trial** (`story test quest = 02610C:90,0242BA:5`, the player teleported into High Hrothgar, real clicks for Arngeir's first talk, the shout menu
  Equip click and the real `=` key, the player turned to Einarth by an `abs` step): the shout hits Einarth, `OnMagicEffectApply` reaches
  `MQ105ShoutTargetScript`, **MQ105 stage 25, then stage 50 (`Fragment_22`)**; Einarth staggers (`1hm_staggerbackmedium`) and does not attack. Frame
  `runs/e2/user/screenshots/screenshot002.png` (taken before the fix: Einarth's bar and tooltip with the Greybeards around him).
- **Arngeir at stage 50** (real clicks): "I am Master Arngeir ... why have you come here?"; "I'm answering your summons, Master." sets **stage 55**; "What is
  my destiny?" and the next answer lead to "You have shown that you are Dragonborn ... That remains to be seen." (frames `runs/e6/.../screenshot005.png`,
  `runs/e7/.../screenshot006.png`); Goodbye closes it and nothing else is set (gap 1).
- **Einarth's teaching** needs stage 20, which no reachable trigger set (gap 1); with the aid `0242BA:20@105` the game's own `Fragment_96` runs and
  **`EinarthTeachScene` begins** (phase 0 and 3 run, 1 and 2 are skipped by their conditions, phase 4: Arngeir's voiced "Dragonborn. It is you. Welcome to
  High Hrothgar.") and then **stalls in phase 4** for the rest of the run; **Ro is NOT taught and Fus Ro Dah is NOT shouted** (stage 70 never runs).
- **Lydia** (`story test quest = 02610C:160@25`, the Thane stage that moves her into Dragonsreach): her greeting "Yes?" now offers **"Follow me. I need
  your help."** (frame `runs/l8/.../screenshot000.png`); the click answers "Lead the way." (the game's `dialoguefavorge_000b0ee9` line, frame
  `runs/l9/.../screenshot000.png`) and the log says `follower recruited: placement 666772` (the game's own scripts and the alias, no aid). With the player
  stood on her level and then moved 600 units, **she walked to him and stands beside him** (`MQ watch` distance 220 to 130; frame `runs/l12/.../screenshot001.png`).
- Repeatability and the Morrowind comparison: section 6.

## 5. Tests

- New: `testskyrimsemq324.cpp` (`SkyrimMq324Relations` 4, `SkyrimMq324Talked` 2, `SkyrimMq324Relationship` 1) and two tests appended to
  `testskyrimsecombatai.cpp` (`CombatAi.aVictimAlliedToThePlayerDoesNotFightBackWhenTheHookSaysSo` and `aVictimThatIsNotTheHooksAllyStillFightsBack`).
  Written before the code; **against the old code they do not compile** (they name functions that did not exist), which is the red evidence; no run against
  stubs. One of the two appended combat tests failed at its first green run because the fighter's profile is read at its first update (a test fault, fixed with
  `run(5)`). The glue (the sink, the hook, the natives, the debug aid) has no unit test: the game runs are its evidence.
- `components-tests` filter `SkyrimMq324*:CombatAi*` 38 pass. Existing tests edited: **none**.

## 6. Checks

After the rebase onto `872713d15a` (a fresh build, `skyrimse`, `components-tests`, `openmw`, `openmw-tests`; `openmw-tests` was built, not run):

- `components-tests` filter `*Skyrim*:*Papyrus*:*Scene*:*Dragon*:*Shout*:*Quest*:*Dialog*:*Condition*:CombatAi*:*Follower*` 4,242 tests, 4,111 pass, the rest skip (data-gated), 0 fail.
- **Push scenario, three runs** (`runs/f1..f3`: the MQ105 stage lines, the spell and hit lines, the talk turns and the harness steps, 20 lines, poll numbers masked): identical (md5 `8c4813635731`),
  with no `hit on` the player after the push. Before the rebase the same three runs gave `19dafe67bcd9` (a different line set: the build before the rebase logged fewer lines).
- **Lydia scenario, three runs** (`runs/g1..g3`: talk turns, the recruit line, the forced greeting, the `MQ watch` distances): identical (md5 `9af879bc471b`), recruited once, final distance 130.
- **Morrowind unchanged** (against `I:/Projects/OpenEden-work/integ2/build`, not a build of the exact parent): `tools/worldobjects_check` 110 objects OK (111 lines each), the fixed Balmora frame
  (`--start Balmora --random-seed 7`, frame 600) is `1c5d47ad0b5dc32d` for both.
- Frames kept in `I:/Projects/OpenEden-work/mq324/keep` (e2, e6, e7, l8, l9, l12); the other run folders were deleted.

## 7. Gaps, ranked

1. **Stage 20 and the Einarth scene.** Nothing in this host sets MQ105 stage 20 after Arngeir's talk (the TIF fragments of his branch set 30, 50 and 55 only);
   it was set by the aid. The scene then stalls in phase 4 after Arngeir's line (cause not found: a speech completion that does not arrive, or a phase that
   waits for a package of Einarth). Without it: no `TeachWord(Ro)`, no `PushTrialScene` (stages 80 to 90), no Borri, no Horn, no Dah; **Fus Ro Dah is not shouted**.
2. **Lydia on another floor does not reach the player** (she stands at the messenger marker below the throne room; her mover logs `blocked foot` and does not
   climb; the path between the floors was not examined). She follows correctly on her own level. The axe `AddItem` was not looked at.
3. The relation `Faction.SetAlly` writes is not saved.
4. Quest greetings are on with `main quest whiterun` although this task did not need them.

## 8. Decisions and edits for toddhow

1. **`GetRelationshipRank` target 0 = the player is general** (not behind a setting): lines of actors with a RELA to the Player (allies, friends, lovers)
   now see their real rank. Say if it should be behind `main quest whiterun`.
2. A blow of the player on an allied faction never provokes it (an accident), only with `main quest whiterun`; repeated hits are not counted (OURS).
3. `GetTalkedToPC` for packages is permanent once a conversation began (only with `quest alias packages`); the game resets it over time (not modelled).
4. Existing tests edited: none. New setting: `dialogue debug topic` (a developer aid, declared in `extension.json`, not in the test profile).

## 9. Stage log

- Edits outside new files: `combat.hpp`, `combat_ai_run.cpp`, `conditions.{hpp,cpp}`, `dialogue.{hpp,cpp}`, `dialogue_runtime.cpp`, `follower_state.{hpp,cpp}`, `papyrus_host.cpp`,
  `skyrimse_game_pack.cpp`, `world_view.{hpp,cpp}`, `extension.json`, the harness, `apps/components_tests/CMakeLists.txt`.
- **Not run:** the Linux gate, GCC, clang-format 14 (formatted by hand), clang-tidy, the Lua tests, `openmw-tests`, the whole `components-tests`, a fresh review, saves in
  the middle of the chain.
- Warnings for the next hand: never query INFO records with a `*parent` filter in houseCARL (the call hung for 30 minutes); a click on the dialogue box's Goodbye
  needs the button's real position (it moves with the number of choices); input script frames are not the Lua update count.
