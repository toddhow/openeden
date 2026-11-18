# Task 0281: the shout menu's powers section (follow-up of task 0266)

Design: `docs/tasks/0266-skyrim-shout-menu.md`. toddhow chose this on 2026-11-17 and decided to keep the menu on the H key.

Goal: list every power the player has in the H window, with its source and cooldown, let him equip one so that the P key uses it, and save the choice.

## What was built

- `CharacterService` (namespace `skyrimse.character`, stays 1.1; four additive functions): `power_source(i)`, `power_cooldown(i, now)` (seconds left of the once-a-day cooldown), `power_chosen()` (index or -1), `choose_power(name)` (Mutate; an empty name clears; an unknown or Unsupported power is refused).
- Sources: "Race" for a race power, "Lycanthropy" for Beast Form, "Vampirism" for a power whose name starts with Vampire, "Standing stone" for the other extra powers (`Hooks::mPowerSource`).
- Save: a NEW small canonical chunk `power_choice` v1 (one u32, the spell form id; zero is no choice), because `character_powers` is a list of use times whose format an existing test pins. Old saves have no such chunk and load with no choice; a vanished power gives -1; a damaged chunk is refused.
- Lua: `shout_windows.lua` has a "Powers" section under the shouts (name, source, "Ready" / "Ready in ...", Equip and Unequip buttons, a `*` on the chosen row, "Equipped power: ..." line), refreshed when the character revision changes or a game minute passes; button nodes are keyed by the editor id so they stay stable (controller focus works as in the shouts section). `character_player.lua` `usePower()` uses the chosen power (message "X is not ready." when it is on cooldown) and falls back to the old first-ready rule with none chosen. `character_global.lua` handles `choosepower`.
- Found by running it: `core.getGameTime()` is a float and the binding wants an integer; `shouts_player.lua` floors it.

## Tests (written first)

`apps/components_tests/skyrimse/testskyrimsepowerchoice.cpp` (10), `apps/components_tests/ui/testskyrimsepowerswindow.cpp` (10). Against stubs (new service methods returning false / -1 / 0, the old Lua module) 20 of the 24 failed; the other 4 pin absence or the unchanged old chunk. After the change `*Character*:*Shout*:*Werewolf*:Ui*:ChoiceFx*:PowerFx*` 705 pass. No existing test edited.

## In game (`tools/shoutpowers_check/check.sh`, Bannered Mare, real input script, sound off, quiet window)

Nord, Beast Form granted by `werewolf test grant`. Real clicks: Done of the creation window, H, a click on the Equip button of Werewolf Change, H to close. Then P: the log says `CHARACTER power WerewolfChange used` and `WEREWOLF transformed` (without the choice P uses Battle Cry, the first ready power). The window shows the `*` row, "Equipped power: Werewolf Change" and Unequip (frames viewed). A save written 3 s after the choice, loaded in a NEW process: the choice is back (`chosen=1`) and the window shows it with no test learning; P then transforms again. Three runs: the choice and use lines are identical; the used-at game second differs by 14 in one run (the game clock at that moment depends on the records worker, not on this feature). Cooldown after use reads 86357 s and counts down.

Morrowind unchanged: `tools/worldobjects_check` 110 lines OK; the fixed Balmora frame is `1c5d47ad0b5dc32d` with the settings off and with the neutral settings on.

## Not run / decisions

- Not run: the Linux gate, clang-format 14 (formatted by hand), clang-tidy, the Lua integration tests, a fresh review, the controller path in game (the Equip buttons are focusable like the shouts' buttons; unit tested only), a vampire or stone power in game (source strings unit tested).
- Decision: the cooldown text is minute-resolution; the choice lives in its own chunk (above); an Unsupported power cannot be equipped.
