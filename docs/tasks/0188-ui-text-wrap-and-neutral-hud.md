# Task 0188: bitmap text wrapping and hiding Morrowind's HUD in a Skyrim session

Approval: toddhow chose both items on 2026-11-15. Full loop (the font engine is UI core; the HUD edit touches `mwgui`).
Only with `[GUI] rmlui` (item 1) and `[Game] neutral combat` + `[GUI] rmlui` in a neutral session (item 2); with them off nothing changes.
Read first: tasks 0104 (U1, the font engine), 0111, 0163, 0166 and 0180 decision 3.

## 1. Text wrapping

**Survey.** `components/ui_rml/fontengine.cpp`, `BitmapFontEngine::GenerateString`. RmlUi's `ElementText::GenerateGeometry`
(`Source/Core/ElementText.cpp`, loop over `lines`) calls `GenerateString` once per line and passes the SAME
`TexturedMeshList` to every call; its own engine (`FontFaceHandleDefault::GenerateString`) sizes the list and APPENDS
vertices and indices. Line metrics (`metricsFor`), `stringWidth` and `layoutString` (`bitmapfont.cpp`) were fine: lines
are broken at the right places, the geometry just never reached the screen.

**Cause (by experiment).** `GenerateString` did `meshes.resize(1)` (harmless), appended its vertices, then
`mesh.indices = quads.mIndices`: it REPLACED the indices, and without offsetting them past the vertices of the lines
before. A wrapped element's mesh therefore held the vertices of all lines and the indices of the last line only, pointing
at the first line's vertices. Measured on a real RmlUi context with a recording render interface (a three-line text of
nine glyphs): 36 vertices, 18 indices referencing 12 of them; on screen the first line, cut short ("Temple o").

**Fix.** Append to the list's first mesh and offset the new indices by the vertices already there. FreeType faces (the
fallback engine) were never affected: they append already.

**Hand line breaks of the journal.** `files/data/openeden/skyrimse/journal_windows.lua` (`LINE_CHARS` and `wrap`) break
lines by hand at 52 characters. They are KEPT: the existing test `SkyrimJournalWindow.aLongEntryAndALongEarlierLine...`
(`apps/components_tests/ui/testskyrimsejournalwindows.cpp`) pins them (it counts the `quest-entry` nodes and their
lengths), and existing tests are not edited without toddhow's approval. To take them out: change that test to expect one
node per entry and drop `wrap`, `addWrapped` and `LINE_CHARS` (task 0185 decision 6).

## 2. Morrowind's HUD in a Skyrim session

**Survey.** `MWGui::HUD` (`apps/openmw/mwgui/hud.cpp`) is the MyGUI HUD: `mHealth/mMagicka/mStamina`, `mWeapBox`,
`mSpellBox`, `mEnemyHealth`, `mCrosshair`, `mMinimapBox`, `mEffectBox`, `mSneakBox`, `mCellNameBox`,
`mWeaponSpellBox`. Visibility setters exist (`setHmsVisible`, `setWeapVisible`, `setSpellVisible`; `setEnemy` shows the
enemy bar); scripts and `WindowManager` call them, so the HUD enforces the choice once a frame in `onFrame`.

**Design.**
- `MWGui::replacedHudParts(playerInNeutralCell, neutralCombat, rmlui)` (`hudreplaced.hpp/.cpp`, pure) says what the
  combat HUD replaces. The signal for "a Skyrim session" is the player standing in a neutral cell
  (`CellStore::isNeutral`): a Morrowind session never does, with any setting.
- Hidden: the three stat bars, the selected weapon and spell icons (and the name that pops up for them) and the MyGUI
  enemy bar (`setEnemy` returns early). `HUD::updateReplacedParts` runs in `onFrame`; with the two settings off it
  costs two setting reads. When the player leaves a neutral cell (a Morrowind save is loaded) the bars and icons come back.
- Kept: minimap, cell name, active-effect icons, sneak indicator, drowning bar (no replacement), and **the crosshair**:
  the combat HUD's own crosshair (`skyrimcombat.lua`, `combat-crosshair`) draws nothing in any frame (no bright pixel at
  the screen centre in runs that hide the MyGUI one), so hiding the MyGUI one would leave none. For toddhow, and
  for the `hud*` work.

## Tests (first, run red)
- `UiRmlWrappedText` (`apps/components_tests/ui_rml/testuiwrappedtext.cpp`): two strings appended into one list; a
  wrapped element in a real context draws every glyph. **Red against the old engine: 2 of 2** (12 of 36 vertices
  referenced; 18 of 54 indices). Green after.
- `MWGuiHudReplaced` (`apps/openmw_tests/mwgui/hudreplaced.cpp`): **red against a stub: 1 of 3** (the other two pin
  absence: Morrowind and the settings off). Green after.
- Not unit-tested: `HUD::updateReplacedParts` (needs MyGUI); shown in game.

## Stage log (2026-11-15; same hand wrote tests and code; Windows only; local commits on branch `uifix` from `b6bd3c2dcf`)
- Build `I:\Projects\OpenEden-work\combat5\build`. `components-tests` UI selection 351 and `openmw-tests` MWGui 10 pass.
- **Wrap in game** (`tools/ui_check` `lua` variant with a demo mod `combat5\wrapmod`, bitmap font, 300 dp wide text):
  before (combat4 executable) only the first line, cut at "Temple o"; after, all lines of both paragraphs
  (`combat5\wrapcmp.png`). `tools/ui_check` `cpp` and `lua` frames old against new: 155 and 96 pixels over 4, all in the
  frame-rate digits.
- **Skyrim fight** (`neutral_session_check` `n5trade`, Bannered Mare, `neutral combat`, `rmlui`): frames with only the
  new bars (health, stamina, a third bar), the enemy bar "Hulda", the MyGUI crosshair and the minimap; no Morrowind bars
  or weapon icon. With the replacement switched off (temporary build) the Morrowind bars and weapon icon are drawn under
  the new ones. A run that keeps its user folder accumulates screenshots from earlier runs: use the highest numbers.
- **Morrowind unchanged:** `tools/worldobjects_check` 110 objects OK (against combat4); the fixed Balmora frame SHA-256
  `1c5d47ad0b5dc32d` for the parent, this build, this build with `neutral world/actors/combat`, with `rmlui` (parent and
  new) and with all of them on; the Balmora frame with all on shows the Morrowind HUD intact.
- **Not run:** the Linux gate, clang-format 14, clang-tidy, the Lua tests, a FreeType face in a frame, restoring the
  bars after leaving a neutral cell, the full test binaries.

## Decisions and test edits for toddhow
1. The journal's hand line breaks stay because `aLongEntryAndALongEarlierLine...` pins them (above).
2. Weapon and spell icons are hidden too (they show Morrowind items and spells a Skyrim session has no use for); the
   spell work in `hud*` will bring its own.
3. The crosshair stays (above); the combat HUD's crosshair needs a look.
4. Hiding is per frame and does not remember a script's own visibility request; the parts return as visible when the
   player leaves a neutral cell.
5. No existing test was edited.
