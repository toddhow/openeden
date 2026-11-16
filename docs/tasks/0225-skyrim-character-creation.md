# Task 0225: a first character creation for a Skyrim session

Status: BUILT; Windows only, local commit on branch `racemenu` (from `main` at `b494cc2fb8`), not pushed. Not run: the Linux gate, clang-format 14, clang-tidy, the Lua integration tests, `openmw-tests`, a fresh review (see "Not run").
Design doc section: `docs/design/skyrim-se-implementation-plan.md` (character creation), decision 0015 (UI), tasks 0207 (menus: New Game), 0222 (the player's body), 0200 (progression), 0186/0179 (abilities in the combat).
Goal: at New Game of a Skyrim session the player chooses race, sex, hair, facial hair, eyes and a name in an RmlUi window over his own body; the choice is saved, drawn, and feeds the progression (starting skills) and the combat (race abilities).
Inherited code touched: none under `apps/` (no engine change). Pack: `actors.*` (RACE DESC, skill boosts, plugin index; HDPT plugin and flags; FLST), `player_body.*` (a head choice in the request), `progression.*` (`applyRacialBoosts`), `combat.*` (`setPlayerRaceAbilities`), `world_view.*`, `skyrimse_game_pack.cpp`.
Constraints: off by default behind `[extension.skyrimse] character creation` (needs `menus`, `player body`; the window needs `[GUI] rmlui`); Morrowind unchanged (nothing starts without the pack's namespace).
Out of scope: facegen sliders, hair colour, skin tint, scars and brows, a first-person preview, setting the engine's player name, the Skyrim intro.

## 1. Survey

* **Playable races (data, a Python walker over the five masters).** RACE DATA offset 32 is a flags word; bit 0 is set on exactly 10 of 161 races, the ten player races (forms 0x13740 to 0x13749). VERIFIED against the game's known starting bonuses. DATA bytes 0 to 13 are seven (actor value, boost) pairs (255 ends the list): Nord Two-Handed 10 and One-Handed, Block, Smithing, Speech, Light Armor 5; Khajiit Sneak 10 and Lockpicking, Pickpocket, Alchemy, One-Handed, Archery 5. FULL and DESC are localised (names in `.STRINGS`, texts in `.DLSTRINGS`); the in-game window shows the real texts ("Citizens of Skyrim, they are a tall and fair-haired people...").
* **Abilities.** RACE SPLO: Nord 0xAA020 (Resist Frost) and a power 0xE40C3; Khajiit two. The pack has no spell NAMES (SPEL FULL is not decoded), so the window shows a count.
* **Head parts.** The race's own list (RACE NAM0 HEAD with INDX) holds only the DEFAULT parts. The choices come from the HDPT records: DATA flags bit 0 playable, bit 1 male, bit 2 female, bit 3 extra part; PNAM the type (2 eyes, 3 hair, 4 facial hair, 6 brows); RNAM an FLST of the races it is valid for (9 of 766 have none, excluded). The pack did not read FLST for actors: `ActorRecords::mFormLists` (named `ActorFormList`, `FormListRecord` exists in `dialogue.hpp`) is new, capped, never fails a plugin. Counts for the Nord man: 29 hairs, 46 beards, 22 eyes; the Nord woman: 26 hairs, 0 beards, 20 eyes (the gated test pins these numbers, they equal the Python survey's).
* **The menu flow.** Task 0207's menu starts New Game; the engine then starts the neutral session at the `--start` place. The window cannot sit in the main menu (there is no world), so it opens in the game, in GUI mode with no MyGUI window (the pattern of the skills window), over the paused world.
* **No engine change needed.** The body of task 0222 already rebuilds when the pack's revision moves; the Lua camera API (Static mode) places the camera in front of it.

## 2. Design

* `character.*` (pure): `playableRaces`, `partOptions`, `defaultChoice`, `validChoice`, `sanitiseName`, `applyChoice`, the canonical save blob (`encodeChoice`/`decodeChoice`) and `Creator`, the working choice (race, sex, three parts, name; a race or sex change goes back to that race's defaults; the lists are sorted by name or editor id; hair and facial hair start with None; a default part the race's list does not name is added).
* `character_service.*`: `CharacterService` (hooks for the records, the body, the race and the log), the chunk `character` v1 (a done byte, then a length and the choice's blob), and the namespace `skyrimse.character` 1.0 (Read `ready needed revision race_count race_name race_desc race_skills race_abilities race_index female name part_count part_name part_index`; Mutate `choose_race choose_female step_part set_name finish`).
* **The body follows at once.** The world is paused while the window is open, so nothing may wait for the pack's game phase: every Mutate call applies the choice to the world view (`setPlayerLook`, which bumps the revision the engine polls) in the call itself.
* `PlayerBodyRequest::mHeadChoice`: a listed head part type REPLACES the race's default parts of that type (the replaced hair's extra parts go with it), None removes them.
* `SkyrimWorldView`: `setPlayerLook`, `characterAccess` (the actor index once the combat worker has read it, plus a text resolver over the plugins' string tables, so the menu does not wait for the dialogue data).
* Starting skills: `PlayerProgression::applyRacialBoosts`, applied by the pack's frame callback when the progression exists (only for a NEW character: a loaded one has them in its own `progression` chunk). Race abilities: `SkyrimCombat::setPlayerRaceAbilities(chosen, standIn)` (the stand-in `Player` record is a Nord, so the Nord's abilities are removed and the chosen race's added; the ability sums are worked out again).
* Lua: `files/data/openeden/skyrimse/character_window.lua` (the window logic, tested over the headless backend), `scripts/openeden/skyrimse/character_player.lua` (opens the window when `needed()`, keeps the interface mode, places the camera, gives it back) and `character_global.lua` (the Mutate calls; a global script only). The camera stands at the first of eight angles round the player whose line from his head is free for 230 units (a column hid the body in the first run).

## 3. Tests

* `testskyrimsecharacter.cpp` (pack model, 35 incl. 3 progression tests and the gated real-data test), `testskyrimsecharacterservice.cpp` (13), `testskyrimsecharacterwindow.cpp` (10 Lua).
* **Red against stubs: 45 of the first 54 ungated tests failed** (stub `character.cpp`/`character_service.cpp` answering nothing and a stub window module); the 9 that passed test absence (an empty index, an unknown race, nothing shown) or pinned `makePlayerAppearance`'s head choice, which was written before the stubs (2 tests, not run red). All pass with the code (58 character tests; `*Skyrim*:Ui*:*Menus*:*Lua*` 2,423 pass). The 3 progression tests (`SkyrimCharacterSkills`) were written AFTER the code. One test-data fault (an Argonian with no default eyes made the load test fail) was fixed in the test.
* The gated test passes on the installed masters: 10 playable races, the Nord's skills text, every race and sex has a valid default choice with options for each part, and the counts above.
* No existing test was edited or deleted.

## 4. Stage log (2026-11-16)

* In game (`tools/racemenu_check/check.sh`, Bannered Mare, fixed frame time, quiet window, master volume 0, real clicks through `OPENEDEN_RMLUI_INPUT_SCRIPT`): frames viewed of the window on the Nord man (the camera first stood inside a column; fixed), after clicking the race arrow (Khajiit), Female, hair twice and eyes once (the body in front of the camera is a Khajiit woman with her tail, her hair and a different face), with "Ria" typed in the name field, and after Done the third person view in the Mare. The skills went from 15 everywhere to One-Handed, Archery, Pickpocket, Lockpicking, Alchemy 20 and Sneak 25 (the Khajiit bonuses), the combat trace says "player race abilities: 2 chosen, 2 of the stand-in's race left out".
* **Save and load in a new process:** the save written after Done loads with the creation not offered again, race Khajiit, female, hair `HairKhajiitFemale02`, name Ria, skills as saved, and the body drawn (frame viewed).
* **Three runs** of the whole scenario give the same 11 RM225/CHARACTER lines (md5 `15da43e0c515`; screenshots are not byte-compared).
* **Morrowind unchanged:** the fixed Balmora frame (`--start Balmora --random-seed 7`, frame 600, 1280 x 720 equivalent) has the hash `1c5d47ad0b5dc32d` with the settings off and with `neutral world`, `neutral actors`, `neutral player body`, `menus`, `player body` and `character creation` on; the log says both character scripts "were not started: no game pack provides script bindings". Morrowind's own character creation was NOT shown: the base configuration here starts a Morrowind new game at the main menu with no chargen to compare (no engine file changed, the Lua scripts do not start without the pack's namespace).

## 5. Edits outside the new files

`game_packs/skyrimse/{CMakeLists.txt (two lists), extension.json, include/game_skyrimse/{actors,combat,player_body,progression,world_view}.hpp, src/{actors,combat,player_body,progression,skyrimse_game_pack,world_view}.cpp}`, `apps/components_tests/CMakeLists.txt`, `files/data/{CMakeLists.txt,builtin.omwscripts}`, `tools/testprofile/{profile.cfg,README.md}`, `CLAUDE.md` (changelog row), `game_packs/skyrimse/CLAUDE.md` (a note). No SDK header changed, no engine file, no combat contract change.

## Decisions for toddhow

1. **The creation opens in the game, not before it.** New Game starts the world (the Bannered Mare) and the window opens over it a moment later (when the actor records are read, a few seconds); a New Game without `character creation` is as before. Alternative: a loading step before the first frame.
2. **An old save has no `character` chunk**: it is treated as a new game and offers the creation once. Say if a save with a `progression` chunk should count as finished.
3. **The name is stored in the pack chunk only.** The engine's player record keeps its stub name, so the save list and the HUD do not show it. Writing it needs an engine call from the pack (a new SDK interface).
4. **Hair, facial hair and eyes show editor ids** (`HairKhajiitFemale02`): the records have no FULL for most parts. A readable name needs a naming rule of ours or the CK's strings.
5. **Race abilities are shown as a count** (spell names are not decoded) and only their DamageResist and weapon speed values reach the combat (the rule of task 0179); a power such as the Nord's Battlecry is not given to the player.
6. **Starting skills apply to a new character only when `player progression` is on**; with it off the boosts are dropped.
7. **The Khajiit woman's default eyes are `FemaleEyesArgonian`**: that is what the race's own head part list names (a data quirk), not a bug of the menu.
8. **No first-person preview, no tint or colour choice** (the head part's own texture set only).
9. After Done the camera returns to the mode it had (first person at a new game); the check's third-person frame comes from a test aid in `tools/racemenu_check`.

## Not run

The Linux gate and GCC `-Werror` (the GCC traps of the root guide were checked by hand), clang-format 14 (formatted by hand to 120 columns), clang-tidy, the Lua integration tests, `openmw-tests`, mutation checks, a fresh review, the combat race ability rule in a unit test (seen only in a trace), Morrowind's own character creation, keyboard or gamepad use of the window, the Escape key (the script re-enters the mode; not tried), an Orc, Argonian or Redguard frame (only the Nord and the Khajiit were seen).
