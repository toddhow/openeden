# Task: textured frame art for the Skyrim-style HUD (follow-up of task 0244)
Design doc section: decision 0015 (UI system); tasks 0163 (themes), 0244 (the HUD), 0180 (the plain HUD).
Approval: toddhow, 2026-11-16 ("make our OWN art in the spirit of TrueHUD"; no copying of its, Skyrim's or any mod's textures or exact shapes). Task 0244's decision 6 ("no art files: if you want textured frames, say so").
Goal: with `[extension.skyrimse] hud style = true` (plus `[Game] neutral combat` and `[GUI] rmlui`) the player cluster, the boss/enemy bar and the bars over heads are drawn with ornamental frames (pointed end caps, a dark translucent backing, a thin metal border), gradient fills in red, blue and green, a pale damage trail and an emblem frame for the level. If the pictures are missing the plain boxes of task 0244 are drawn as before.
Inherited code touched: none. Edits outside the new files: see "Edits outside the new files".

## The art is original
`tools/hudart/generate.py` draws every picture from polygons, lines and gradients (Pillow, 4x supersampling, no input image, no palette taken from anywhere); the output is deterministic (two runs give identical bytes, checked with md5). The pictures are original to this project and under its licence (GPL-3.0-or-later). The idea (framed bars with pointed ends, a trail, an emblem) is a layout, not a work; nothing of TrueHUD's, Skyrim's or any other game's or mod's art, shapes or constants was used or looked at beyond the screenshot toddhow showed.

Files in `files/data/textures/openeden/hud/` (20 PNG, a few KB together):
- `frame_h<H>_l|c|r.png` for H = 24 (player health), 20 (player magicka and stamina), 22 (boss bar), 14 (an actor's bar), 11 (its small bars): a bar frame as three slices. The left and right caps are 10 px wide with a pointed tip and a diamond stud; the centre is a 4 px tile that repeats. The bar sits `INSET` = 4 px in from top and bottom and `CAP` = 10 px in from each end.
- `fill_red|blue|green|trail.png`: 4 x 32 gradient strips (a gloss line on top, darker towards the bottom).
- `emblem.png`: 48 x 48, an octagon with a double border and four studs.

## Design
- **Decorators.** The renderer draws RCSS decorators from textures (`RenderInterface::LoadTexture` reads the file through the VFS and `readImageFromMemory`), not shaders, filters or masks (decision 0015). A frame is `decorator: tiled-horizontal(left, center, right)` (the positional order of RmlUi's shorthand is left, centre, right; my first version had left, right, centre and drew the right cap stretched across the middle: seen in the first frame, fixed, and pinned by a test), a fill is `decorator: image(...)`, the emblem the same. Paths start with `/` so RmlUi does not join them to the document's (`SystemInterface::JoinPath` strips the slash: the VFS path is `textures/openeden/hud/...`).
- **Why not `tiled-box` nine-slice.** It needs nine images per frame (or an `@spritesheet` in a style sheet, which the Lua module cannot add; a theme or `base.rcss` edit would touch every document). The bar frames only stretch horizontally and have fixed height, so a horizontal three-slice is the nine-slice that matters; the emblem has a fixed size.
- **Fallback.** `env.hasArt()` (the player script checks that every file of `mod.artFiles()` is in the VFS with `openmw.vfs.fileExists`) decides when the documents are built. False, nil or missing: the plain boxes of task 0244 with the same properties (the 25 `UiHudStyle` tests pass unedited). With art the backing, edge and cap boxes are not made (one `style-art-frame` element does it), the end caps are `ART_CAP` = 10 instead of 4 (the boss document's width and margin and the actor bars' left follow), the emblem is 48 and its level text sits 14 px down.
- **Where the setting is.** Nothing new: the module only exists with `hud style` (the package `core.skyrimse.hudstyle` is declared only with it, the REQUIRES tag in `builtin.omwscripts`). No setting added, so nothing for `tools/testprofile`.
- **Not done:** the sneak eye's frame (`skyrimsneak.lua`, task 0227) and the compass: not part of this HUD module; the crosshair is unchanged.

## Edits outside the new files
- `files/data/openeden/hud/skyrimstyle.lua`: the art branch (decorator helpers, `artFiles`, `ART_*` constants, `framedBar`, emblem, widths that follow the cap).
- `files/data/scripts/openeden/skyrim_hud_style.lua`: `env.hasArt` and the `openmw.vfs` require.
- `files/data/CMakeLists.txt`: the 20 PNG names in `BUILTIN_DATA_FILES`.
- `apps/components_tests/CMakeLists.txt`: one line, the new test file.
- `CLAUDE.md`: the changelog row.
No C++ source, shader, SDK, pack or existing test was changed.

## Stage log (2026-11-16; same hand wrote tests and code; Windows only; local commit on branch `hudart` from `2f5ef5cf93`, not pushed)
- **Tests** (`apps/components_tests/ui/testuihudart.cpp`, 11): plain boxes without `hasArt` and with `hasArt` false; a decorator on every frame (3 + 1 + 24) and no backing, edge or cap box; frame heights are bar heights plus the inset and all in the shipped list; fills are gradient decorators; emblem; caps move the bars; boss and actor documents use the art caps; an ally's bar is green; a still frame with art makes no protocol call; every named picture exists and has the size the layout expects (PNG headers); RmlUi's own property parser accepts the strings on a real context. The module was edited first, then the tests were run against the OLD module (HEAD's `skyrimstyle.lua` put back): **8 of 11 failed** (the 3 that passed test the fallback and the still frame). With the new module 10 of 11 passed; the 11th was a wrong expectation of mine (`24px`, the property is `24dp`), fixed. The game then showed the decorator argument order was wrong; the module and the test string changed together. `UiHudArt*`, `UiHudStyle*`, `UiCombatHud*`, `Ui*Theme*`, `UiRml*`: 270 pass.
- **In game** (`I:\Projects\OpenEden-work\combatai\hudart\hud.sh`, the task 0244 scenario: Bannered Mare, `combat test crowd = 3`, fixed frame time, quiet window, sound 0): kept frames in `combatai\hudart\keep`: `player_cluster_enemy_bars.png`, `boss_bar_wide_frame.png`, `boss_bar_trail_actor_bars.png`, `bars_over_heads_trail.png`, a 4x zoom of the player cluster and the art's contact sheet. The log has no image or RmlUi property warning. The first run (wrong argument order) showed the stretched cap; the second is correct.
- **Morrowind unchanged:** the fixed Balmora frame (`--start Balmora --random-seed 7`, frame 600) SHA-256 prefix `1c5d47ad0b5dc32d`.
- **Not run:** the Linux gate, clang-format 14 (the only C++ is the test file, formatted by hand), clang-tidy, the Lua integration tests, a fresh review, UI scale other than 1 (the pictures are 1x and RmlUi scales them with the dp ratio, so they soften at scale 2), the plain fallback in a running game (unit tested only), `slang shaders`.

## Decisions for toddhow
1. The art is 1x pixel art for dp 1; at GUI scale 1.5 or 2 it is bilinearly enlarged (soft). A 2x set could be added under the same names if you want it sharper.
2. The backing alpha (178 of 255) and the colours (gold-grey metal, red, blue, green) are my choice.
3. The sneak eye and the compass frames were not restyled (not in this module).
4. Missing art silently falls back to plain boxes (no log line): say if you want a warning once.
