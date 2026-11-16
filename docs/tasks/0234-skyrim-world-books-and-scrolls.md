# Task: Skyrim books in the world, notes that start quests, and scrolls (milestone M-S6, follow-up of tasks 0203, 0208, 0186)

Status: BUILT (Windows only, local commit on branch `worldbooks` from `a99561bd2d`, not pushed). Same hand wrote the tests and
the code; the new tests were written first and run red against stubs (counts below); no fresh reviewer.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.17 (items), 4.19 (magic); tasks 0203 (lock picking and
books: "a world book" was listed as not run), 0208 (pickup and theft), 0204 (crime), 0186 (the magic runtime), 0181 and 0187
(Papyrus, the quest journal 0185).
Goal: (1) activating a BOOK lying in the world with the real activate key opens the reading window and does NOT take the book; a
Take button moves it to the inventory through the pickup path (an owned book is theft); a skill book read there raises its skill
once; a note whose script waits for `OnRead` starts its quest. (2) A scroll in the inventory has a Use button; using it casts
its spell through the magic runtime (cast clip, aimed or self delivery, no magicka) and uses the scroll up.
Constraints: books behind the existing pack setting `[extension.skyrimse] lockpicking and books` (so also `inventory`, `[Game]
neutral inventory`, `[GUI] rmlui`); Papyrus events need `papyrus`; scrolls behind `inventory` AND `[Game] neutral combat` (a
scroll is refused, and kept, while the combat rules never ran). No new setting. With the settings off nothing changes.

## 1. Survey (read at `a99561bd2d`; data from the five masters, Python walkers in `I:\Projects\OpenEden-work\bow6\wb`)

**The route already existed for any item.** `MWClass::Neutral::activate` sends `openeden_skyrimse_item_activated` (placement,
base, name) for a placed Item (`apps/openmw/mwclass/neutral.cpp`, `decideNeutralPickup`); `pickup_global.lua` answers it with
`pickup.pickup(placement, base)` (task 0208). A BOOK is `ObjectKind::Item` (`world_view.cpp` base kind table), so today a placed
book is taken at once. No engine change was needed.

**Book scripts.** 1,042 BOOK records in the five masters (task 0203 counted 1,054 with the DLC overrides); **49 carry a VMAD**:
`DefaultOnReadSetQuestStageNotAlias` (Lost Legends, the Legend of Red Eagle, Dawnguard notes: properties `myQuest`, `myStage`;
the `.pex` has `Event OnRead`), `defaultSetStageOnCloseBookNotAlias`, `defaultAddMapMarkerOnCloseBookNotAlias` (the Explorer's
Guide), the quest-specific letters (`MS01EltrysNoteScript`, `WERoad08LetterScript` ... `onread`), `dunTreasMapScript`, and
`defaultsetstageonplayeracquireitem` (skill books of the Dark Brotherhood: acquire, not read). The event is the
`ObjectReference` event **OnRead**. A placement gets its base record's scripts since task 0181 (`baseScriptsOf`, BOOK is a
`BaseTable::Model`), so a placed book has a script instance to send it to.
**Quest.** `Book3ValuableLostLegends` (0x0ED608) sets `dunGauldursonQST` (0x0E4D31, name "Forbidden Legend") to stage 10, which
has a log entry (CNAM) and an objective: the journal (task 0185) lists it. The Legend of Red Eagle sets `dunRebelsCairnQST`
(no name, no log text: nothing to show). Dozens of placements of the scripted books exist (195 lines of the survey, those with a VMAD); **the placement near the Dragonsreach shelf
(0x0E27D5 at 1669, 2267, -18 of `WhiterunDragonsreach`) was reachable by the activate key only from one angle** (found with the
ray sweep `tools/skyworldbooks_check/luatest/scripts/inv/sweep.lua`: from the west the ray passes through the shelf to a flagon).
Owned placements (REFR XOWN) exist: Falkreath's Dengeir's Hall has one (0x104B26).
**SCRL.** 99 records after the load order merge (`SKYRIMSE-SCROLLS` lines of `testskyrimsescrollsreal.cpp`): DATA 8 bytes (value,
weight), **SPIT exactly 36 bytes laid out as a SPEL's** (cost, flags, type, charge time, cast type, delivery, cast duration,
range, half cost perk), EFID + EFIT (12 bytes), 0 odd subrecords. **The cast type of a scroll is 3 ("Scroll") in 97 records
and 2 (concentration) in 2** (Detect Life, Telekinesis); the runtime knows 0, 1, 2, so the cast copy is normalised
(3 -> fire and forget, cost 0: the SPIT cost field of a scroll is not a cost). Deliveries: self 34, aimed 50, target actor 5,
target location 10 (touch 0). There is **no plain "Healing" scroll**: the healing scrolls are Fast Healing (0x0965AF, self,
45), Cure Wounds, Grand Healing (area), Heal Other (target actor); the Firebolt scroll is 0x096598 (aimed, 15 damage).
**The inventory.** `Category::Scroll` existed (task 0182); `use()` knew potions (and, since task 0229, poisons) only.
**The windows.** `book_window.lua` (task 0203) shows a book of the inventory; the interface mode is the inventory window's.
The lock window of task 0203 is the model for a window that opens its own interface mode.

