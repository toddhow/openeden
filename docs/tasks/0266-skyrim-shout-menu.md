# Task 0266: Skyrim shout menu

Design doc section: `docs/tasks/0205-skyrim-shouts.md` (shouts), `docs/tasks/0256` (word walls, `unlockWithSoul`), `components/ui_rml/CLAUDE.md` (controller). Approved by toddhow.

Goal: a window to see the shouts the player has learned a word of, spend dragon souls on their words and choose the shout the shout key uses, with the mouse and the controller.

Inherited code touched: none under `apps/openmw`. `files/data/openeden/ui.lua` gets one additive document property (`navCapture`).

## What was built

* **Pack (`game_packs/skyrimse`)**: `WordRecord::mTranslation` (WOOP `TNAM`, decoded); `ShoutService::menu()`, `menuUnlockNext(shoutEditorId)`, `menuEquip(shoutEditorId)` (types `MenuShout`, `MenuWord`, `MenuResult` in `shouts_service.hpp`); an optional hook `Hooks::mText` for localised strings; ten Read functions and two Mutate functions added to the namespace `skyrimse.shouts` (version stays 1.0, additive): `menu_count`, `menu_id(i)`, `menu_name(i)`, `menu_equipped(i)`, `menu_can_equip(i)`, `menu_can_unlock(i)`, `menu_word_count(i)`, `menu_word_name(i, w)`, `menu_word_translation(i, w)`, `menu_word_state(i, w)` ("unlocked", "learned", "unknown"), and `menu_unlock(shoutEditorId)`, `menu_equip(shoutEditorId)` (Mutate, by editor id so a menu that changed between the read and the call never touches another shout). The pack resolves the word names and translations from the plugin's `.STRINGS` table through the string reader the books already use (`resolveShoutString` in `skyrimse_game_pack.cpp`: the plugin is the load order position in the top byte of the form id; a light plugin, or a table that cannot be read, falls back to the editor id's name and no translation).
* **Rules (ours, the data has none)**: a shout is listed when at least one of its words is learned; a shout's level is the leading run of unlocked words (task 0205), so "Unlock" spends one dragon soul on the first word after that run and only when that word is learned; "Equip" needs one unlocked word; both announce a message (the existing `ShoutState::announce`, shown as the game's message): "Word of Power unlocked: Fus", "You need a dragon soul to unlock a word.", "You have not learned the next word of X.", "X equipped", ....
* **Window (`files/data/openeden/skyrimse/shout_windows.lua`)**: souls and the equipped shout in the header, one row per learned shout (a star marks the equipped one; words as `Fus (Force)` unlocked, `[Ro (Balance)]` learned and not unlocked, `-----` unknown), an Unlock and an Equip button in fixed cells, Close. Rows keep their nodes when the pack changes (the nodes read a reactive table), so the controller focus stays on the button that was pressed. `ui.Document` gets `navCapture = true` (sets the `nav-capture` attribute on the document element), the document opts in to the controller.
* **Host scripts**: `shouts_player.lua` opens it with the key **H** (free in the engine's default bindings: used are WASD, F, R, Q, E, J, T, 1 to 0, - = [ ], Tab, Space, LShift, LCtrl, Caps Lock, ` and F1/F10 to F12; the pack uses K skills, I inventory, M map, P power, L lock pick), `shouts_global.lua` (new, in `builtin.omwscripts` with `REQUIRES=core.skyrimse.shouts>=1.0`) receives the requests and calls the Mutate functions. Second H, Escape or Close leave.
* **Saved**: the equip choice, learned and unlocked words and souls were already in the `shouts` chunk (v1); nothing new is saved (test: the equip choice survives the chunk, and a load in a new process shows the menu from the save alone).
* **Behind** the existing `[extension.skyrimse] shouts` (no new setting; needs `[GUI] rmlui`). Controller: with a focus entry (`[GUI] rmlui focus button = back`, or `rmlui focus key`, both default empty; `controller menus` is not needed) the d-pad, A and B drive it.

## Not built

* **The powers section (race and stone powers, the `P` key choice)**: not cheap. `P` uses the first ready power (`character_player.lua`); choosing one means a new saved value, and the only home for it is the `character_powers` chunk (v1, whose format an existing test pins) or a new chunk. Left for a follow-up (decision for toddhow: a new chunk `power_choice` v1, or v2 of `character_powers`).
* No shout-name display text from the `SHOU` FULL string (the name stays derived from the editor id as the HUD does) and no word wall "learned from" information.
* No controller button to open the window (H or a keyboard only; once open, the focus entry button works). A keyboard-less controller player has no way to open it until a button is chosen (decision for toddhow).

## Tests

* Written first, run red against stubs (`ShoutService::menu` and friends returned empty or `NoShout`, the window module was an empty stub, `TNAM` not decoded): **26 new tests, 23 failed, 3 passed (they pin absence: an empty menu before the records, a refused unlock before the records, a frame with nothing changed makes no protocol call)**. Files: `apps/components_tests/skyrimse/testskyrimseshoutmenu.cpp` (16: TNAM, the model, unlocking, equipping, the chunk, the script functions through a stub bindings service, the revision) and `apps/components_tests/ui/testskyrimseshoutmenuwindow.cpp` (10).
* After the controller run showed the focus lost when a pressed button's row was rebuilt, the window's rows were changed to stable keys and a test added (`aChangeKeepsTheSameButtonNodesSoTheControllerFocusStaysOnThem`, written after the change, not red first); the test helper counts only visible buttons (rows now hide buttons instead of replacing them). 27 tests pass.
* `*Skyrim*:Ui*:*Lua*:*Extension*:*Frozen*:*Neutral*:*Papyrus*:*Shout*` of `components-tests` and the whole `openmw-tests`: 3,819 pass of `components-tests` (0 failed), the whole `openmw-tests` 3,093 pass (the engine was not changed). `check_tests_registered`, `check_layer_rules` (104) and `tools/testprofile/coverage.sh` ok. No existing test edited.

## In game (`tools/shoutmenu_check/check.sh`, Bannered Mare, real keys and mouse/controller events, sound off)

* Four words learned (Fus, Ro, Yol, Toor) and two souls by test settings; H opens the menu (frame `sm_keep/opened.png`: both shouts, words with their translations from the string table, "Dragon souls: 2", "Equipped: none"); a real click on Unlock spends a soul (Fus, "Word of Power unlocked: Fus"), a second the other (Ro), Equip then equips Unrelenting Force (`sm_keep/equipped.png`); H closes. The script namespace prints the same state.
* **Three runs give an identical state sequence** (`SHOUTMENU` lines with the simulation time removed: md5 `0ed2689e3dd4` in all three).
* **Save and load in a new process**: the equipped shout, both unlocked words and 0 souls come from the save alone (`sm_keep/after_load.png`).
* **Controller** (`ctrl.txt`, `[GUI] rmlui focus button = back`): back moves the focus to the first Unlock (frame `sm_keep/ctrl_focus.png`), A unlocks Fus (souls 2 to 1), d-pad right moves to Equip, A equips (`sm_keep/ctrl_equipped.png`); with and without `[GUI] controller menus = true` (that setting is for the MyGUI menus; the overlay did not need it). A first version rebuilt a row when its state changed, which dropped the focus (the d-pad press after the A press did nothing); the rows are stable now.
* **Morrowind unchanged**: `tools/worldobjects_check` 110 objects OK against the `combat4` build, the fixed Balmora frame `1c5d47ad0b5dc32d` for the reference, this build, and this build with the neutral settings and `shouts` on.

## Decisions for toddhow

1. The key H (free in the engine's bindings); a controller opening button is not chosen.
2. Powers section not built (above).
3. The window shows a word's `FULL` text as its name and `TNAM` as the translation; in the data one name reads "T8r" (Toor, Fire Breath's second word) as stored, not changed.
4. Unlock always takes the next word in order (never an arbitrary one), as the leading-run rule needs.
