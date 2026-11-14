# Task: UI follow-ups and the second component slice (decision 0015, follow-up of tasks 0162 to 0164)

Status: BUILT (2026-11-14), not pushed. Requested by toddhow's coordinator: four items, in order.
Design doc section: `docs/research/0045-ui-system.md` section 6 (components); `docs/tasks/0162-ui-element-protocol-and-lua-library.md` ("Not done"), `docs/tasks/0163-ui-theme-system.md`, `docs/tasks/0164-ui-same-frame-geometry-and-update-hook.md` ("Decisions for toddhow", item 3).
Goal: remove the explicit-width workaround of `ui.List`, make `setUpdater` replace, show clicks and typing reach a Lua panel in a running game, and add Checkbox, Toggle, Slider, ProgressBar, ScrollView and Tabs.
Inherited code touched: `components/ui_rml/rmlprotocol.{hpp,cpp}` (`documentDefaults`, new `scrollbarDefaults`), `components/ui_rml/themesheet.cpp` (`requiredTokens`), `components/ui_lua/backendpackage.{hpp,cpp}` (`UpdateRegistry`, `setUpdater`), `files/data/openeden/ui.lua`, `files/data/ui/base.rcss`, `files/data/ui/document.rml`, both shipped themes, `tools/ui_check`.
Constraints: off by default (`[GUI] rmlui`); no colour or size literal in `base.rcss` (tokens only); no clip mask, filter, gradient or shadow.
Out of scope: Dropdown, Window, Menu, Tooltip, Grid, Icon, Radio, l10n, a layout box query.

## Acceptance criteria
- [x] A `ui.List` in a column (and in a flex row, a shrink-to-fit document, `align-items: flex-start`) needs no explicit width: its rows fill the list beside its scrollbar (RmlUi layout test, a frame).
- [x] A second `setUpdater` of one session replaces the first, keeps its place; other sessions are untouched; after `clear` a session adds again.
- [x] Clicks on a Lua `Button`, `Checkbox`, `Toggle`, a click on a `Slider`, a click on a tab and typing into a `TextInput` reach the Lua side in a running game (log lines) and the frames show the result.
- [x] Six components with headless tests, themed through new tokens that every shipped theme declares.
- [x] With `[GUI] rmlui` off nothing changes (see "Off means unchanged": compared as far as was feasible).

## Item 1: the root cause of "a List needs an explicit width"

**Measured, not guessed** (`apps/components_tests/ui_rml/testuirmllistlayout.cpp`: the protocol calls `ui.List` makes, on a real `Rml::Context`): the width of the list's body, and so of every row (`width: 100%`, absolute), is **0 whenever the list's scroll container shows a scrollbar** (`overflow-y: scroll`, or `auto` with content taller than the list) **and the scrollbar parts are unstyled**. With `overflow: visible`/`hidden`, or `auto` with content shorter than the list, the body is the full width; so are the same elements with `display: flex` on the scroll container. In-flow content, `width: 100%`, `min-width`, `display: flex`/`inline-block` on the body made no difference; only an absolutely positioned body or a flex scroll container did. Cause: RmlUi has no default style sheet and an unstyled `scrollbarvertical` has `width: auto`, which takes the whole width of its container, so the scroll container's content width is `width - scrollbar = 0`. That is the 0162 finding "rows showed one letter": it was found before the theme's base sheet gave the scrollbar a size (`scrollbarvertical { width: var(--scrollbar-width) }`, 0163), and the explicit `width` worked round it because the rows then no longer depended on the body.

**What is wrong today** is not the shipped theme (it styles the scrollbar, and the demo list shows its rows with no width) but every path that does not link `base.rcss`: the in-memory fallback document (`RmlProtocol::documentDefaults`), a mod that replaces the base sheet, and any scroll container that is not a `List`, e.g. the new `ScrollView`. **Fix:** `RmlProtocol::scrollbarDefaults()` (sizes of the scrollbar parts, one line) is part of `documentDefaults` and is also the first `<style>` of `files/data/ui/document.rml`, before the link to the base sheet, so the theme's own sizes still win. A test checks that the template file and the defaults carry the same text.

**Tests.** Written first as five layout tests (`ARowTakesTheListsWidthInAColumnOfAFixedWidthDocument`, `...ShrinkToFitDocument`, `...FlexRowAndBesideAFixedSibling`, `...ColumnThatAlignsItsItemsToTheStart`, `AnExplicitWidthStillWins`): all but the last failed against the old code (row width 0 where 260, 800, 257.1 were expected; copied from the run before the fix). After the fix they pass, with two new ones (a list that does not overflow has no scrollbar and fills its width; the template file and the defaults agree). The expectation was adjusted from "row equals list" to "row equals list minus the 8dp scrollbar" once the cause was known, which is the correct layout. **Frame:** `tools/ui_check` demo with the list's `width = 236` removed (`I:\Projects\OpenEden-work\uinext\work\runs\w2`): rows and scrollbar at the full width of the column. **A frame of the old build showing the failure was not made**: the build with the theme already draws the list correctly, so the old behaviour is shown by the unit test, not by a picture.

## Item 2: `setUpdater` replaces

`UpdateRegistry::add` returns an id, `replace(id, fn)` swaps the function in place (keeps the order, clears the remembered error) and returns false for an id that `clear` forgot; the session table keeps its id. Tests (`testuiupdatehook.cpp`, three added; the existing nine are unedited and pass): two calls of one session run the second only; replacing keeps the place and leaves other sessions alone; a session registers again after `clear`. The first two were red against the old registry (`size()` 2 and both functions run).