## 2. Design

**Books.** `lockbooks_service` gets `readWorldBook(item)` (the body of `readBook` shared through `readImpl(item, held)`: a world
book need not be held and a spell tome is not consumed; the read-once set is the same, so "once" holds across world and
inventory). New `worldbooks.*` (`WorldBooksService` over the book service) with the namespace `skyrimse.worldbooks` 1.0:
`is_book(item)`, `can_take(item)` (false for BOOK DATA flag 2, "cannot be taken", 16 books), `read_count()`, and the Mutate
`read(placement, item)`: applies the first-reading effect, then tells the Papyrus host (`PapyrusHost::onRead`, new: sends
`OnRead` to the placement's instances, run at the next game update by the driver). A new namespace, not version 1.1 of
`skyrimse.lockbooks`, so no existing pin moved. Lua: `worldbooks_global.lua` answers the engine event for a known book (reads
in place, sends `SkyrimBookOpen {item, placement, takeable}` to the player) and the window's `SkyrimBookTake` (calls
`pickup.pickup`, so the existing theft hook judges it); `pickup_global.lua` ignores a book when the worldbooks namespace
says it is one (without the namespace the old behaviour stays: the book is taken); `books_player.lua` opens the interface mode
itself for a world book and leaves it on Take or Close; `book_window.lua` shows a **Take** button for a placement it may take
(only when the host gave it `env.take`). The inventory path is unchanged.
**Scrolls.** `scroll.*`: `decodeScroll` (decodeSpell, then `mScroll`, cost 0, cast type 3 -> 1), `scrollArchetypeApplied`,
`classifyScroll` (All, Some or None of the effects applied, used by the survey). `combat_stats` reads SCRL in the same
worker walk and `CombatIndex::spell(id)` finds a scroll by form id (never by editor id, and not in `spellIds()`).
`SkyrimCombat::castPlayerScroll(scroll)` (new file `combat_scroll.cpp`, modelled on `castPlayerPower`): refuses when the combat
never ran (`updates() == 0`), the id is not a scroll with data and effects, or another waits; otherwise queues it and
`runPlayerScroll` (one call in `combatUpdate`) begins the cast through `beginCast` (cast clip timing, delivery, effects: all of
task 0186), waits at most 5 s for the cast clip's timing and then gives up; `setScrollOutcome(sink)` tells the pack once
whether the cast began. **No virtual was added to `CombatRequestWriter`.** `InventoryService::use` (a scroll: `EquipKind::Scroll`,
`use_kind` "scroll") asks the new hook `mUseScroll` (1 accepted: one is consumed; -1 or 0 refused: kept; no hook: refused); the
pack's hook calls `castPlayerScroll`, and the outcome sink gives the scroll back when the cast gave up. `inventory_windows.lua`
shows **Use** on a scroll row.
**Which scrolls work** (derived from `applyOneEffect` and `fireCast` as they are at this commit, printed by the gated real-data
test; other agents are adding archetypes, so this table ages): an effect is applied when its archetype is value modifier,
peak value modifier, dual value modifier, absorb, stagger, demoralize or accumulate magnitude; the delivery must be self or
aimed (touch only with `combat spell beams`; **target actor and target location are refused by `fireCast`**); the cast must be
fire and forget (a held scroll is cast for the default 3 s hold). Of the 99 scrolls: **45 cast completely** (49 have every
effect applied, 4 of those have a delivery that is not cast: Heal Other, the three runes), **14 partly** (some effects
only: for example Paralyze has the stagger, not the paralysis; Blizzard the damage hazard is not), **36 do nothing** (every
effect an archetype the runtime does not apply: summons, reanimation, cloaks, calm, frenzy, invisibility, light, detect life,
telekinesis, soul trap, banish). The Firebolt scroll, Fast Healing and Cure Wounds are in the first group.

## 3. Stages and tests

Tests first, run red against stubs (bodies returning refusal or nothing), then the code. **Pack tests, 34 new, 22 failed red**
(`testskyrimsescrolls.cpp`: decoder 3 of 4, support 2 of 2, combat 7 of 9, inventory 5 of 8; `testskyrimseworldbooks.cpp` 5 of 11:
the 12 that passed test refusals and absence); **Lua window tests, 8 new, 5 failed red** against the windows of task 0203
(`testskyrimseworldbookwindow.cpp`; the 3 that passed test absence); 1 gated real-data test (`testskyrimsescrollsreal.cpp`,
passes with `OPENEDEN_SKYRIMSE_DATA`). Total 43 new tests, 27 red first. **No existing test was edited or deleted.** After:
`components-tests` (without the Slang, complement and property groups) 12,568 tests, 12,465 pass, 103 skipped, 0 failed;
`openmw-tests` 2,873, 2,871 pass, 2 skipped; the subset `*Skyrim*:Ui*:*Lua*` was 2,674 tests (2,603 pass, 71 skipped) at the
parent and 2,690 (2,618 pass, 72 skipped) now. `CI/check_tests_registered.sh`, `check_layer_rules.sh` (104 exceptions),
`check_extension_sdk.sh`, `check_cmake_format.sh`, `check_game_conventions.sh`, `check_no_osg.sh`, `check_mwbase_fakes.sh` and
`tools/testprofile/coverage.sh` pass (`check_file_names.sh` still lists `tools/skyrimcell_check/rotation_ground.cpp`, as before).

## 4. In game