## Item 3: clicks and typing in a running game

`OPENEDEN_RMLUI_INPUT_SCRIPT` with `tools/ui_check/input_lua.txt` (new) and `INPUT=<absolute path>` in `tools/ui_check/check.sh` (new; `MODDIR` is now really read), `[GUI] rmlui debug panel = false`, the Lua demo mod (its handlers log `[uipanel] ...`). Log of the run, in order: `click one`, `click two`, `change hello`, `change hell` (BackSpace), `change hell!`, `checkbox false` (it started on), `toggle true`, `slider 70` (a click on the track), `tab list`. Frames viewed (`runs\in1`): the panel after typing shows `hell!` and `clicks: 2`, then the Checkbox off, the Toggle on, volume 70 with the progress bar following it, then the List tab with a scrolling list. Real SDL events through the real input filter; no real mouse.

## Item 4: the components

All in `files/data/openeden/ui.lua` (one scope per document, reactive properties, a class string `component [state] [user class]` written by one effect that writes only a changed string):

| Component | Properties | Elements | Notes |
| --- | --- | --- | --- |
| `Checkbox` | `value` (Value = two-way, or a plain boolean = own state), `label`, `onChange(bool)` | `div.checkbox[.checked]` > `div.check-box`, `div.check-label` | focusable; a click toggles |
| `Toggle` | as Checkbox | `div.toggle[.on]` > `div.toggle-track` > `div.toggle-knob`, `div.toggle-label` | the knob moves by padding, no `calc` |
| `Slider` | `value` (two-way), `min` 0, `max` 1, `step`, `width`, `onChange(number)` | `input` `type=range` | RmlUi's own range input; a change that is not a number is ignored; no echo of the value it just got |
| `ProgressBar` | `value`, `max` 1 (both may be reactive) | `div.progress` > `div.progress-fill` | the fill's `width` is a percent, clamped to 0 to 100 |
| `ScrollView` | `height` (required), `width`, children | `div.scrollview.scroll`, `overflow-y: auto` | |
| `Tabs` | `value` (two-way id, default the first tab), `tabs = { { id, label, content = function() end } }`, `onChange(id)` | `div.tabs` > `div.tab-bar` > `div.tab[.active]`, `div.tab-panel` per tab | every content is built once with the Tabs; only the selected panel is `display: block` |

Tokens added to `requiredTokens()` and to both shipped themes: `--color-track`, `--color-accent`, `--color-tab`, `--color-tab-active`, `--size-check` (the existing theme tests iterate the list, so no existing test changed). `base.rcss` styles every class above through tokens only.

**Tests.** `testuicomponents2.cpp` (18 headless tests, written first; 15 failed red against the library without the components, the 3 that passed at once test error paths of components that did not exist yet, where an error is the answer either way) and `testuiwidgetsheet.cpp` (3 tests: the tokens are required, declared by every theme, read by the base sheet, every component class styled; all 3 red first). After the code every one passes. Frame `runs\w1` (demo document `lua-widgets`): tabs, Checkbox, Toggle, Slider, volume text and ProgressBar draw in the neutral theme.

## Results (RTX 4070, 1280 x 720, Windows only, own build `I:\Projects\OpenEden-work\uinext\build`, RelWithDebInfo)
- `components-tests` (`Ui*:*Theme*:*Rml*:*Overlay*:*Lua*:ArchiveKinds*`): 483 pass. `openmw-tests` (`*Ui*:*Lua*:*ExtensionHost*`): 69 pass. The whole of either binary was not run.
- `CI/check_tests_registered.sh` ok, `CI/check_layer_rules.sh` 104 exceptions, `tools/testprofile/coverage.sh` ok.
- **Off means unchanged:** no file that runs with `[GUI] rmlui` off changed (the changes are in `components/ui_lua`, `components/ui_rml`, `files/data`, `tools`, tests). A fixed-time Balmora frame with rmlui off (`--start Balmora --random-seed 1`, `OPENEDEN_FIXED_FRAME_TIME`, frame 600) is identical in two runs of this build (SHA-256 of the pixels `9352849f665d2ed7`). **It was NOT compared against a build of main's parent**, and `tools/worldobjects_check` was not run: a second full build was not made (about 40 minutes of build time).

## Not run
The Linux gate and GCC `-Werror` (the new code is written against the known traps), clang-format 14 and clang-tidy (not installed here), the Lua integration tests, the whole test binaries, mutation checks, a fresh review, high-contrast and variant themes in a frame, keyboard activation (Space or Enter on a focused Checkbox or tab), gamepad navigation of the new components, the Slang twin, a real mouse.

## Existing tests edited
None. (Three new tests were added to `testuiupdatehook.cpp`; the file's existing tests are unchanged.)

## Decisions for toddhow
1. The scrollbar defaults are written twice (the in-memory defaults and `document.rml`) and a test keeps them equal. The alternative, loading the template from the defaults string, would make the template file only a fallback.
2. `Tabs` builds every tab's content when the Tabs are built (hidden panels are `display: none`); lazy building would need a way to destroy and rebuild a panel. A long Tabs content list costs elements for hidden tabs.
3. `Slider` is RmlUi's `input type=range` (its own scrollbar-like parts, styled in `base.rcss`); thumb dragging needs no library code but its keyboard behaviour is RmlUi's.
4. Five new required theme tokens: a third-party theme made before this change will be refused until it declares them (the theme validator falls back to the shipped theme and logs which are missing).
5. `.toggle-knob` uses `--color-text-muted` as its colour (no token of its own).