`tools/skyworldbooks_check/check.sh` (a copy of `tools/skybenches_check` with `GAME_EXTRA`, `PACK_EXTRA`, `AIM`, `EYEUP`, `SWEEP`,
`NEUTRAL_COMBAT`), `OPENEDEN_QUIET_WINDOW=1`, `[Sound] master volume = 0.0` and `music volume = 0.0`, fixed frame time, real key
and real mouse clicks through `OPENEDEN_RMLUI_INPUT_SCRIPT`. Frames in `I:\Projects\OpenEden-work\bow6\wb\keep`.
**World book, quest note, take.** Dragonsreach, `AIM` at the book, Space (update 800 in the first runs, 1500 in the three-run set): `NINV item 000e27d5 base 000ed608 'Lost
Legends' opened`, `worldbooks_global` reads it, `LOCKBOOKS read 'Lost Legends' (book)`; the window (`book_read_lostlegends.png`)
shows the title, page 1 of 6 and **Previous, Next, Take, Close**, the book still on the shelf behind it. OnRead runs at once:
`PAPYRUS quest 000E4D31 started with 6 aliases filled`, `set to stage 10`, the fragment runs. A real click on Take:
`PICKUP 000e27d5 took 000ed608 x1` and the book is gone from the shelf (`book_taken.png`); the journal (J) lists **Forbidden
Legend, running**, with its log text and the objective "Investigate the Gauldur legend" (`journal_forbidden_legend.png`); the
inventory (I) holds Lost Legends with a Read button (`inventory_after_take.png`). Three runs (Space at update 1500, `keep/input_book.txt`): the 10 relevant log lines are
byte-identical (md5 `a4a2e266e02c` three times). The Papyrus host starts on a worker in real time (10 to 25 s here): a book
read before it has started is queued, the quest still starts, but the log order of the PAPYRUS lines against the PICKUP line
then differs (seen with Space at 800 and 1200), hence the late key. An unscripted shelf book (The Alduin/Akatosh Dichotomy, 0x0E27CD) opened the
same way. **Theft.** Dengeir's Hall (owned placement, `crime = true`): read in place without a crime, then Take: `CRIME theft
witnessed by dengeir faction 00028170 bounty +6 total 6` (`t2_theft_log.txt`).
**Off.** With `lockpicking and books = false` the same activation logs `[skyrimse pickup] item activated` and
`PICKUP ... took` (the old behaviour), no window, no quest (`JOURNAL ... 0 running`).
**Scrolls.** Bannered Mare, Hulda casts Firebolt at the player every 6 s (`combat test cast`), the player holds Scroll of Fast
Healing and Scroll of Firebolt (`inventory test items`): her bolt does `FireDamageFFAimed ... -> player magnitude -25`
(`scroll_inventory_damaged.png`: the health bar is down); the inventory shows **Use** on both rows; a real click on Fast Healing:
`COMBAT player scroll FastHealingScroll queued`, `INV use 000965af: scroll cast, ok`, the cast begins after the inventory
closes, `COMBAT cast FastHealingScroll by player cost 0.000 magicka 100.000 of 100.000`, `COMBAT effect RestoreHealthFFSelf
player -> player magnitude 45.000 instant` (the bar is full again); then Firebolt: `COMBAT spell launch FireboltScroll player`,
`spell ended ... fate 1`, `COMBAT effect FireDamageFFAimed player -> actor 108142 magnitude -15.000 instant`
(`scroll_after_firebolt.png`: the bar full, Hulda's enemy bar shown). Magicka stays 100 of 100. Three runs: the 29 COMBAT,
SCROLL and INV use lines are byte-identical (md5 `40aeffd746a3` three times). **Off:** with `[Game] neutral combat = false`
both Use clicks log `SCROLL ... refused` and `the scroll cannot be cast now`, the inventory still holds both scrolls.
**Morrowind unchanged** (against a `openmw.exe` built from the parent `a99561bd2d` in the same build folder, kept as
`openmw_parent.exe`): `tools/worldobjects_check` 111 lines, byte-identical to the parent's (md5 `42facf6efb92`), OK; the fixed
Balmora frame (`--start Balmora --random-seed 7`, `OPENEDEN_FIXED_FRAME_TIME`, `--auto-screenshot 600`, 1280 x 720) has the
SHA-256 prefix `0d6dd39995b927fa` for the parent, this build, and this build with `neutral world`, `neutral actors`, `neutral
inventory`, `neutral combat`, `inventory` and `lockpicking and books` on.

## 5. Edits outside the new files

New files: `scroll.hpp/.cpp`, `combat_scroll.cpp`, `worldbooks.hpp/.cpp`, `worldbooks_global.lua`, the three test files and
`tools/skyworldbooks_check/`. Edited (all additive): `perks.hpp` (`SpellRecord::mScroll`), `combat.hpp` (`castPlayerScroll`,
`setScrollOutcome`, `runPlayerScroll`, three members), `combat_magic.cpp` (one call in `combatUpdate`), `combat_stats.hpp/.cpp`
(SCRL walk, `mScrolls`, the `spell()` fallback), `inventory.hpp` (`EquipKind::Scroll`), `inventory_service.hpp/.cpp` (hook, use,
`use_kind`), `lockbooks_service.hpp/.cpp` (`readWorldBook`, `readImpl`), `papyrus_host.hpp/.cpp` (`onRead`),
`skyrimse_game_pack.cpp` (the driver's `read`, the worldbooks service, the scroll hook and outcome), the four Lua files
`book_window.lua`, `books_player.lua`, `inventory_windows.lua`, `pickup_global.lua`, `builtin.omwscripts`, `files/data/CMakeLists.txt`,
`game_packs/skyrimse/CMakeLists.txt` (both source lists) and `apps/components_tests/CMakeLists.txt`.

## Limits and decisions for toddhow

1. **A spell tome read in the world teaches its spell and stays where it lies** (RECALLED-VERIFY: I believe a world tome is
   not used up); taking it afterwards gives a tome that is read and kept ("You already know ..."). The alternative is to teach
   only from the inventory.
2. **OnRead goes to a placed book only, once per reading, at the moment the window opens** (not when it closes: RECALLED-VERIFY
   which Skyrim does). A book read from the inventory still sends nothing (it has no placement). Without Papyrus (`papyrus`
   off) nothing is sent and the world book is still readable.
3. **A scroll is used up when the cast is accepted** and given back only if the cast gives up after 5 s without the cast clip's
   timing. It gives no skill experience (cost 0).
4. **No plain Healing scroll exists in the data**; Fast Healing was shown. The scroll magnitudes are the scrolls' own (Firebolt
   15, Fast Healing 45), not the spells'.
5. **Scrolls with target actor or target location delivery are refused by the magic runtime** (Heal Other, three runes, the
   summon scrolls); 36 of 99 scrolls do nothing and 14 do part: that is the magic runtime's coverage, not a scroll limit. A
   held scroll (2) is cast for the default 3 s hold, and both are unsupported archetypes anyway.
6. A book flagged "cannot be taken" has no Take button. The window is mouse driven (keyboard and gamepad navigation of Take
   were not checked).
7. Not built: books read from an exterior ground (the harness reached interiors only), a book on the ground with no script
   instance (works, nothing is sent), `OnRead` for the 16 skill books of the Dark Brotherhood (they use acquire-item events).
**Not run:** the Linux gate and GCC, clang-format 14, clang-tidy, the Lua integration tests, a fresh review, a concentration
scroll in game, a frame of the Firebolt scroll's projectile in flight, keyboard use of the book window.
