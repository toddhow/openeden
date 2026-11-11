# 0045 — A UI system to replace MyGUI: state, requirements, options, plan

Date: 2026-11-06 · Author: Claude, at toddhow's request · **Research note, docs only.** Nothing is built, no dependency is added, no code was run. Draft decision: `docs/decisions/0015-ui-system.md`. **Update 2026-11-06: the U0 spike ran; its stage log and verdict (conditional go for RmlUi) are section 13. Sections 1 to 12 are as first written (before the spike; where section 13 corrects a mark, it says so).** **Update 2026-11-10: U1's first slice is built (task 0104, stage log there): two facts from it correct this note: MyGUI has no substitute glyph (an unmapped code point draws nothing; section 15.4's underscore assumption was wrong), and the first glyph to claim a code point wins in MyGUI's font resource.**

Evidence marks: **READ** = I opened the source or the page this session (URL or file given). **RECALLED-VERIFY** = from memory, must be checked before anyone relies on it. Third-party facts were read from web pages through a summarising fetch tool, so "READ" for a web page means the page's text as summarised, not a line-by-line reading of the source; where it matters I say so.

## 1. toddhow's answers (2026-11-06)

1. In the end MyGUI is **completely replaced**; Morrowind's windows migrate last.
2. **CSS-like style sheets only style elements.** The UI is built from a set of **base components** (declarative, composed from small components, reactive values, computed values, children; like React or Roblox's Fusion), styled **by default to match each game's vanilla look**, and **called from the Lua API**. A game pack ships style sheets and theme tokens, component definitions or documents, fonts and art as VFS data, and Lua that wires them to game state through its `core.<game>.*` namespace (draft decision 0014).
3. **Controller navigation can wait**, but the focus model must not be designed so that it cannot be added.
4. **Later, UI must render into the 3D world** (Fallout terminals, in-world screens): render to a texture and route input to it.
5. Never load SWF.

## 2. Current state (measured)

### 2.1 The in-game UI is MyGUI, drawn through VSG
- **READ** `components/vsgadapters/mygui/render.hpp`: `vsgAdapters::mygui::Render` implements `MyGUI::RenderManager` and `MyGUI::IRenderTarget`, with `createVertexBuffer` and `doRender(IVertexBuffer*, ITexture*, size_t count)`. `components/myguiplatform/platform.cpp` registers the shader pair `gui/gui.vert` and `gui/gui.frag` and exposes one `vsg::Node` (`Platform::node()`). The GUI command graph is the last graph written to the swapchain image, after the tonemap (`components/render/CLAUDE.md`, the screenshot paragraphs: "the GUI graph follows [the tonemap graph]"). So today the whole UI is one VSG node fed by MyGUI's batches, drawn to the swapchain only. There is no path to draw it into another image.
- **READ** `components/myguiplatform/scalinglayer.cpp`: UI scaling is a MyGUI layer type (`ScalingLayer::getScaleFactor`); `scaling factor = 1.0` in `files/settings-default.cfg` line 206.
- MyGUI version required by the build: 3.4.3 (**READ** `CMakeLists.txt` line 410, `find_package(MyGUI 3.4.3 REQUIRED)`); `OPENMW_USE_SYSTEM_MYGUI` default ON, with a bundled path in `extern/CMakeLists.txt`.
- Fonts: **READ** `components/fontloader/fontloader.cpp` creates `MyGUI::ResourceTrueTypeFont` (lines 340 and 349) and also reads bitmap fonts (`.omwfont`, line 382 onward). Shipped fonts in `files/data/fonts`: DejaVu LGC Sans Mono, Demonic Letters, Mystic Cards.

### 2.2 The Morrowind windows (counts)
- `apps/openmw/mwgui/`: **83** `.cpp` files, **33,545** lines; headers **7,834** lines (`wc -l`). The design doc says "41k lines" for `mwgui`, which counts both.
- **46** headers declare a class derived from `WindowBase`, `WindowModal`, `WindowPinnableBase`, `BookWindowBase` or `ReferenceInterface` (grep count; approximate window class count).
- `files/data/mygui/`: **62** `.layout` files (4,254 lines in total) and **44** skin/resource `.xml` files, including `openmw_layers.xml`, which defines **22** named layers (Scene, HUD, Windows, Modal, Popup, Notification, Pointer and others, some `overlapped`, some `pick="false"`).
- Largest files (lines): `windowmanagerimp.cpp` 2,561, `mapwindow.cpp` 1,623, `bookpage.cpp` 1,425 (custom typesetter for books and journal), `class.cpp` 1,221, `settingswindow.cpp` 1,151.
- Controller support exists in the engine's window manager (`windowmanagerimp.hpp` has `getActiveControllerWindow`, `cycleActiveControllerWindow`, `setActiveControllerWindow`; a `ControllerButtonsOverlay` and `openmw_controllerbuttons.layout`), so Morrowind's windows already have a controller focus concept that the new system must eventually match.
- `components/widgets/` (box, list, imagebutton, numericeditbox, sharedstatebutton, tags, windowcaption) are custom MyGUI widgets.

### 2.3 OpenMW's Lua UI (what a script author has today)
- **READ** `components/lua_ui/*` (2,167 lines with `apps/openmw/mwlua/uibindings.cpp` at 355 lines and `files/lua_api/openmw/ui.lua` at 322). **Every Lua widget is a MyGUI widget**: `components/lua_ui/widget.hpp` says `WidgetExtension` "extends MyGUI::Widget and its child classes"; `LuaWidget : public MyGUI::Widget, public WidgetExtension`. Layers are MyGUI layers (`components/lua_ui/layers.cpp` creates `MyGUI::OverlappedLayer`s). So the Lua UI does not stand apart from MyGUI; replacing MyGUI replaces its renderer, not only the windows.
- **Widget types** (`ui.lua`, `ui.TYPE`): Widget, Text, TextEdit, Window, Image, Flex, Container. **Layout** is a table: `type`, `layer`, `name`, `props`, `events`, `content`, `template`, `external`, `userData`. **Content** is an array-with-names container with `insert`/`add`/`indexOf`. **Elements** are created by `ui.create(layout)`, have `layout`, `update()` and `destroy()`; changes to the layout table are applied by calling `update()` (or `ui.updateAll()` after a template override, documented as "potentially extremely slow"). Common props found in `widget.cpp`: `position`, `size`, `relativePosition`, `relativeSize`, `anchor`, `visible`, `alpha`, `inheritAlpha`, `pointer`, `propagateEvents`. Text: `text`, `textSize`, `textColor`, `textShadow`, `textShadowColor`, `textAlignH/V`, `multiline`, `wordWrap`, `autoSize`. Image: `resource`, `color`, `tileH`, `tileV`. Flex: `horizontal`, `arrange`, `align`, `autoSize`, `grow`, `stretch`. Window: `caption`, move/resize actions. Events: mouse press/release/click/doubleClick/move, `focusGain`, `focusLoss`, `keyPress`, `keyRelease`, `textInput`, `textChanged`.
- **Templates and resources:** a `Template` has `props`, `content`, `type`. `files/data/scripts/omw/mwui/*.lua` (509 lines) is the interface `MWUI`: `padding`, `interval`, `borders`, `box`, `boxTransparent` and text templates, and a documented way to override them (`I.MWUI.templates.textNormal = ...; ui.updateAll()`). `ui.texture{...}` registers an atlas region.
- **What is missing** (by reading the API and the widget list, not by testing): no style sheets or selectors (look is in per-layout props and a handful of templates); no theme tokens; no reactive state (the author mutates `layout` and calls `update()`; nothing observes anything); no component notion beyond templates and plain functions; no list or grid widget, no scroll view, no checkbox, slider or button (authors compose them from Widget and events); no animation or transition; no keyboard focus traversal or gamepad focus (events exist, `Gamepad` found only in controls docs, not in `ui.lua`); no way to render a tree into a texture; text layout is MyGUI's (single shaper, no bidirectional text); no per-widget localisation hook (authors call the `core.l10n` function themselves; **READ** `components/l10n/messagebundles.hpp` uses ICU `Locale` and message formats, so plural forms and the locale fallback chain already exist).
- Settings pages for script mods use the same widgets (`ui.registerSettingsPage`, `components/lua_ui/scriptsettings.cpp`).

### 2.4 What this means
The Lua API shape (layout tables, content, events, templates, layers) is a reasonable base for a *component call* syntax, and is what existing OpenMW Lua mods already use; but its engine is MyGUI, and MyGUI is also where Morrowind's windows live, so the migration order in toddhow's answer 1 (Morrowind last) means two UI engines coexist for a long time.

## 3. Requirements

From the answers: R1 replace MyGUI completely; R2 style sheets style only, components are base elements in code; R3 defaults match each game's vanilla look, per game pack; R4 components called from Lua through the core's single sandboxed Lua host; R5 reactive values, computed values, children; R6 focus model that can take a gamepad later; R7 render to texture and route input to it; R8 no SWF.

Further requirements derived here (each is a recommendation, not a decision):
- **Localisation:** all text through the existing YAML/ICU system (`components/l10n`); a text value may be a message key plus arguments, re-evaluated reactively when the locale changes; plural/gender via ICU message format stays in the l10n layer.
- **Text and fonts:** UTF-8; TrueType and the existing `.omwfont` bitmap fonts must both work (Morrowind's look uses them); font fallback chains per game (CJK and Cyrillic in Skyrim's official languages is RECALLED-VERIFY); wrapping with CJK line-break rules and combining marks needs a Unicode line-break algorithm (ICU is already a dependency for l10n, **READ** above, so ICU's break iterator is available without a new library, RECALLED-VERIFY that the build links the break-iterator part). **Right-to-left and complex shaping (Arabic, Indic): not needed for the current target games and not promised;** the text engine must sit behind one seam (measure, wrap, generate glyph geometry) so a HarfBuzz and bidi layer can be added later. Do not assume this seam exists in a chosen library (see 4.2).
- **Scaling and accessibility:** a global UI scale plus per-game base resolution, DPI aware (today `scaling factor` only); sizes in layout units, not raw pixels; text size multiplier; high-contrast and reduced-motion token sets (a theme variant); focus visible styling; every interactive component has an accessible name field even though no screen reader is targeted yet.
- **Input:** mouse and keyboard now; a focus tree and navigation events (up/down/left/right/accept/back) defined in the model so gamepad is a source of those events later; text input and IME (SDL text input events) from day one.
- **Animation and transitions:** property transitions declared in style (duration, easing) plus a Lua-driven spring/tween value (`Tween(value)`); all driven by the core's frame `dt` and never by a clock (root guide, determinism).
- **Performance:** retained tree; dirty-tracked; geometry compiled once per changed element and reused; no per-frame allocation in the update and draw paths; batched into few draw calls per layer; no Lua callback per frame unless a reactive value changed. The Lua side allocates when values change, not each frame (a reactive library whose effects only run on change).
- **Save/load of UI state:** UI is not saved (decision 0005: extensions do not touch saves; a game pack owns save chunks). Window positions, scroll positions and tab choices are *preferences*, kept in the settings system, not in the `.omwsave`. A game pack may save its own state in its chunk through its save handler (decision 0013), UI code only reads it back.
- **Sandbox and determinism:** Lua runs only in the core's host (decisions 0003, 0014). UI code can read game state only through `core.*` APIs and must not mutate simulation state during an update; UI events reach simulation only through the same event path script input uses. Layout results must not depend on anything but the tree, the style sheet, fonts, scale and viewport (so a recorded session replays the same).
- **Editor:** an Eden Studio module that loads a pack's style sheets and component documents and re-renders on file change, with a token inspector. It uses the same backend through the editor's Vulkan viewport (task 0014 and the editor module API). Live preview on edit means style sheets and documents are re-loadable at run time (also useful to modders).
- **Modding story:** a modder (a) replaces a style sheet or token set through the VFS (later data directory wins, as for other game data); (b) overrides one component by registering a component with the same name later in load order, or by wrapping it; (c) ships art and fonts in the VFS. A mod must not need C++.
- **Licences:** anything linked must be GPL-3.0-or-later compatible, recorded in `docs/dependencies.md` before it is added (root guide, Guardrails). No proprietary middleware (guardrail), so Scaleform and Coherent style products are out.

## 4. Options

### 4.1 Option 1 — core-owned component layer in Lua over OpenMW's existing Lua UI widgets and renderer
What it is: keep MyGUI-based `lua_ui` as the engine; add in Lua (and a small C++ style resolver) a component library, a style sheet loader that turns selectors/tokens into `props`, and a reactive library. Licence: nothing new (MyGUI is LGPL-2.1, RECALLED-VERIFY; already linked). Maintenance: MyGUI is slow-moving upstream (RECALLED-VERIFY; the build pins 3.4.3, **READ**). Integration cost: lowest.
- For: fastest first result; reuses code that works; all current script mods keep working.
- Against: contradicts answer 1 (MyGUI is not replaced, it is wrapped); MyGUI layout is absolute/relative coordinates plus `Flex` and `Container` written by OpenMW, not a general layout engine; no render-to-texture without rewriting MyGUI's `RenderManager` target model (today it draws to the swapchain only); no real style resolution (cascade, state selectors, transitions) inside widgets; text shaping stays MyGUI's.
- Verdict: a legitimate **first stage inside the migration** (the component library can be written against an abstract element protocol, see 5), not an end state.

### 4.2 Option 2 — RmlUi as layout and render engine, with our component and reactive layer in Lua generating its elements
- **Licence: MIT** — READ `https://raw.githubusercontent.com/mikke89/RmlUi/master/LICENSE.txt` (copyright CodePoint Ltd and Shift Technology Ltd 2008–2014, The RmlUi Team 2019–2026); the README notes some bundled third-party parts under other licences (SIL OFL fonts for the debugger, Zlib for an SDL GPU backend's shader code), none of which we would ship. MIT is GPL-3.0-or-later compatible.
- **Maintenance:** active — the repository page shows 2019–2026 copyright; releases 6.1, 6.2, 6.3 (6.3 "22 Aug", year not shown on the page; I could not read the release dates' years). **READ** `https://github.com/mikke89/RmlUi/releases` summary. C++17.
- **What it is** (**READ** README summary): a C++ UI library based on XHTML1 and CSS2 with some HTML5 and CSS3 (flexbox, animations, transforms, media queries, custom properties). Styling is **RCSS**. Elements are a DOM of tag elements. Data binding (MVC with dirty variables) is built in.
- **Rendering model** (**READ** `https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces/render.html`): the application implements `RenderInterface`: required `CompileGeometry` / `RenderGeometry(handle, translation, texture)` / `ReleaseGeometry`, `LoadTexture` / `GenerateTexture` / `ReleaseTexture`, scissor; optional clip mask, transform, **layers** (`PushLayer`, `CompositeLayers`, `PopLayer`), `SaveLayerAsTexture`, filters, shaders. Vertices are pixel-space indexed triangles; the application builds the projection. So a VSG backend is: compile geometry into vsg buffers once per element, record `RenderGeometry` as draws in a command graph. That matches how `vsgAdapters::mygui::Render` is shaped (per-batch vertex buffers, one pipeline), but with retained, handle-based geometry rather than MyGUI's batch buffers. Cost: a new VSG node type, a Slang `gui2` shader pair, stencil or the mask path for clipping, and a scissor/transform story under VSG's state model (unmeasured).
- **Vulkan:** the README lists Win32_VK, GLFW_VK and SDL_VK backend samples (**READ**; I did not open the backend source, so the quality of that sample and what it covers (layers, filters, shaders) is unverified). The 6.0 release added the filter system and "only the OpenGL 3 renderer implemented it" (**READ**, releases page): filters (blur, drop shadow) would be ours to implement on Vulkan, or left off.
- **Text and fonts** (**READ** `.../interfaces/font_engine.html`): default font engine uses FreeType; `FontEngineInterface` can be replaced (`LoadFontFace`, `GetFontMetrics`, `GetStringWidth`, `GenerateString`, with a `TextShapingContext`); `RMLUI_FONT_ENGINE` CMake option removes FreeType; the docs say the repository has `bitmap_font` and `harfbuzz` samples, so a HarfBuzz engine and a bitmap-font engine are supported shapes. **No RTL note anywhere I read; I could not confirm RTL support.** Bitmap fonts (`.omwfont`) would be our own font engine work or a conversion.
- **Lua plugin** (**READ** `.../lua_manual.html` and releases): exists, "resembles Javascript as closely as possible", exposes elements, documents, contexts, events; 6.3 reworked its data-model support. It is **a separate Lua binding with its own state**: for OpenEden that conflicts with decisions 0003 and 0014 (one sandboxed Lua host, no `sol` types handed around). The plan would **not use it**; our own sol2 bindings over RmlUi's C++ `Element` API would. Which Lua version the plugin targets: not stated on the page (we would not use it anyway).
- **Data binding** (**READ** `.../data_bindings.html`): works on named top-level variables with dirty flags; limitations listed (cannot dirty a single array index; removing elements from inside a `data-for` is undefined). We would **not use RmlUi data binding**: reactivity belongs in our Lua layer (answer 2), which updates element properties/attributes directly. That keeps the reactive model engine-independent.
- **Render to texture:** the README says shader-based rendering includes "render-to-texture and masking support" (**READ**) and the layer interface can render into targets; but for *our* requirement (a whole Context drawn into a world texture) the mechanism is simply that our RenderInterface decides which image it draws into and the app provides a separate `Context` of its own size. I did not find a documented, tested "context to texture" feature, so treat it as **achievable with a render interface that can target an image, unproven**. Input routing is by feeding `Context::ProcessMouseMove` etc. with texture-space coordinates (RECALLED-VERIFY the exact calls).
- **Focus/navigation:** a DOM has tab index/focus; directional navigation for gamepads is not something I read about in RmlUi; RECALLED-VERIFY and, if absent, our component layer implements a spatial focus manager over element boxes (which needs access to layout results, which RmlUi exposes).
- **Integration cost:** the biggest items are the VSG render interface, a font engine for bitmap fonts and fallback, an element-protocol binding to sol2, an RCSS-based default theme per game, a mouse/keyboard bridge from SDL (OpenMW's input manager) and an IME bridge.
- **Risks:** RmlUi's CSS2 box model differs from game UI conventions (e.g. no built-in nine-slice `border-image`? RmlUi has `decorator: ninepatch(...)` in RCSS — RECALLED-VERIFY); dependence on one maintainer-led project (bus factor; mitigated by MIT and the small size, we can fork); RTL unknown; filter support missing on Vulkan.

### 4.3 Option 3 — a custom retained UI engine in C++
Flexbox layout from **Yoga** (**READ** `https://github.com/facebook/yoga` and `.../LICENSE`: MIT, "Copyright (c) Facebook, Inc. and its affiliates"; C++20 main implementation per its README; active — 2,747 commits, 18.9k stars, 106 open issues on the page I read; it is layout only, no text, no rendering), our own tree, style sheet parser/cascade, event dispatch, and text via FreeType (dual licence: FreeType Licence or GPLv2, RECALLED-VERIFY; I could not read it, the fetch was blocked) plus HarfBuzz (**READ** `COPYING`: "Old MIT" licence) and a bidi library (FriBidi is LGPL, RECALLED-VERIFY; SheenBidi is Apache-2.0, RECALLED-VERIFY).
- For: total control; one thing to learn for the owner; no foreign DOM; render-to-texture and VSG integration designed in; text shaping (HarfBuzz) and RTL possible from day one.
- Against: the largest build: style sheet cascade and selectors, state handling, scrolling, clipping, text layout (wrapping, caret, selection, IME), input and focus, animation, rich text, accessibility. A realistic estimate is many months and a lot of owner learning of C++ (the guide notes toddhow is learning C++); every detail RmlUi already has has to be rebuilt and tested, and the repo's rules (full loop for risky areas) make each stage slow.
- Verdict: the fallback if the RmlUi spike fails, and the destination for any part of RmlUi we end up replacing (the font engine first).

### 4.4 Option 4 — others
- **Qt Quick / QML:** Qt 6 is already used for the launcher, wizard and editor. QML offers declarative components, bindings and styles and can render to textures (`QQuickRenderControl`, RECALLED-VERIFY). Reject for the game: Qt licensing (LGPL/GPL for the open modules, RECALLED-VERIFY) is workable on paper, but it would put a large UI toolkit, event loop and Qt's own scene graph (RHI) inside the renderer; the game uses SDL for windowing and VSG for Vulkan; QML's JavaScript engine conflicts with "Lua only" (decision 0003); the shipped game would depend on Qt runtime deployment. I did not read Qt's pages for this note.
- **Proprietary middleware (Scaleform, Coherent, Ultralight, Noesis):** excluded by the guardrail (no proprietary SDKs) and by SWF being out. Not evaluated.
- **Dear ImGui:** MIT (RECALLED-VERIFY; not stated in the FAQ I read). **READ** `https://raw.githubusercontent.com/ocornut/imgui/master/docs/FAQ.md`: it is an immediate-mode API ("issues the whole UI on every update"), built for debug tools though the low-level API can be used for game UI, with "restricted" skinning (colours, sizes, padding, rounding, fonts), keyboard and gamepad navigation built in, and no mention of RTL or complex text. Verdict: **unfit as the game UI** (styling is code, not data; no cascade; every frame rebuilds, which fights "no per-frame allocation"; weak text), but **good for developer tools and the in-game debug overlay** the project already has needs for (Eden Studio-in-game tools). Adding it for that is a separate, small decision; not recommended before the main UI exists, to avoid two UIs.
- **Other retained libraries not evaluated:** Taffy (MIT? Rust, would add a language), Clay (Zlib? C layout library), Slint (GPL-3.0/commercial, RECALLED-VERIFY), Ultralight and Noesis (proprietary). Listed so nobody re-asks; none was opened.

### 4.5 Comparison table

| | 1: Lua layer on MyGUI | 2: RmlUi + our Lua components | 3: custom C++ (Yoga + FreeType + HarfBuzz) | Qt Quick | Dear ImGui |
|---|---|---|---|---|---|
| Licence | MyGUI LGPL (RECALLED-VERIFY) | MIT (READ) | Yoga MIT (READ), HarfBuzz Old MIT (READ), FreeType FTL/GPLv2 (RECALLED-VERIFY) | LGPL/GPL (RECALLED-VERIFY) | MIT (RECALLED-VERIFY) |
| Replaces MyGUI (answer 1) | no | yes | yes | yes | not as a game UI |
| Style sheets (answer 2) | we write a resolver on MyGUI props | RCSS built in, custom properties (READ) | we write | QML styles | no |
| Layout | absolute/relative + Flex written by OpenMW | CSS2 box + flexbox (READ) | Yoga flexbox (READ) | anchors/layouts | immediate |
| Reactive components in Lua (answer 2) | ours | ours (not RmlUi data binding; Lua plugin unused) | ours | QML bindings (not Lua) | no |
| Render to texture (answer 4) | hard (MyGUI target model) | render interface decides target; unproven | designed in | supported, other scene graph | possible |
| Draw through VSG | exists | must write render interface | must write | RHI/Vulkan, separate | backend needed |
| Text shaping / RTL | MyGUI only | swap font engine; RTL unconfirmed | HarfBuzz + bidi possible | yes | no RTL |
| Gamepad focus (answer 3) | existing MW controller code only | needs checking | ours | built-in | built-in |
| Effort | low | medium-high | highest | high, wrong fit | low, wrong fit |
| Main risk | MyGUI stays forever | one external DOM, unverified Vulkan backend | scope | Qt in engine, JS | not a styled UI |

## 5. Recommendation

**Option 2's *shape* with Option 1's *order*, gated by a spike:** build our own base component and reactive layer in Lua against a small, engine-independent **element protocol** (create element of a base kind, set property/attribute, add/remove child, set class/state, subscribe to event, query layout box, request focus), implement that protocol first on RmlUi, and treat RmlUi as replaceable (Option 3's custom engine behind the same protocol is the fallback). Do **not** use RmlUi's Lua plugin or data binding (they would give a second Lua state and a second reactive system; decisions 0003 and 0014 and answer 2 put both in our layer). The element protocol is not a renderer abstraction layer in decision 0002's sense (it does not wrap Vulkan; there remains exactly one renderer); its purpose is to keep Lua component code independent of a third-party DOM. Whether to accept even that much indirection is open question 3.

Why: RmlUi is the only candidate I found that already provides style sheets with custom properties, a flexbox layout, animations, a replaceable font engine and a render interface close to what VSG needs, under MIT, with active releases; the real unknowns (Vulkan backend completeness, render-to-texture, RTL, gamepad focus) are exactly what a spike can answer cheaply before commitment. A Lua layer on MyGUI (Option 1) cannot meet answers 1 and 4; a custom engine (Option 3) is the largest cost and should be taken only if the spike fails or for the parts RmlUi does badly.

## 6. The base component set and the reactive model (design sketch)

### 6.1 Components
Base kinds the library ships, styled by the pack's default theme to the game's vanilla look:
- **Layout containers:** `Frame` (plain box), `Row`, `Column`, `Stack` (z-ordered children), `Grid` (columns/rows/template), with `gap`, `padding`, `align`, `justify`, `grow`, `wrap`.
- **Content:** `Text` (message key or literal, rich-text spans), `Image` (atlas region, nine-slice, tiling, tint), `Icon` (named image from a theme).
- **Controls:** `Button`, `Checkbox`, `Radio`, `Toggle`, `Slider`, `TextInput` (single and multi-line, IME), `Dropdown`.
- **Collections:** `List` (keyed items, virtualised when large), `Grid` of items (inventory), `ScrollView`, `Tabs`.
- **Overlays:** `Tooltip`, `Window`/`Dialog` (title, close, drag, optional modal, layer), `Menu`.
- **Feedback:** `ProgressBar`, `Meter`.
The base components carry **behaviour and structure** (focus, state, events, accessibility name); **appearance** is style-sheet only (answer 2). A pack can define its own composed components in Lua (`Component(function(props, children) ... end)`) and may register the *document* (a declarative tree in Lua tables or a markup file) used to build them.

### 6.2 Reactive model (Fusion-like, fine-grained)
- `Value(x)` (settable), `Computed(function() ... end)` (derived, dependency-tracked, lazy), `Observer`/`Effect(function() ... end)` (runs on change), `Scope` (owns effects and children; destroyed with its component, running cleanups), `ForValues`/`ForKeys`/`ForPairs` (keyed list mapping, reusing child components for equal keys).
- A component function runs **once** and returns elements whose properties are plain values *or* reactive values; a reactive property installs one effect that writes the property when the source changes (no virtual-tree diff). This keeps cost proportional to changes and avoids per-frame allocation.
- Changes are batched and applied at one defined point per frame (UI update phase), so a value changed twice in a frame costs one write; effects must not change values they depend on in the same batch without a cycle guard.
- Game state enters as `core.<game>.*` observables or polled values wrapped by the pack (`GameValue("player.health")`), updated by the core's event path at the UI phase; the UI never reads simulation state at arbitrary times.
- Lifetime: a scope per window; destroying it releases elements and handlers; no global state.

### 6.3 Illustrative Lua sketch (NOT a decided API)
```lua
-- game pack: game_skyrimse/ui/inventory.lua   (sketch only)
local ui   = require('openeden.ui')          -- base components, reactive primitives
local game = require('core.skyrim')          -- pack namespace (decision 0014)
local L    = ui.l10n('Inventory')            -- YAML-backed message lookup

return ui.Component(function(props, scope)
  local items    = game.player.inventory:observe()       -- reactive list
  local filter   = ui.Value('all')
  local selected = ui.Value(nil)
  local shown    = ui.Computed(function()
      return items:get():filter(function(i) return filter:get() == 'all' or i.category == filter:get() end)
  end)

  return ui.Window { class = 'inventory', title = L'Title', onClose = props.onClose,
    ui.Row { gap = 'space.md',
      ui.Column { class = 'categories',
        ui.Tabs { value = filter, tabs = { 'all', 'weapon', 'armor', 'potion' }, label = L } },
      ui.ScrollView { grow = 1,
        ui.List { items = shown, key = function(i) return i.id end,
          render = function(item)
            return ui.Button { class = ui.Computed(function() return selected:get() == item and 'item selected' or 'item' end),
              onClick = function() selected:set(item) end,
              ui.Image { icon = item.icon }, ui.Text { text = item.name } end end } },
      ui.Frame { class = 'details', visible = ui.Computed(function() return selected:get() ~= nil end),
        ui.Text { class = 'title', text = ui.Computed(function() return selected:get().name end) },
        ui.Button { text = L'Equip', onClick = function() game.player:equip(selected:get()) end } } })
end)
```
Style sheet (RCSS-like; `--` names are theme tokens; sketch):
```css
:root              { --font-body: 'SkyrimBold'; --color-text: #e8e0c8; --space-md: 12dp; --panel: url(ui/panel.png); }
.inventory         { decorator: ninepatch(var(--panel)); padding: var(--space-md); font-family: var(--font-body); color: var(--color-text); }
.inventory .item   { height: 28dp; }
.inventory .item:hover, .inventory .item.selected { color: #fff; background-color: #3a342a; }
.inventory .item:focus-visible { outline: 2dp solid var(--color-focus); }
@media (prefers-reduced-motion) { * { transition: none; } }
```
Modder override: ship `ui/theme.rcss` later in load order to change tokens (`--panel`, colours, fonts), or register `ui.override('Button', function(base) return function(props, scope) ... end end)` to wrap or replace one component's structure. Both are VFS-level, no C++.

## 7. Migration path and coexistence

1. **First client: the Skyrim SE game pack's UI** (it has none; new work needs no migration). Likely first windows: HUD (compass, health bars), pause/main menu, inventory list. Also a **developer overlay** for Eden Studio's in-game tools and the existing stats/debug window, because it is small and exercises the pipeline. ImGui is *not* needed for this.
2. **Coexistence with MyGUI:** both are nodes in the GUI command graph. Plan: MyGUI keeps its layers; the new UI is drawn after MyGUI's windows but before its pointer layer, or the pointer is drawn by the new system last (one cursor owner). **Input routing:** one arbiter in the input manager: topmost hit wins by (layer order, then new system vs MyGUI per surface); keyboard focus belongs to one system at a time and moves with mouse click or explicit focus requests; text input events (IME) go to the focus owner. **Draw order:** a fixed band structure (e.g. MyGUI "Windows" band; new "UI" band; MyGUI "Modal/Popup"; pointer) rather than free interleaving. The details need a spike; free interleaving of two retained UIs is the classic source of bugs and should be avoided.
3. **OpenMW's Lua UI scripts** (mods): they keep running on the MyGUI-based `lua_ui` until the Morrowind stage; at that stage `lua_ui` is re-implemented over the new system (same `ui.create`, same widget types, same props mapped to components/styles) so existing mods survive, or are deprecated with notice. Their `ui.updateAll`/template overriding semantic maps onto style tokens.
4. **Morrowind windows last.** What they need: the ~46 window classes' logic separated from MyGUI widgets (today `mwgui` classes hold widget pointers and call `MyGUI::` APIs directly: **not measured how many call sites**, a count belongs in a stage-0 task); a book/journal typesetter replacement for `bookpage.cpp` (1,425 lines; inline images, pages, links); map window with canvas drawing and markers (`mapwindow.cpp`); drag-and-drop item views; tooltips with markup; the existing controller focus navigation (`windowmanagerimp` controller functions). The MW pack ships its theme from the vanilla textures and `.omwfont` bitmap fonts.
5. **Final step:** delete `components/myguiplatform`, `vsgadapters/mygui`, `components/widgets`, `files/data/mygui`, the MyGUI dependency, and the `OPENMW_USE_SYSTEM_MYGUI` options, with `CI/` check that no MyGUI include remains (like `CI/check_no_osg.sh`).

## 8. Render into the 3D world (requirement R7) and what it asks of the backend
- The UI backend must be able to draw a `Context` (a UI tree with its own size, scale and theme) into **an offscreen image** (a vsg `Image`/framebuffer) instead of the swapchain, with its own clip/scissor state and projection; today `vsgAdapters::mygui::Render` draws to the swapchain only (**READ**, 2.1), so this is new.
- The offscreen target is updated once per frame (or on change; most terminals are static): render only when the context is dirty; mipmaps and anisotropic filtering for the world texture; format and colour space: the GUI graph is drawn after tonemap into an sRGB-like target, whereas a world surface is lit in linear HDR, so the texture needs the sRGB-to-linear conversion at sampling (use an `_SRGB` format) and an emissive scale setting.
- **Input:** a ray from the camera (or controller cursor) hits the surface through the physics query (Jolt read-only, extensions may cast rays, decision 0003) or the render scene graph; the hit UV becomes texture-space pixel coordinates fed to that context's pointer events; keyboard focus can be captured by the in-world context while the player "uses" it.
- Needs from the library: multiple simultaneous contexts with independent size/DPI/theme; no global state in the render interface; resizable targets.
- Not needed now. What must hold from day one: **no singleton "the screen"** in the component layer; viewport/scale is a property of the context.

## 9. Risks
1. RmlUi's Vulkan sample may not cover layers, clip masks or filters; our render interface may be a larger piece of work than estimated. (Spike.)
2. Text: bitmap `.omwfont` support, CJK wrapping, IME and RTL are the likeliest sources of surprise. (Text spike: render the three real Morrowind fonts, one CJK and one Cyrillic line, wrap, caret.)
3. Two UIs coexisting (focus, input, draw order). (Fixed bands, one arbiter.)
4. Reactive Lua layer performance and allocation (closures per effect): the cost per UI update must be measured against the per-frame allocation rule; list virtualisation required for big inventories.
5. Dependency bus factor and API churn of RmlUi (major versions 5 and 6 changed the render interface, RECALLED-VERIFY): pin a version, wrap behind the element protocol, keep the right to fork.
6. Scope: toddhow is learning C++; the C++ part should be the small render/input/font bridge, with most work in Lua and data.
7. Mod compatibility for existing OpenMW Lua UI mods during the Morrowind stage.
8. Licence of bundled sample fonts or code: ship none of RmlUi's samples; record licences in `docs/dependencies.md` at adoption.

## 10. Staged plan (rough order; each stage needs its own task spec approved by toddhow)
- **U0 Spike (go/no-go, throwaway; DONE 2026-11-06, section 13):** build RmlUi against the pinned VSG as a standalone test; render one window document with RCSS to the swapchain through a minimal `RenderInterface` on VSG; measure draw calls, per-frame allocations, CPU time for 500 elements; render a Context into an offscreen image and sample it on a quad in the 3D scene; push mouse events through a UV; load a bitmap font. Output: a note with numbers and a go/no-go table, nothing merged into the engine.
- **U1 Element protocol and backend:** the engine-independent protocol, RmlUi render interface and font engine, input/IME bridge, one `Layer` in the GUI graph, coexistence rules with MyGUI (bands, arbiter). Tests for layout results and event routing.
- **U2 Reactive library and base components in Lua:** `Value`/`Computed`/`Effect`/scopes, the component set of 6.1, the default neutral theme, focus model (tab order, spatial navigation hooks), l10n integration, tests with a headless backend.
- **U3 Theme system:** tokens, style sheets from the VFS, overrides, high-contrast and scale variants; Eden Studio preview module with live reload.
- **U4 First pack UI:** Skyrim SE HUD and menus (needs the game pack work of decision 0013 and the descriptor mechanism of 0014 for `core.skyrim.*`); in-game debug overlay.
- **U5 Render to texture and input routing** to world surfaces.
- **U6 Gamepad navigation.**
- **U7 Morrowind migration** window by window, `lua_ui` compatibility, then MyGUI removal.
*(Update 2026-11-10: Track V is closed by toddhow; wherever this note says "after Track V", read "after game-pack stage d" only.)* Ordering constraints: U4 waits on the game pack tier (decision 0013 stages d to f); the root guide's current priority is Track V and the extension system, so U0 is a spare-time research stage, not an interruption.

## 11. Open questions for toddhow (with recommendation)
1. **Approve a U0 spike on RmlUi before committing?** Recommend yes; it is cheap, throwaway and answers the unknowns.
2. **Markup file or Lua tables for component documents?** Recommend Lua tables as the only form at first (one language, no new parser; matches the Lua-first answer); a declarative markup file can be added later for tooling.
3. **Accept an engine-independent element protocol** (small abstraction so Lua components do not depend on RmlUi)? Recommend yes; it is the price of keeping option 3 open and of testing without a window.
4. **Where does the component library live?** As a core feature (`openeden.ui` in Lua, shipped with the engine, registered under the core's host), with packs supplying themes and documents. Recommend core: components are game-neutral and packs are "privileged" only for game namespaces.
5. **Is RTL/complex script a requirement?** Recommend no for now, keep the seam; ask again when a target game's localisation needs it.
6. **ImGui for developer tools?** Recommend not now; decide after U1 whether the new UI is good enough for the debug overlay.
7. **Existing OpenMW Lua UI mods:** keep compatibility through the Morrowind stage or break? Recommend keep (the ecosystem is the point of OpenMW heritage), by re-implementing `lua_ui` over the new system.
8. **In which stage is the first UI pack work scheduled relative to Track V and the game-pack stages?** Recommend after game-pack stage d (the tier exists) and not before Track V closes.
9. **Default look source:** the vanilla look needs textures and fonts from the player's installed game; themes ship only layout and tokens, and read art from the player's data (guardrail: no game assets in the repository). Recommend confirm.

## 12. What I could not verify
- RmlUi's release dates' years, its Vulkan backend source and what it supports, its RTL support, gamepad/directional navigation, the exact context-to-texture mechanism, `ninepatch` decorator syntax, and its Lua version. I read only the project's README, release page, licence, render/font-engine/data-binding/Lua manual pages (through a summarising fetch).
- FreeType's licence page was blocked; FriBidi, SheenBidi, MyGUI, Qt, Dear ImGui licences and Qt's render-to-texture are from memory.
- I did not build or run anything; no measurement of draw calls or cost exists. The counts in section 2 are `wc` and grep counts of this checkout; the "46 window classes" is a header grep.
- The number of `MyGUI::` call sites in `mwgui` (not measured).
- Whether the ICU break iterator is linked in OpenEden's builds.

Sources read: https://github.com/mikke89/RmlUi, https://github.com/mikke89/RmlUi/releases, https://raw.githubusercontent.com/mikke89/RmlUi/master/LICENSE.txt, https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/interfaces/render.html, .../interfaces/font_engine.html, .../data_bindings.html, .../lua_manual.html, https://github.com/facebook/yoga and its LICENSE, https://raw.githubusercontent.com/ocornut/imgui/master/docs/FAQ.md, https://raw.githubusercontent.com/harfbuzz/harfbuzz/main/COPYING.

## 13. U0 spike, 2026-11-06 (stage log)

Started at toddhow's word of 2026-11-06 ("Can we also start on the UI system"), earlier than decision 0015's schedule (after Track V and game-pack stage d). Nothing in the repository changed except this note, decision 0015's status line and one changelog row. **All code is throwaway and off-repo** in `I:\Projects\OpenEden-work\uispike\` (own CMake, never added to the repo's CMake, not committed): `harness/` (about 1,470 lines in total: `testui.hpp`, `layout_main.cpp`, `vsgrml.hpp` the VSG render interface, `vsgmain.cpp`, `protocol_main.cpp`, `lua/reactive.lua` 169 lines, `lua/app.lua` 71 lines), outputs in `out/`. Marks: **MEASURED** = run here; **READ** = source or licence file opened; "not verified" items are listed in 13.9.

### 13.1 Verdict

**CONDITIONAL GO for RmlUi as the first backend behind the element protocol; the custom fallback (Yoga + own text) is not needed on this evidence.** Everything the spike was asked to show worked, with three findings that change the plan and a list of things it did not reach. The conditions:

1. **The document is the layout unit.** A change to one text in a 200-row document re-lays out the whole document (1.15 ms Update + 0.49 ms Render and about 6,200 allocations, against 0.026 ms and 26 allocations when the same text lives in a separate small document, 13.7). So: live values (HUD numbers, timers) go in their own documents; lists are virtualised (about 30 to 50 rows); the Lua component API must say which component owns a document (a `Window`/`Hud` root). The "fine-grained, no churn" promise of 6.2 holds for the Lua layer (one effect run and one protocol call per change, 13.7) but not for text edits inside a big RmlUi document.
2. **One `RenderInterface` for all contexts, with a draw list per context** (13.5). Two interfaces plus a shared style sheet silently lose decorator and sprite-sheet textures in the second context.
3. **The VSG backend needs a stencil clip mask and a layer stack, or themes must not use** `border-radius` with `overflow`, `box-shadow`, `filter` and `mask-image` (13.4: two of them draw wrongly, not just missing). That is U1 work to size; vanilla-look themes (nine-patch art, flat colours, text) need none of it.
4. Not verified and to be settled in a follow-up before U1 (13.9): `.omwfont` bitmap fonts, IME composition, `nav` gamepad focus, Linux/GCC, GPU time, sRGB against the HDR world surface.

### 13.2 What was fetched (all into the spike folder only)

| Item | Version | Source | SHA-256 | SHA-512 |
| --- | --- | --- | --- | --- |
| RmlUi | **6.3** (release of 2026-08-22, GitHub API `published_at`) | https://github.com/mikke89/RmlUi/archive/refs/tags/6.3.tar.gz, 3,977,268 bytes | `d977298bb6147610e5984d5db85ddf284020d655a8713913f6982074f1dbdede` | `b66d2a3621ca2690d6e826661203fc3a4babe16243ed01b2f884d149f5b7ee598241f1f743262c7a8fbf8ae98641d5c5526fc63d8a6283638b6dc021d23e103e` |
| FreeType | **2.14.3** (tag of 2026-03-22; 2.15.0 of 2026-10-11 is under two months old, not used) | https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.gz, 4,134,916 bytes | `e61b31ab26358b946e767ed7eb7f4bb2e507da1cfefeb7a8861ace7fd5c899a1` | `e0cbdb2ba64ce0add42b02934e9253b942d4ead901a0c29610c81578d1e17d6c01400461f818c6b9dc87def429ddb6eabe840b0d77f1433308f1fccb776c0c80` |
| Liberation Fonts (test font only, not for shipping) | 2.1.5 | https://github.com/liberationfonts/liberation-fonts/files/7261482/liberation-fonts-ttf-2.1.5.tar.gz, 2,385,008 bytes | `7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0` | `b9f178fae43c9214ba6de36c6da613fcf59bd98084058afaaa9151d00f2406942bc360430be0a8f35ea6b5529146eef371c7cf75bdd7910403d449254f943fed` |

GitHub archive tarballs have changed their hashes before: adoption should pin the SHA-512 the repository uses and prefer a release asset or a vendored copy. No third-party source or licence text was committed.

**Licences (READ the files in the unpacked archives):**
- **RmlUi: MIT** (`LICENSE.txt`: "Copyright (c) 2008-2014 CodePoint Ltd, Shift Technology Ltd, and contributors; (c) 2019-2026 The RmlUi Team, and contributors"). `Include/RmlUi/Core/Containers/LICENSE.txt` lists the bundled third-party parts of the core, "all MIT licensed": robin_hood (Martin Ankerl) and itlib flat_map/flat_set (Chobolabs, Borislav Stanimirov). With `RMLUI_THIRDPARTY_CONTAINERS=ON` (the default) they are compiled in. The samples and `Backends/` carry other notices that we would not ship. MIT is GPL-3.0-or-later compatible.
- **FreeType: dual licence, choose one** (`LICENSE.TXT`): the FreeType Licence (`docs/FTL.TXT`, BSD-like with an advertising clause that requires crediting the FreeType project; "compatible to the GNU General Public License version 3, but not version 2") or GPLv2 "any later version can be used also" (usable as GPLv3). Its `bdf`/`pcf` drivers are X11-style, the gzip module is zlib-licensed, and four autofit files carry HarfBuzz "Old MIT" code. **FreeType is not a new dependency on Windows**: `freetype.lib` and `freetype.dll` are already in the vcpkg set and in `build-jolt` (MyGUI uses it), so adopting RmlUi adds one library, not two (the spike built its own copy only because the task said to fetch one).
- **Liberation Sans 2.1.5: SIL OFL 1.1** (`LICENSE`: "This Font Software is licensed under the SIL Open Font License, Version 1.1"). Used only for the spike. Not shipped.

### 13.3 Step 1: builds with MSVC 2022 and runs headless (MEASURED)

- RmlUi 6.3 and FreeType 2.14.3 build with MSVC 19.44 (VS 2022 BuildTools), **`/std:c++20`** for every RmlUi file (200 compile lines in `build.ninja`), RelWithDebInfo, static libraries, Ninja, no precompiled headers, **no compiler warnings** in the log. Build time on this 24-thread machine: **FreeType 2.2 s, RmlUi 15.8 s** from scratch (configure included). RmlUi core is 71,175 lines (`Source/Core` + `Include`).
- Size: an empty MSVC executable is 9,728 bytes; the headless layout test linked with RmlUi and FreeType is **4,610,048 bytes** (the VSG harness 4,908,544), so the added code is about **4.6 MB** at `/O2 /Ob1` with debug info in a separate PDB (an upper bound for a release build). For scale: `MyGUIEngine.dll` in the build folder is 1.35 MB and `openmw.exe` 80 MB.
- Headless run (`layout.exe`, a null `RenderInterface`; `out/layout.txt`): a document + RCSS (`@spritesheet`, custom properties `--text` with `var()` on `body`, flexbox rows and columns with `gap` and `flex: 1 1 0`, a `ninepatch` decorator, `img`, `button` with `:hover`/`:active`/`:focus-visible`, `input type="text"`, a list with `overflow-y: auto`) and a 30-row list (212 elements counting text nodes). Border boxes, in pixels:

| Viewport, dp ratio | window (x, y, w, h) | list (w, h), scroll content | item0 height | `equip` button width | `#desc` height |
| --- | --- | --- | --- | --- | --- |
| 1280x720, 1.0 | 40, 30, 576 x 416 | 264 x 372, content 810 | 26 | 180 | 100.8 |
| 1920x1080, 1.5 | 60, 45, 864 x 624 | 396 x 558, content 1215 | 39 | 270 | 129.6 |
| 1280x720, 2.0 | 80, 60, 1152 x 832 | 528 x 744, content 1620 | 52 | 360 | 172.8 |

  (First run; the final run in `out/layout.txt` adds a one-line Cyrillic element, so the details column and `equip` are one line lower, 20 px at dp 1.) Every number scales exactly with the dp ratio, and layout is a pure function of the tree, style sheet, fonts, viewport and ratio, which is what the determinism rule of section 3 needs. Scrolling the list to `scrollTop` 130 dp moves item5 to y = 71 (dp 1), 106.5 (dp 1.5), 142 (dp 2). Corrections to this note's marks: **`ninepatch` exists** (READ `Source/Core/DecoratorNinePatch.cpp`; RCSS `ninepatch(outer, inner, scale)` over an `@spritesheet`, used in the render below), **custom properties and `var()` work** (RmlUi 6.3 changelog; used here), and a **`nav` property exists** (`nav-up|right|down|left: none|auto|horizontal|vertical|tree-order`, READ `StyleSheetSpecification.cpp:378`); `nav` was not run.

### 13.4 Step 2: a `RenderInterface` through VSG (MEASURED)

**Backend** (`harness/vsgrml.hpp`, about 250 lines are the backend proper; the rest is a PNG writer and the read-back): VSG 1.1.16 (the repo's built `vsg-17rd.dll`, headers from `build-jolt/extern/fetched/vsg`), one `vsg::GraphicsPipeline` (GLSL 450 compiled at run time: pixel-space position, premultiplied RGBA8 colour, uv; push constants mat4 + translation; blend ONE / ONE_MINUS_SRC_ALPHA; dynamic viewport and scissor; no depth), one `vsg::VertexIndexDraw` per compiled RmlUi geometry (compiled once through the `CompileManager`, reused every frame), one `vsg::BindDescriptorSet` per texture, and a `UiNode : vsg::Command` that records the frame's draw list (`RenderGeometry` only appends `{geometry, translation, texture, scissor, transform}` to a reserved vector). Implemented: indexed geometry, TGA textures (premultiplied on load) and generated glyph atlases, scissor, `SetTransform` (a rotated button draws correctly), alpha blending. **Not implemented: clip masks (stencil), layers, filters, shaders.** The window is a real `vsg::Window` created at (-30000, -30000) (the game's quiet position); a second `RenderGraph` draws into an RGBA8 640 x 480 `vsg::Image`, which the window then samples on a perspective quad (the in-world terminal). Both images were read back and written as PNGs.

**Frames viewed (all in `I:\Projects\OpenEden-work\uispike\out\`)**: `screen_dp1.png` (the window at dp 1.0 with the in-world quad on the right), `screen_dp15.png` (dp 1.5), `screen_termdp125.png` (terminal at dp 1.25), `rtt_dp1.png` and `rtt_ok_*.png` (the offscreen image), `screen_features.png`. They show the nine-patch panel, Liberation Sans in two weights, the list with scrollbar, hover and selected rows, button states, a text input with a caret, the icon with its transparent corner, wrapped paragraph text, and Latin-1, Greek and Cyrillic glyphs (`Привет, мир! Ёж ß ñ Ω`). No rendering fault in the normal scene.

**Numbers, a window of 212 elements (30 list rows, 108 draw calls)**, three runs of the same executable (default configuration: two interfaces and two style sheets), 300 frames each after 60 warm-up frames, RTX 4070, 1280x720, `VK_PRESENT_MODE_IMMEDIATE_KHR`; times in ms (runs agree within a few percent; `out/vsg_final_run1..3.txt`):

| Scenario | Update() | Render() (build draw list) | VSG record + submit + present | exe-side allocations per frame |
| --- | --- | --- | --- | --- |
| nothing changes | 0.005 | 0.041 | 0.23 (p95 0.28 to 0.31) | **0** |
| list scrolls 2 px per frame | 0.004 to 0.005 | 0.044 to 0.046 | 0.24 | 0.2 (max 4); 30 geometries recompiled over 300 frames |
| mouse moves over rows (hover changes) | 0.014 to 0.016 | about 0.05 | 0.25 | 17.7 (max 26) |
| 1 of 30 row texts changes per frame | **0.30** | **0.22** | 0.32 | **1,620**; about 14.5 geometries recompiled per frame (15.9 to 16.9 ms of `compile()` calls over 300 frames) |
| screen and terminal contexts both, nothing changes | 0.017 | 0.079 | 0.25 | 0 |

Per frame: **108 Rml draw calls**, 2,236 vertices, 3,456 indices, 2 scissor changes, 48 texture binds (they alternate between the white pixel, the icon and the glyph atlas: RmlUi does not sort by texture). The first 60 frames compiled 217 geometries and 12 textures; `compile()` costs 0.005 ms each on the CPU (the upload's GPU time was not measured). **"Allocations" counts the exe's own `operator new` (RmlUi, linked statically, and the harness); the VSG DLL has its own allocator, so VSG's recording is not in the 0.** The backend's record path reuses a reserved vector and allocates nothing per frame. The static scene meets the "no per-frame allocation" rule for the UI side; a changed text does not (13.7 for the cause).

**Feature probe** (`features.rml`, `screen_features.png`, RmlUi's log): **two features draw WRONG without backend support, not merely missing**: (A) `border-radius` + `overflow: hidden` draws a black box and the clipped child spills outside it (RmlUi calls `EnableClipMask`/`RenderToClipMask`, which my interface leaves at the base class's no-ops); (B) `box-shadow` draws a **solid white rectangle** (its blurred shadow is produced through layers). Missing but warned in the log: `filter: blur()` and `drop-shadow()` ("Could not compile filter"), `mask-image: radial-gradient()` and `decorator: linear-gradient()` ("Could not generate decorator element data"; these need `CompileShader`). Worked: `transform: rotate()`, `opacity: 0.5`, `border-radius` on a plain box with a border, scissor clipping of the list. (An earlier `background: linear-gradient(...)` line was a syntax error of the test: RmlUi's gradient is a decorator.) A faithful backend therefore needs a stencil clip mask and a layer stack (and optionally shader/filter support). RmlUi ships Vulkan backend samples (READ the file names `Backends/RmlUi_Backend_*_VK.cpp` and `Backends/RmlUi_Vulkan/`; **their code and completeness were not read**), so porting is likely cheaper than inventing, but unsized.

**VSG pitfalls hit (worth recording for U1):** (1) `viewer->compileManager->compile(node)` on a graph with **no `vsg::View`** compiles nothing and still returns success: descriptor-set handles stay null and `vkCmdBindDescriptorSets` crashes inside the driver. `viewer->compileManager->add(device, vsg::ResourceRequirements())` once after `viewer->compile()` fixes it (the game's graphs have views, so this is a trap for overlay-only graphs such as a terminal context). (2) RmlUi calls `ReleaseTexture`/`ReleaseGeometry` for objects the frame's draw list and frames in flight still use: release must be deferred (the spike frees six frames later; a real backend should use the engine's deletion queue). (3) VSG chose a `B8G8R8A8_SRGB` swapchain here (format 50); the spike forces `B8G8R8A8_UNORM` so UI colours equal the MyGUI path's. How a world texture is decoded (an `_SRGB` view) is a U5 matter.

### 13.5 Contexts, interfaces and style sheets (MEASURED, a finding for R7)

RmlUi allows a `RenderInterface` per context (`CreateContext(name, size, &interface)`). With **two interfaces** (one per target) and **the same style sheet file loaded in both**, the second context silently draws **without its decorator textures**: no log line, the terminal's nine-patch panel is simply absent (`out/rtt_FAIL_sharedsheet_separate_interfaces.png`), while icons (per-element textures) and text are fine. Cause (inferred from the behaviour; the source path was not traced): RmlUi caches style sheets by file name and the sheet's sprite-sheet texture belongs to the first interface's render manager. Working configurations, each viewed: **two interfaces and two style-sheet files** (`rtt_ok_separate_sheets.png`; the configuration of every number above) and **one shared interface for both contexts with a draw list per context and the sheet shared** (`rtt_ok_shared_interface.png`; in the harness a `sink` redirects `RenderGeometry` while the terminal renders; the descriptor sets of one pipeline layout bind in the other pass's pipeline because the layouts are compatible). **Recommendation: one `RenderInterface` object for the whole application**, owning all textures and geometry, with the target chosen per `Context::Render` call. That also lets the editor preview and in-world surfaces share fonts and atlases, and one mod style sheet serve all contexts.

### 13.6 Step 3: input routing (MEASURED)

Synthetic events through `ProcessMouseMove/ButtonDown/ButtonUp/Wheel`, `ProcessKeyDown`, `ProcessTextInput` (`out/vsg_final_run1.txt`, "input routing"): moving onto `equip` makes it the hover element with `:hover` set; button down sets `:active` and focus; button up delivers `click` to the listener; **Tab** moves focus to the text input with `:focus-visible` set; the wheel over the list changes `scrollTop` (200 to 212); clicking a row gives it the `sel` class and updates the details title through the listener; typed text (`x` and the UTF-8 `é`) appears in the input.

**In-world terminal:** the 640 x 480 terminal context is drawn on a perspective quad (45 degrees vertical field of view, yawed -25 degrees, 2.6 m away). The quad's clip matrix maps a terminal pixel to the screen; inverting it (a 2 x 2 solve for the plane z = 0, the ray-plane intersection in the quad's own space) maps a screen pixel back. The `equip` button centre, terminal pixel (518.0, 300.0), projects to screen (1184.79, 396.35) and picking that pixel returns (518.0000, 300.0000), error 0.00000 px; a pick left of the quad misses. Feeding the picked coordinates to the terminal context gives it hover (`equip`, `:hover`) and a click delivered to **its** listener only (the screen context's log was untouched). **The pick is analytic (a matrix inverse), not a Jolt or scene-graph ray hit**; going from a world ray to the plane is U5 work and uses the engine's picking, which was not exercised.

### 13.7 Step 4: the element protocol and a Lua reactive layer (MEASURED; a sketch, not the library)

`protocol.exe` links a standalone LuaJIT state (vcpkg's `lua51`) **only in this harness** and exposes `ui.{byId, create, class, text, attr, on, move, remove}` (handles, not pointers) over RmlUi elements; RmlUi's own Lua plugin and data binding are not used. `lua/reactive.lua` (169 lines) has `Value`, `Computed` (lazy; a recompute that gives the same value keeps its version, so readers are not woken), `Effect`, `Scope` (cleanups, child scopes), `bind` (a plain or reactive property; an unchanged value never reaches the protocol), `ForKeys` (keyed list: a row's scope and element are reused for an equal key; only new, removed or moved rows touch the protocol) and one `flush()` per frame that runs dirty effects once each in creation order. `lua/app.lua` (71 lines) builds the inventory list: per row a reactive class, a reactive count and a click handler (about 12 protocol calls per row).

Update cost (headless RmlUi with a null render interface that builds the draw list; ms; Lua calls are cached function calls, not compiled chunks; "allocations" are the exe's `operator new`, Lua bytes counted with the collector stopped; `out/protocol_rows{30,100,200,1000}.txt`):

| List rows | idle frame: Lua flush / Update / Render | **1 text changes**: Lua flush / Update / Render, exe allocations | **selection moves** (per-row Value): Update / Render | build all rows | keyed remove 1 + insert 1 |
| --- | --- | --- | --- | --- | --- |
| 30 | 0.0001 / 0.004 / 0.037 | 0.002 / 0.27 / 0.10, 1,404 | 0.015 / 0.039 | 1.8 ms | 0.44 ms |
| 100 | 0.0001 / 0.011 / 0.109 | 0.002 / 0.62 / 0.25, 3,367 | 0.044 / 0.12 | 1.4 ms | 1.07 ms |
| 200 | 0.0002 / 0.023 / 0.23 | 0.004 / 1.15 / 0.49, 6,168 | 0.083 / 0.22 | 2.3 ms | 1.96 ms |
| 1000 | 0.0008 / 0.13 / 1.10 | 0.010 / 5.96 / 2.72, 28,576 | 0.43 / 1.16 | 10.2 ms | 9.06 ms |

Idle allocations: 0.1 per frame exe-side and 0.5 Lua bytes at every size.

- **The Lua layer is not the cost**: one changed `Value` gives **exactly 1 effect run and 1 protocol call** (a selection change: 2 and 2), 0.002 to 0.01 ms of Lua and about 0.5 KB of Lua allocation. (An earlier version of the harness reported 301 bytes per idle frame and 1.3 KB per change: that was its own `luaL_dostring`; fixed and rerun.) One shared "selected key" Value makes every row's computed recompute (200 recomputes, 0.086 ms and 44 KB of Lua allocation per selection change at 200 rows): the idiom is a per-row Value or a cutoff.
- **RmlUi's layout is the cost**: a text change costs time and allocations proportional to the rows of the document, and `SetText` on the existing text element is no cheaper than `SetInnerRML` (1.14 against 1.13 ms at 200 rows). A class change (no layout) costs 6.9 allocations at 200 rows. **Experiment** (200 rows loaded, the same change in): (a) a fixed-size `position: absolute` div **inside the same document**: Update 1.16 ms, 6,177 allocations (no gain); (b) a fixed-size div in a **separate document of the same context**: Update 0.026 ms, Render 0.23 (the other document's draw list is rebuilt each frame anyway), 26 allocations; (c) an auto-size div in a separate document: 0.026 ms, 35 allocations. So layout scope is the document: the first condition of 13.1.
- Idle cost: `Render()` rebuilds the whole draw list every frame (0.04 ms at 30 rows, 0.23 at 200, 1.1 at 1,000: about 1 microsecond per row), without allocating. A context that did not change could skip `Update` and `Render` and re-record its stored list (the VSG side already re-records a stored list); not tried.
- Rebuilding all 200 rows (destroy and create) takes 4.8 ms, 2.5 times the keyed diff.
- Memory: private bytes 1.0 MB at start, 2.8 MB after RmlUi init and three font faces, 3.2 MB after the first document; Lua memory +954 KB for 200 rows (about 4.8 KB a row with 2 effects, 2 computeds and closures; +4.65 MB at 1,000 rows).
- What the sketch does not do: components with children and prop merging, `Tween`, focus and navigation, l10n, error isolation of effects, ordering between a parent's and a child's effects, readable cycle errors (the guard is a loop count of 100).

### 13.8 Against the note's criteria

| Criterion | Evidence | Status |
| --- | --- | --- |
| VSG render interface, acceptable draw calls | 108 draws, 0.04 ms to build, 0.23 ms to record and submit, 0 allocations when static | met for the plain feature set; clip mask, layers, filters, shaders not built |
| No per-frame allocation | 0 static, 0.2 scrolling, 17.7 hover, about 1,600 per text change of one row in 30 | met only while no text in the document changes (condition 1) |
| Text | FreeType, Liberation Sans, wrapping, Latin-1, Greek, Cyrillic and UTF-8 text input drawn | met for these; **CJK, bitmap `.omwfont`, RTL, IME not run** |
| Render to texture and input routing | context drawn into an image, sampled on a perspective quad, picked back to 0.00000 px, events reach only that context | met (the pick is analytic) |
| Several contexts, no global "the screen" | per-context size and dp ratio (1.0 and 1.5 on the screen, 1.25 on the terminal) work; a shared sheet breaks per-context interfaces | met with one shared interface |
| Binary size and build time | about +4.6 MB, 15.8 s from scratch for RmlUi, 2.2 s FreeType (MSVC, 24 threads) | acceptable |
| Licence | MIT plus bundled MIT containers; FreeType FTL or GPL | OK; a credit line for the FTL, or take the GPL option |
| Focus and gamepad | Tab order and `:focus-visible` run; `nav: auto` exists in the source, **not run** | open |
| RTL | the `dir` attribute and `-rmlui-direction` property reach `TextShapingContext` (READ `Element.cpp:1790`, `ElementText.cpp:398`, `TextShapingContext.h:13`); a grep of `Source` and `Include` finds no shaping or bidi code | no RTL shaping; the font-engine seam is where it would go |

Other risks: RmlUi does not sort draws by texture (48 binds for 108 draws, harmless here); the second-context style-sheet failure is silent; per-element geometry means many small draws for big UIs, so virtualisation keeps it small; RmlUi is a DOM with its own text-node types, so the element protocol must hide `ElementText`; bus factor as in note 9.

### 13.9 Not verified (so nobody assumes it)

`.omwfont` bitmap fonts (corrected in 14.2: `.omwfont` is TrueType, the bitmap case is `.fnt`; RmlUi's `bitmap_font` sample is 591 lines under `Samples/basic/bitmap_font` and a replaceable `FontEngineInterface` exists; **not run**); CJK (no CJK font was used) and fallback chains beyond one fallback face; IME composition (only `ProcessTextInput` ran; the 6.3 changelog lists IME support in the SDL backends); `nav` gamepad navigation; sRGB handling of a world surface under HDR (the swapchain was forced to UNORM); Linux/GCC `-Werror` (Windows only); GPU time and the transfer cost of geometry uploads (only the CPU time of `compile()`); the real `Render::Engine`, its deletion queue and the editor's `SceneHost` (the spike used a plain `vsg::Window` and `CompileManager`); a Jolt ray hit for the in-world pick; the harness window's taskbar and focus behaviour at (-30000, -30000) (no window was seen by me, but no window-event hook was run either); multi-threaded recording and calling RmlUi from the update workers; the real component library, l10n and `lua_ui` compatibility; override order of mod style sheets; text-input editing features (selection, clipboard); memory of a full game UI. The harness calls `viewer->update()` twice per frame (a harmless leftover) and keeps vertex data in host-visible memory through the compile manager (not tuned).

### 13.10 Revised staged plan

The note's stages stand, with changes. **U0b (small, before U1; needs toddhow's approval):** the unverified items that can block the choice: a bitmap-font `FontEngineInterface` over a real `.omwfont`, a Linux build, and the clip mask plus layer stack sized against RmlUi's own Vulkan backend (same MIT repository; read its code and licence before copying anything). **U1** gets the rule "one `RenderInterface`, a draw list per context, deferred release through the engine's deletion queue" and the document-scope rule in its acceptance criteria (a test that a HUD value changing every frame does not live in a big document). **U2**'s base set adds `Hud`/`Document` as the layout-root component, requires `List` to be virtualised from the start, and documents the per-row-Value idiom. U3 to U7 unchanged. Decision 0015's schedule (after Track V and game-pack stage d) was set aside by toddhow only for the spike; U0b and U1 stay on that schedule unless he says otherwise.

### 13.11 Decisions for toddhow

1. **Accept the conditional GO** (RmlUi as the first backend behind the element protocol, the custom engine as the fallback if U0b fails)?
2. **Accept "the document is the layout unit"** as a design rule for the component library (live values in their own documents; lists virtualised)?
3. **Themes and effects:** build the stencil clip mask and layer stack (U1 cost, unsized), or forbid `box-shadow`, `filter`, `mask-image` and rounded-corner clipping in themes for now?
4. **Approve U0b** (bitmap fonts, Linux, clip mask and layers, IME and `nav` runs) as the next spare-time stage?
5. **FreeType licence option:** FTL with a credit line, or the GPL option; and reuse the vcpkg FreeType rather than a second copy.
6. **Schedule:** U1 onward after Track V and game-pack stage d (as decision 0015 says), or earlier?

## 14. U0b spike, 2026-11-09 (stage log)

toddhow asked for U0b ("verify the unverified items that can block choosing RmlUi"). Docs only in the repository: the code is throwaway in `I:/Projects/OpenEden-work/uispike/u0b/` (not committed, nothing added to CMake, no dependency added), the Morrowind fonts were read from the user's install and nothing derived from them is committed (the picture of rendered text stays in the work folder). RmlUi 6.3 and FreeType 2.14.3 are the U0 builds. Everything below is **measured** unless marked otherwise.

### 14.1 Verdict

**The conditional GO stands and its last two font and platform conditions are met.** Bitmap fonts, the Linux build, IME hooks and `nav` all work. One thing is worse than section 13 assumed: **RmlUi's own Vulkan backend has no clip mask, no layers, no filters and no shaders** (its README table says so), so the stencil clip mask and layer stack have no Vulkan reference to port; the reference is the OpenGL 3 renderer. Sizing is in 14.5. Decision 3 of section 13.11 (forbid `box-shadow`, `filter`, `mask-image` and rounded-corner clipping in themes for now) is therefore **more** attractive, not less.

### 14.2 A correction: `.omwfont` is not a bitmap format

Section 13.9 and the U0b item spoke of "bitmap fonts over a real `.omwfont`". That is two different things in OpenEden's code (`components/fontloader/fontloader.cpp`, `FontLoader::loadFont`):

- **`.fnt` plus `.tex` is the bitmap format.** Morrowind's own fonts. `Fonts_Font_0` and `Fonts_Font_2` of the ini name one; `loadBitmapFont` reads it. Layout read from that function: a float font size, two ints that must be 1, a 284-byte name, 256 glyph records of 14 floats (an unused float, four corner points as normalised UVs, width, height, left kerning, right kerning, ascent); the `.tex` is an int width, an int height and raw RGBA8. The glyph index goes to a code point through the game's code page (CP437 for a Windows-1252 install, with a Polish swap table for 1250).
- **`.omwfont` is a MyGUI XML wrapper around a TrueType file** (`ResourceTrueTypeFont`: `Source`, `Antialias`, `Resolution`, `TabWidth`, `Code range` and `Code hide` lines). OpenEden ships three (`files/data/fonts/`: the mono debug font and the two TrueType fallbacks for the Morrowind fonts). It is loaded with a `Size` of `[GUI] font size` and a `Resolution` scaled by the window and book scale.

So there are two items, both checked: 1a (a bitmap `FontEngineInterface`) and 1b (a `.omwfont` through RmlUi's default FreeType engine).

### 14.3 Item 1a: a `FontEngineInterface` over `.fnt`/`.tex` (MEASURED)

Prototype: `u0b/bitmapfont.hpp`, 310 lines, written from `fontloader.cpp` and RmlUi's public `FontEngineInterface.h` (the RmlUi `bitmap_font` sample, 591 lines, was not copied or followed beyond learning that a callback texture is how an in-memory image becomes a `Texture`). It parses the file, premultiplies the texture (RmlUi's `GenerateTexture` takes premultiplied alpha: `RenderInterface.h`), builds a code point map through the CP437 table, and implements the whole interface for its own families. **It delegates every other family to RmlUi's default engine**: `Initialise()` first creates the default engine, the prototype takes it with `GetFontEngineInterface()` and installs itself with `SetFontEngineInterface()`, and a set of its own handles tells whose a `FontFaceHandle` is. The loaded faces are `Magic_Cards_Regular`, `century_gothic_font_regular` and `daedric_font` (the `.tex` files are 256 by 256, 256 by 256 and 128 by 128 RGBA8: 256, 256 and 64 KiB).

- **It works.** One document mixing three bitmap families (16 and 32 px), two TrueType families through the delegate and a run of bitmap text inside a plain paragraph laid out and drew correctly (viewed; Daedric draws in its own glyph set, accents `ö` and `ñ` resolve through CP437). Windows (MSVC 2022) and Linux (GCC 13.3) produced a **byte-identical** rendered PNG.
- **Self-consistency:** `GetStringWidth("Hello")` at 16 px is 35, the sum of `width + kerningRight` read independently from the file; at 32 px (scale 2) it is 70; the laid-out span width equals `GetStringWidth`. A CJK code point is absent (width 0): no fallback chain in the prototype (the prototype ignores the `fallback_face` flag RmlUi passes to the installed engine; honouring it is the engine's job).
- **The baseline convention** (needed for `FontMetrics`): the loader gives MyGUI a bearing Y of `fontSize - ascent` and a line height of `fontSize`, so a glyph's top sits `fontSize - ascent` below the line top and the **baseline is `fontSize` below the line top**. Check on the file: for `A` the ascent equals its height (a cap sits on the baseline) and for `g` the height exceeds the ascent by 2 (a descender), so `g` ends 2 px below `A`. The metrics are ascent = `fontSize`, descent = the largest `height - ascent` of any glyph (1 px for Magic Cards), line spacing = `fontSize`, x height = the ascent of `x`.
- **Scaling:** RmlUi asks for an integer pixel size; the prototype scales the quads by `size / fontSize` with positions and sizes rounded to whole pixels. That gave readable, slightly irregular glyph spacing at 14 and 24 px; whether the game wants nearest or linear sampling for a scaled bitmap font (MyGUI's choice is not recorded here) is a visual decision, not tested.
- **Cost (Windows, 3 runs):** reading and preparing three fonts 1.1 to 1.6 ms (12 ms in the cold Linux container); a document using them lays out in about 1.3 ms; 200 rows of about 30 characters lay out in 0.9 ms (0.5 ms on Linux); drawing is 62 draws for 200 rows (one mesh per text element). Render times in the log include the harness's software rasteriser and are not a renderer cost.
- **A lifetime rule found by a crash:** a `CallbackTextureSource` that outlives `Rml::Shutdown()` logs "Leaking CallbackTexture" and then crashes. The engine must drop its fonts (the sources) **before** `Rml::Shutdown()` and while the render manager lives, and any font reload has to follow the same rule through `ReleaseFontResources`. Also: RmlUi lower-cases family names before the engine sees them from a style sheet, so a custom engine must key its families in lower case (a direct `GetFontFaceHandle` call with a mixed-case family on the default engine returned a null handle and crashed the harness on its first use; the style sheet path always passes lower case).
- **A constraint on the production shape (checked in the headers and `Core.cpp`):** the delegation above is **not a supported pattern**. `FontEngineInterfaceDefault` lives only under `Source/Core/FontEngineDefault/` (not a public header), and `Core.h` says `SetFontEngineInterface` "must be called before Initialise()"; the prototype swaps the engine after `Initialise()` to get hold of the default one, which works in 6.3 but is undocumented behaviour. A production engine therefore either keeps that swap (fragile across RmlUi updates) or **owns its FreeType path** (a copy of `FreeTypeInterface.cpp` and the face and glyph code, MIT, a few hundred lines to maintain), which is also where `FT_RENDER_MODE_MONO` for `Antialias=false` (14.4), glyph hiding and the fallback chain would live. This changes U1's shape (a font module of its own) but not the verdict.
- **Not built, and what production needs:** the Polish and other code page tables of `fontloader.cpp` (the prototype has CP437 only, and control codes 0 to 31 are unmapped), the `[GUI] font size` and book scale (the journal draws at another resolution), the `Fonts_Font_*` ini lookup and VFS reads (the prototype reads from disk), kerning pairs (the `.fnt` has none beyond the per-glyph right bearing), a fallback chain, colour codes in journal text, and the cursor and not-defined glyph substitutions (`|` and `_`). Estimate: 1 to 2 days for the engine proper with tests, plus the visual check of scaling; low risk.

### 14.4 Item 1b: a `.omwfont` through the default engine (MEASURED)

A 12-line scan of `DejaVuLGCSansMono.omwfont` reads `Source=DejaVuLGCSansMono.ttf`, `Antialias=false`, `Resolution=58`, `TabWidth=8` and 7 code lines; the TTF named by `Source` loads through RmlUi's default FreeType engine and lays out ("DejaVu mono: 0123 fi AV" is 184 px wide at 14 px; ascent 13, descent 4, line spacing 16). What an adapter has to decide, found by reading `FreeTypeInterface.cpp` and running the check:

- **`Antialias=false` is not honoured.** The default engine always renders `FT_RENDER_MODE_NORMAL` (`FreeTypeInterface.cpp`, line 309); the pixel-art look of the mono font and of the Mystic Cards and Demonic Letters fallbacks would become anti-aliased text. A true mono render needs `FT_RENDER_MODE_MONO` in a custom engine, or a flag in a copy of the default one.
- **`Code range` and `Code hide` are not honoured** (U+0416 from the font's `1025 1105` range is present and so is everything else the TTF holds). Hiding is a filter an adapter can apply to the string before delegating; it only matters for the ranges the files hide on purpose (control and Windows-1252 holes).
- **`Resolution` and `Size`:** MyGUI renders the glyph atlas at `Resolution` dpi for a `Size` in points; RmlUi takes the size from RCSS `font-size` in px. The adapter maps `[GUI] font size` to the RCSS size and ignores `Resolution` (oversampling is RmlUi's own atlas business).
- So an `.omwfont` needs a ~40-line loader on top of the default engine, and a custom `FontEngineInterface` only if the mono look matters. This is the same single dispatching engine as 1a.

### 14.5 Item 3: the clip mask and layer stack against RmlUi's Vulkan backend (READ, NOT BUILT)

Read, not copied: `Backends/RmlUi_Renderer_VK.cpp` (3,017 lines) and `.h` (563), `RmlUi_Renderer_GL3.cpp` (2,171) and `.h` (210), the DX12 renderer (10,086 lines, only counted). Licence: `LICENSE.txt` of the repository is MIT (2008 to 2014 CodePoint Ltd, Shift Technology Ltd; 2019 to 2026 The RmlUi Team); the Vulkan folder carries its own `LICENSE.txt` for the bundled `vk_mem_alloc.h` (Vulkan Memory Allocator, AMD, licence text read; `vulkan.h` is the Khronos header, its licence not read). Nothing was copied into the repository; any later port would keep the notice in `docs/dependencies.md` and in the file.

- **Finding: the Vulkan backend is a basic renderer.** The README feature table gives the Vulkan renderer "basic rendering" and "transforms" only: **no clip masks, no filters, no shaders** (the GL3 renderer is the reference and has all four; DX11 and DX12 too). A grep of `RmlUi_Renderer_VK.cpp` for `PushLayer`, `CompositeLayers`, `SaveLayerAsTexture`, `CompileFilter` and `CompileShader` finds nothing. Its only stencil use is a workaround for a scissor under a transform (it fills a stencil region from a quad, then tests against it); most of its 3,017 lines are instance, device, swapchain, allocator and command-buffer plumbing that VSG already provides.
- **So the reference for OpenEden's port is GL3.** Line counts of its parts: clip mask 73 lines (`EnableClipMask`, `RenderToClipMask`: the three operations `Set`, `SetInverse` and `Intersect` of `ClipMaskOperation`; the stencil test on every draw is by reading the enum's comments and the VK scissor workaround, not by reading those GL lines), layers about 115 (`PushLayer`, `CompositeLayers`, `PopLayer`, `SaveLayerAsTexture`, `SaveLayerAsMaskImage`) plus 89 for the layer stack (framebuffers created and reused per nesting depth), filters and shaders about 390 (`CompileFilter`, blur 113, drop shadow, colour matrix, gradients, blend mask) plus 320 lines of GLSL.
- **What the engine lacks today (grep of `components/render` and `apps/openmw/vsgengine.cpp`):** no pass or pipeline uses a stencil (`DepthStencilState` appears with stencil off), and the swapchain depth format is `VK_FORMAT_D32_SFLOAT` (`components/render/attachmentformat.hpp`, `Render::compatibleDepthFormat`, the format every main-view pipeline is compatible with). A stencil clip mask therefore needs its own render graph for the GUI with a depth-stencil image of its own (D32 plus S8 or D24 plus S8), not a change of the window format. The GUI graph already is separate (submit order 1, `components/render/CLAUDE.md`), so that is feasible without touching the scene passes.
- **The hard part is layers, not the clip mask.** Layers make the draw order of one UI frame switch render targets in the middle (draw the element into an offscreen image, filter it, composite it back). A VSG `RenderGraph` is one render pass; the UI frame has to be recorded as a sequence of passes interleaved in the order RmlUi issues them, with the right barriers, and with MSAA resolve rules for the final target. That has no precedent in this tree (the AO, bloom and TAA passes are fixed chains).
- **Estimate (mine, not measured, plus or minus 50 percent):** clip mask 2 to 3 working days (stencil attachment on the GUI pass, three stencil pipelines, a stencil test variant of every draw pipeline, tests including the transformed and rounded cases U0 showed wrong); layer stack 5 to 8 days (offscreen targets per depth, composite and save-as-texture passes, barriers, tests, a frame check); filters and shaders another 5 to 8 days and only if a theme wants blur, drop shadow, gradients or masks. Together clip mask and layers are about 7 to 11 days, 12 to 19 with filters and shaders. None of it is on the critical path if themes stay inside the supported subset (decision 3 of 13.11); **recommended: forbid those effects in the first themes and defer this entire item to after U1**, and say so in the theme guide.

### 14.6 Item 2: the Linux build (MEASURED)

In the `openeden-linux-noosg` image (GCC 13.3.0, CMake 3.28, Ninja; run with `--memory 12g`, `-j 8`): FreeType 2.14.3 in 2.5 s, RmlUi 6.3 plus its debugger library in 24.9 s, and the spike with the repository's warning set (`-Wall -Wextra -Wpedantic -Wshadow -Wdangling-reference -Wrange-loop-construct -Wunused-function -Werror`; the third-party libraries were built without `-Werror`, as `RMLUI_WARNINGS_AS_ERRORS=OFF`) in 3.6 s with **no warning**. The spike's checks (18) pass on Linux, its text output equals the Windows one but for the timing lines, and the rendered PNG is **byte-identical** to the one the MSVC build made, so layout, glyph placement and the premultiplied blend agree between the two compilers. Not done on Linux: `RmlUi` with `-Werror` (its own warnings were not counted), the Vulkan harness (needs the repository's Windows build tree), the Lua protocol harness (needs the vcpkg LuaJIT), any run on a real Vulkan driver, and the GCC `-Werror` pitfalls of the unit-test code the production adapter will add.

### 14.7 Item 4: IME and `nav` (MEASURED, headless, no OS input method)

- **`nav`:** with `tab-index: auto` and `nav: auto` on four buttons placed in a 2 by 2 grid, Right, Down, Left, Up from the first move focus b1 to b2 to b4 to b3 to b1 (spatial order), Tab and Shift+Tab walk the tab order, an explicit `nav-left: #b4` on a `div` jumps there and `nav-down: #b3` to its target, and **Return and Space on a focused element each fire one `click`**. So RmlUi provides focus movement and activation from five key identifiers (arrows, Tab, Return, Space); a gamepad is mapped to those keys by the engine's input layer (OpenMW's controller manager already turns pad input into key events, an assumption to check at U1). There is no separate navigation API and none is needed; what the component layer must supply is the focus ring and `:focus` styling (the RCSS `:focus` rule applied), and the policy for modal windows (a window's `nav` scope; not tested).
- **IME:** `Rml::TextInputHandler` gets `OnActivate`, `OnDeactivate` and `OnDestroy` with a `TextInputContext` that offers `GetBoundingBox` (the input's rectangle in screen pixels, 200 by 24 here: where an OS candidate window goes), `GetSelectionRange`, `SetSelectionRange`, `SetCursorPosition`, `SetText`, `SetCompositionRange` and `CommitComposition`. Driving it by hand as an IME would (preedit "ni" in range 2..4, then committing U+4F60) left the input's value `ab` plus the character in UTF-8 and the bytes right. `OnActivate` fired on focus, `OnDeactivate` on the next focus change, and `OnDestroy` for both inputs on closing the document (**no `OnDeactivate` before `OnDestroy` for an input that was still active**: a handler must clean up in `OnDestroy` too). `ProcessTextInput` inserted a typed é at the caret and returned false (RmlUi's input functions return true when the event was **not** consumed, so false means it was taken). A handler must be installed **before** a context is created (`SetTextInputHandler` is read at construction: a context made earlier never called it).
- **Not verified:** a real OS IME (the SDL backend in the RmlUi repository, `TextInputMethodEditor_SDL`, 569 lines for the whole SDL platform file, handles `SDL_TEXTEDITING` and `SDL_SetTextInputRect`, and OpenEden uses SDL2; it is the pattern to follow and was read, not run), CJK glyph rendering (no CJK font), composition underline drawing, right-to-left text, clipboard, and a game pad run on a controller.

### 14.8 What is still not verified after U0b

The bitmap path on the real Vulkan renderer (the font texture goes through the same `GenerateTexture` as the TrueType atlas that U0 drew, so the risk is low, but it was drawn by the harness's software rasteriser here); nearest against linear sampling of a scaled bitmap font; the code page tables other than CP437 and the Polish swaps; fonts read through the VFS and inside the Morrowind BSAs (these four are loose files in this install; whether `.tex` files in an archive read the same is not checked); the `Fonts_Font_*` ini lookup; the clip mask, layers, filters and shaders (sized only); GPU timing; RmlUi's own warnings under GCC; an OS IME; a pad; and everything on the U0 list that U0b did not touch (section 13.9).

### 14.9 Decisions for toddhow (answered 2026-11-10, toddhow: go with recommendations: clip masks and layers forbidden in first themes; RmlUi and schedule stand; Lua UI option A; Czech `a-acute` limitation recorded, per-language font maps later)

1. **Confirm the correction:** `.omwfont` is TrueType-only; the bitmap case is `.fnt`/`.tex`. The first UI backend must support both, through one dispatching font engine that, because the default one cannot be reused supportably (14.3), **owns its FreeType path** (a copy of RmlUi's, MIT). OK to plan U1 that way?
2. **Mono look:** keep `Antialias=false` (it lives in the owned FreeType path of decision 1, small) or accept anti-aliased fallback and mono fonts?
3. **Clip mask and layers:** with the Vulkan backend having none of it and the estimate at 7 to 11 days (12 to 19 with filters and shaders), **forbid `box-shadow`, `filter`, `backdrop-filter`, `mask-image`, gradients and rounded-corner clipping in themes for now** and revisit after U1? (Recommended; this is decision 3 of 13.11 with the sizing attached.)
4. **Is U0b enough to remove the "conditional"** from the GO? Recommended: yes for fonts, Linux, IME and `nav`; the open conditions are now only the real-renderer font draw (small), effects (a decision), and the schedule.
5. **Schedule:** unchanged (U1 onward after Track V and game-pack stage d).

## 15. U0c spike, 2026-11-10 (stage log)

toddhow asked for U0c: close the items that U0b (section 14.8) listed as not verified, by experiment. Docs only in the repository. The code is throwaway in `I:/Projects/OpenEden-work/uispike/u0c/` (`u0c_main.cpp` about 760 lines, `font2.hpp`, `raster.hpp`, `tga.hpp`; the run logs are `u0c/out/run7.txt` and the PNGs beside it); nothing was added to the repository's CMake and no dependency was added. RmlUi 6.3 and FreeType 2.14.3 are the U0 builds. The spike links the repository's existing RelWithDebInfo build **read-only** (`build-jolt/components/componentsrd.lib` for `VFS::Manager`, `registerBethesdaArchiveKinds`, `ToUTF8` and `Render::GpuTimer`, plus the same VSG 1.1.16 DLL as U0) and the user's installed Morrowind (Data Files with its three BSAs); nothing derived from Morrowind's files is committed (the pictures stay in the work folder). Everything below is **measured** unless marked READ or NOT VERIFIABLE. Machine: NVIDIA GeForce RTX 4070, window at (-30000, -30000) with `OPENEDEN_QUIET_WINDOW=1`, 1920 by 1080, `VK_PRESENT_MODE_IMMEDIATE_KHR`, no other process of mine running; other sessions were using the machine (numbers are medians of 200 frames after 40 warm-up frames).

**What "the real renderer" means here, honestly.** As in U0, the draw path is the spike's own `Rml::RenderInterface` over raw VSG (one pipeline, `VertexIndexDraw` per geometry, a `BindDescriptorSet` per texture, scissor), in a `vsg::Window` and `vsg::Viewer` of its own, **not** the game's `Render::Engine` and not its render graphs. It is the same VSG, the same device and the same swapchain format as the game; what it does not have is the engine's HDR, post-processing and the GUI graph's place in the frame order. The GPU timer is the repository's own `Render::GpuTimer` (`components/render/gputiming.*`, timestamp queries around a pass, the 3-frame read delay), not a copy.

### 15.1 Verdicts

| Item | Verdict | One line |
| --- | --- | --- |
| 1. Bitmap font on the real VSG path | **PASS** | 25,724 text pixels of a frame with 8 text lines, max channel difference 1 against the software result, 0 pixels over 2; and the same glyph shapes as a frame of the MyGUI game (correlation 0.9993 on a flat background) |
| 2. Nearest against linear, 1x / 1.5x / 2x | **PASS** (decided by eye and numbers) | identical at 1x; at 1.5x nearest is irregular, linear is smooth; at 2x nearest is crisp and linear is soft; no cost difference; MyGUI today is linear |
| 3. Code pages | **PASS for the mapping logic; NOT VERIFIABLE for real Polish, Czech and Russian `.fnt` files** | the tables the game builds are reproduced from the same `ToUTF8` tables and the same fontloader code; no non-English Morrowind font is installed |
| 4. Fonts and textures through the VFS and BSAs | **PASS, with one gap** | fonts (`.fnt`, `.tex`, TrueType) from loose files and from an Eden Archive, documents, style sheets and `.tga` from the VFS, and a `.dds` that only `Morrowind.bsa` provides; **no font was read from a BSA** (this install's three BSAs hold no fonts, checked with `bsatool list`); one finding about what RmlUi's file interface does not cover |
| 5. GPU time of the UI pass | **PASS** | 0.007 ms (214 elements) to 0.036 ms (1,834 elements) to 0.103 ms (12,034 elements) over an empty pass; the CPU, not the GPU, is the cost |
| 6. Controller path | **PASS with a limit (synthetic events, no pad)** | focus, activation, virtual cursor and scrolling work from `SDL_CONTROLLER*` events through a 60-line bridge; what the engine's controller manager does beyond that has to be rebuilt |
| 7. IME and CJK | **PASS for the composition path and for CJK glyphs with a fallback face; an OS IME is NOT VERIFIABLE here** | |

### 15.2 Item 1: the bitmap font on the real VSG path

`u0c/font2.hpp` is the U0b font engine with its reads moved to a callback (the VFS), the game's code page handling (15.4) and a per-family scaling policy. The scene is a `Rml::Context` whose render interface is a **tee**: every call goes to the VSG renderer and to the U0b software rasteriser (a CPU triangle fill with premultiplied blending). The frame is read back from the swapchain image with `vkCmdCopyImageToBuffer` and compared pixel by pixel.

- Eight text lines (Magic Cards and Century Gothic at 16 px in the policies lf, lr and nr, Daedric at 16 px, and Magic Cards at 32 px) cover **25,724 pixels** of a 1920 by 1080 frame. Against the software result: **max channel difference 1, mean 0.0064, pixels over 2: 0** (`out/run7.txt`). Viewed side by side (`item1_vsg_crop.png`, `item1_software_crop.png`): identical. **What this covers:** the software rasteriser samples nearest, so the match proves the 1:1 rows (where a linear sampler at pixel centres gives the nearest value) and the nearest 32 px row; it does **not** prove linear sampling at a fractional scale (that is judged by eye in 15.3).
- The first run of the comparison showed 273 pixels that were up to 54 levels off. They were **the software rasteriser's fault**: a pixel centre exactly on the diagonal shared by the two triangles of a quad was drawn twice (a double blend), where a GPU draws it once (top-left rule). After the tie rule was fixed in the rasteriser the difference fell to 1. This is a property of the check, not of the renderer, and is recorded so nobody reads a text-glyph tie as a renderer defect.
- **Against the MyGUI game's text.** The only game frame of Morrowind text I could use without starting the game was another session's (`I:\Projects\OpenEden-work\dialogue\gamecheck\frames\screenshot000.png`, 800 by 600, the dialogue window drawn by MyGUI, read only). Rendering the same two lines at 1:1 with `mc-lf` (linear, float edges) and finding the best alignment by normalised cross correlation: "can I do for you?" **0.9993** (mean coverage difference 0.014, no pixel off by more than 0.25); "Greetings, Citizen. I'm Balyn Omavel." 0.9657 (mean 0.072), the difference being the dialogue window's background gradient under the text, which a percentile normalisation does not remove (`item1c_*.png`: the two rows look the same). So the glyph shapes, advances, baseline and spacing of the prototype engine match MyGUI's for these strings at 1:1.
- **Not compared:** MyGUI at a scaling factor other than 1 (no such frame; its sampler is VSG's default, linear, see 15.3), the journal's book scale, coloured link text.

### 15.3 Item 2: nearest against linear at 1x, 1.5x and 2x

Four policies by family suffix, on the same string in two bitmap fonts, at density ratios 1.0, 1.5 and 2.0 (RmlUi sizes 16, 24 and 32 px): **lf** linear sampling with float quad edges; **lr** linear with edges rounded to whole pixels; **nr** nearest with rounded edges; **ni** nearest with an integer scale (the scale is rounded to a whole number, so the text is as large as 2x at ratio 1.5). A "mid-tone" fraction measures blur: the share of ink pixels (above 10 percent coverage) that are not nearly solid (below 85 percent); the native fraction of the font itself at 1:1 is 74.6 percent for Magic Cards and 67.6 percent for Century Gothic (the fonts are anti-aliased).

| Ratio | lf (linear, float edges) | lr | nr | ni | Seen (`item2_dp15_zoom_*.png`, `item2_dp20_zoom_*.png`, 4x magnified) |
| --- | --- | --- | --- | --- | --- |
| 1.0 (all four identical) | 74.6 % | 74.6 % | 74.6 % | 74.6 % | pixel exact |
| 1.5 (Magic Cards) | 93.8 % | 92.9 % | 73.9 % | 74.3 % (text 33 % larger) | lf smooth, lr almost the same, **nr uneven strokes and spacing**, ni crisp but too large |
| 2.0 | 95.6 % | 95.6 % | 74.6 % | 74.6 % | lf soft, nr pixel-doubled and crisp |

- **Linear is what MyGUI does today**: `components/vsgadapters/mygui/texture.cpp` creates its sampler as `vsg::Sampler::create()` with only the address modes set, and VSG's `Sampler` defaults both filters to `VK_FILTER_LINEAR` (`vsg/state/Sampler.h`, line 31). **The "float edges" half is inferred, not verified:** `components/myguiplatform/scalinglayer.cpp` draws a layer at a float scale through a proxy render target (`ScalingLayer::renderToTarget`, `ProxyRenderTarget::getInfo`), so glyph quads reach the GPU at fractional pixel positions; whether MyGUI rounds a glyph's position in layer coordinates before that scaling was not checked. `lf` and `lr` bracket the two possibilities (they differ by a few pixels of ink at 1.5), so the game's text at a scaling factor above 1 is one of the two lf/lr columns, most likely lf. `lf` and `lr` differ by a few pixels of ink at 1.5 and are equal at ratios that give whole-pixel quads.
- **What looks right.** Whole-number ratios (1 and 2): nearest is clearly the better picture of a pixel font (crisp; MyGUI at a scaling factor of 2 would blur it with its linear sampler). Fractional ratios (1.5): nearest is not acceptable (strokes alternate between one and two pixels), linear is the smooth and familiar result, and integer snapping changes the text size by a third. **Recommendation: linear with float edges as the default (parity with today), plus a theme or setting that switches to nearest when the effective scale is a whole number.** This is a visual call; the numbers above and the four magnified pictures are the evidence.
- **Cost: none.** 200 text lines of about 55 characters at ratio 1.5: `Render()` 0.053 to 0.056 ms and GPU pass median 0.013 to 0.016 ms for all four policies (`out/run7.txt`).

### 15.4 Item 3: legacy code pages

`u0c/font2.hpp` builds the code point to glyph map exactly as `components/fontloader/fontloader.cpp` does for a `.fnt`: for each glyph byte, `ToUTF8::Utf8Encoder(encoding)` gives the code point (CP437 when the data files are windows-1252, otherwise the data files' own encoding), the Polish swap table (`mapUtf8Char`, copied from that file) is applied for windows-1250, the 67 substitution and omission lines (`additional.emplace`, `omitted.push_back`) are copied from the same function by a script (so they cannot drift in the spike), and a code point with no glyph is drawn as the glyph of `_`. The real `FontLoader` could not be run headless (it needs a live MyGUI), so the claim is "same inputs and same code", not "same output seen".

| Case (UTF-8 text, through the legacy bytes and back) | Round trip | Non-ASCII characters | direct | substituted | drawn as `_` |
| --- | --- | --- | --- | --- | --- |
| Polish, windows-1250 | yes | 18 | 18 | 0 | 0 |
| Czech, windows-1250 | yes | 21 | 17 | 3 | **1 (U+00E1, a with acute)** |
| Russian, windows-1251 | yes | 47 | 47 | 0 | 0 |
| Western, windows-1252 with the CP437 glyph table | yes | 19 | 10 | 9 | 0 (some map to glyphs the English font leaves empty) |

- **Finding (inherited, not RmlUi's): the Polish hack applies to every windows-1250 install.** `mapUtf8Char` remaps glyph byte 0xE1 to 0x8C for the Polish font, so under windows-1250 the code point U+00E1 has **no glyph slot** and is drawn as `_`; a Czech install would show that (the loader's own code does it; whether Czech fonts put á elsewhere is not checkable here).
- **Finding (needs a test in U1): duplicate code points.** 21 to 22 code points under windows-1250 and 1251 have both a real glyph and a substitution (for example U+00D3 O with acute is glyph 0x99 in the Polish table and the substitution to `O`, glyph 0x4F). The loader adds both as `<Code>` entries to the MyGUI font, in order of glyph byte (for each byte its real code, then its substitutions), so the entry of the lower glyph index comes first; **which one MyGUI keeps is not verified** (MyGUI's source is not in the tree; only the header was read). The spike lets the real mapping win. Counted from the logged lists: under windows-1250 the substitution has the lower index in 21 of the 22 cases (the exception is U+00A6, real 0xA6, substitution 0xDD), under windows-1251 in 19 of 21 (exceptions U+2021 and U+00A6). So a MyGUI that keeps the **last** entry agrees with the spike except in those 1 to 2 cases, and a MyGUI that keeps the **first** entry disagrees with it in 19 to 21 cases: that is exactly what the U1 characterisation test must pin. U1's font engine must pin this with a characterisation test against the real loader before the old path goes.
- **Not verified:** the glyph pictures of a real Polish, Czech or Russian Morrowind `.fnt` (the install has only the four English fonts; the English font under a 1250 or 1251 map shows other glyph slots, as expected, `item3_crop.png`); control codes 0 to 31 (the spike skips them, the loader maps them).
- TrueType rows in the same picture (`item3_crop.png`) draw Polish, Czech and Russian correctly through the default engine, with `Liberation Sans` and with the repository's `DejaVu LGC Sans Mono` (that is the repository's mono debug font, `files/data/fonts/`, not one of the fallbacks for the Morrowind fonts). The repository's real fallbacks, `MysticCards.ttf` and `DemonicLetters.ttf`, were **not** tested on these scripts; Greek was not tested.

### 15.5 Item 4: fonts and textures through the VFS and the BSAs

`VFS::Manager` was set up as the game does (three BSAs below the loose `Data Files` folder, then the repository's `files/data`, then the spike's own assets; 6 archives, 26,456 files, index in 43 ms in the last run, 107 ms in the first), and RmlUi was given a `FileInterface` (`Open`, `Read`, `Seek`, `Tell`, `Length` over `Manager::find`) and the font engine a read callback over the same manager.

- **Bytes:** `Fonts/Magic_Cards_Regular.fnt` and `fonts/magic_cards_regular.fnt` through the VFS equal the disk file byte for byte (14,632 bytes, read 0.17 ms against 0.11 ms from disk). The 12 bitmap families (3 `.fnt` and their `.tex` files, four policies each) load in 13.6 ms; TrueType (`fonts/LiberationSans-*.ttf`, the repository's `fonts/DejaVuLGCSansMono.ttf`) is read through the VFS and handed to the default engine as memory.
- **Where the fonts came from:** loose files of `Data Files/Fonts` and the Eden Archive. A font inside a BSA was **not** read (there is none in this install), so "fonts inside BSAs" from U0b's list is closed by the DDS case (the BSA reader and the stream are the same for any entry), not by a font.
- **Documents and style sheets:** `ui_bmp.rml`, `theme_bmp.rcss`, `icon.tga` and `panel.tga` were served by `VfsFileInterface` (cumulative counts of the last run: 9 VFS hits, 0 misses, 2 disk fallbacks, which are the two system fonts named by absolute path).
- **A texture that only a BSA provides.** The GOG install has loose copies of many textures, and Morrowind's BSAs hold their textures as **DDS** (the BSA-only VFS has 4,783 `textures/*.dds`, 913 of which a loose file wins in the full VFS, and no `.tga` at all). With a second manager holding only the three BSAs and the assets, `textures/_land_default.dds` (256 by 256, 43,808 bytes, from `Morrowind.bsa`) was read through RmlUi's `LoadTexture`, decoded (the spike has a 70-line DXT1/3/5 decoder; production would upload the block compressed data) and drawn in the document (`item4_crop.png`: the brown terrain texture in the details slot, the nine-patch frame and the bitmap-font text, all through the VFS).
- **A pack:** the four fonts packed with the repository's `edenpack` (`--normalize-names`) and mounted above the loose folder: the VFS names the Eden Archive as the winner, serves the same bytes, and the bitmap font loads from it (both `.fnt` and `.tex`).
- **Finding: RmlUi's file interface does not see everything.** It serves documents, style sheets, `LoadTexture` sources and path-based `LoadFontFace` calls. A **custom font engine reads its own files**, so the `.fnt` and `.tex` reads must be routed through the same VFS by the engine itself (the spike's callback); a font file named by absolute path (a system CJK font) falls outside the VFS and needs a policy.
- **Not verified:** the `Fonts_Font_*` ini lookup (the spike names the files); reading from an archive on a worker thread while the UI loads (the VFS contract allows it after `buildIndex()`; not exercised); a path in a `.omwfont`.

### 15.6 Item 5: GPU time of the UI pass

Timestamp queries around the GUI `RenderGraph` through `Render::GpuTimer` (a `GpuTimestampNode` before and after it, as the engine places them), 1920 by 1080, RTX 4070, `GPU` is the median of 200 frames after 40 warm-up frames. The window is the U0 inventory window (560 by 400 dp); "full-screen panel" resizes the same window to 1880 by 1040 px. An empty pass (clear plus the timestamps) takes **0.005 ms**. The CPU columns are per frame (`Update()`, `Render()` which only builds the draw list, and the VSG record, submit and present).

| Scene | Elements (with text nodes) | Draws / vertices | GPU median | GPU over empty | CPU Update | CPU Render() | CPU record+submit |
| --- | --- | --- | --- | --- | --- | --- | --- |
| U0 window, 30 rows, TrueType | 214 | 108 / 2,232 | 0.012 ms | 0.007 | 0.005 | 0.039 | 0.15 |
| the same, bitmap font | 214 | 108 / 1,968 | 0.012 ms | 0.007 | 0.005 | 0.039 | 0.15 |
| 300 rows in the 560 by 400 dp window, scrolling every frame | 1,834 | 650 / 4,496 | 0.019 ms | 0.014 | 0.035 | 0.36 | 0.22 |
| 300 rows, full-screen panel, TrueType | 1,834 | 696 / 5,940 | 0.041 ms | 0.036 | 0.037 | 0.34 | 0.27 |
| 300 rows, full-screen panel, bitmap | 1,834 | 696 / 5,512 | 0.041 ms | 0.036 | 0.035 | 0.36 | 0.28 |
| stress: 2,000 rows, full-screen panel, bitmap | 12,034 | 4,096 / 19,072 | 0.108 ms | 0.103 | 0.67 | 2.7 | 0.67 |

- **The GPU is not the cost.** Even the 12,000-element stress list is a tenth of a millisecond on a discrete GPU. The cost is the CPU: `Render()` rises from 0.04 ms (214 elements) to 0.36 ms (1,834) to 2.7 ms (12,034), `Update()` to 0.7 ms, and recording to 0.7 ms, **with nothing changing in the document** (RmlUi walks every element each frame). This confirms U0's finding that long lists must be virtualised, and sizes it: about 2,000 elements are comfortable (0.4 ms per frame in total), 12,000 are not.
- A list's draw count grows with its length (108, 650, 696, 4,096 draws): rows outside the scroll container are **not** culled by RmlUi (not investigated further beyond the counts).
- The bitmap and the TrueType fonts cost the same. The nearest and linear policies cost the same (15.3).
- **Not verified:** an integrated or low-end GPU (only the RTX 4070 is here; the Docker llvmpipe route needs a Linux build of the harness, which was not made), the GUI pass inside the game's real frame (HDR, TAA, overlay), the cost of the clip mask and layers (not built, 14.5).

### 15.7 Item 6: a controller through `nav`

No controller is attached (`SDL_NumJoysticks()` is 0), so the events are synthetic: `SDL_PushEvent` of `SDL_CONTROLLERBUTTONDOWN`, `SDL_CONTROLLERBUTTONUP` and `SDL_CONTROLLERAXISMOTION`, polled and passed through a 60-line bridge (`PadBridge` in `u0c_main.cpp`) written after what `apps/openmw/mwinput/controllermanager.cpp` does for MyGUI (`gamepadToGuiControl`), with two deliberate differences: the d-pad goes to the four arrow keys and the shoulder buttons to Tab and Shift+Tab as in the engine, but **A goes to Return** (the engine sends `KeyCode::Space`, or a mouse click in cursor mode; RmlUi activates on both, 14.7) and **B goes to Escape** (the engine calls `exitCurrentModal` or `exitCurrentGuiMode` directly); L3 toggling a virtual cursor, the left stick moving that cursor (`ProcessMouseMove`, with the engine's 2000 dead zone and 1500 px/s) and the right stick scrolling (`ProcessMouseWheel`).

- On a 2 by 2 grid of buttons with `nav: auto`: RB, d-pad right, down, left, up moved focus b1, b2, b4, b3, b1; **A fired one `click` on the focused button**; Shift+Tab from the first control wrapped to the last (the text input); the focused button draws the `:focus-visible` style (`item6_focus_b2_crop.png`). **PASS** (2 checks).
- In cursor mode the left stick moved the cursor and set `:hover`; the right stick scrolled a list from 0 to 6 px in five frames; A clicked the element under the cursor. **PASS** (3 checks).
- **What the engine needs beyond this** (READ in `controllermanager.cpp`, none of it tested): per-window controller handlers (`getActiveControllerWindow`, `onControllerButtonEvent`, `onControllerThumbstickEvent`, `isGamepadCursorAllowed`, the controller scroll widget and tooltip modes), X and Y mapped to the `;` and `'` shortcuts, the triggers to `-` and `=` for page switching, the suppression of some keys while `SDL_IsTextInputActive()`, B closing a modal or the GUI mode, and `Settings::gui().mControllerMenus`. None of that exists in RmlUi: it belongs in the Lua component layer as **focus scopes** (per window, with a modal rule). RmlUi has modal documents (`ModalFlag`, READ in the headers, not tested).
- **Not verified:** a real controller, SDL's controller database and hot plugging, analog dead zones with real hardware noise, repeat rates for held buttons (RmlUi gets key down and up; the repeat is the bridge's job), a gamepad glyph set.

### 15.8 Item 7: IME composition and CJK

- **Composition path, synthetic.** A `Rml::TextInputHandler` written after the pattern of RmlUi's SDL backend (read, not copied) received `SDL_TEXTEDITING` and `SDL_TEXTINPUT` events pushed through SDL's queue. With an input holding `ab` and the caret after it: preedit `n` gave `abn`, `ni` gave `abni`, a replacement preedit (the candidate U+4F60) gave `ab你`, and an empty edit followed by `SDL_TEXTINPUT` with the same character left **exactly one copy**, `ab你`. `GetBoundingBox` returned the input's rectangle (what `SDL_SetTextInputRect` is given; the call was made). **PASS** (3 checks). This extends 14.7, which drove the same context calls by hand without SDL events.
- **CJK glyphs through the VSG path.** With the system fonts Microsoft YaHei and Malgun Gothic loaded as fallback faces (not part of the repository, so not shippable), a line of only Chinese, Japanese and Korean characters (the IME test's document is unloaded first, so nothing overlaps) draws correctly through the default FreeType engine; the evidence is the viewed picture (`item7_cjk_crop.png`), the pixel-count check only guards against an empty line. A **bitmap family cannot draw CJK** (the `.fnt` has no such glyphs) and the engine draws the not-defined `_`; the font engine has no fallback chain yet (14.3), so CJK text needs a TrueType family. **PASS** for glyph drawing.
- **NOT VERIFIABLE here:** a real OS IME (no IME is installed or can be exercised headless), the candidate window position, the underline of the preedit text (RmlUi draws none by itself: the component layer must style the composition range), right-to-left text, clipboard, and which CJK font the game would ship (no CJK font is in the repository: a licence and size decision).

### 15.9 Other things found

- **RmlUi has no default style sheet, so `div` is `display: inline`.** The first list of the controller test ran its rows together until `display: block` was set. A base sheet (block, flex, form controls, scrollbars) is part of the component library's job in U1; the samples' sheets are not ours to ship.
- The spike's font textures have a single mip level and looked right at ratios 1 to 2 with linear sampling; ratios below 1 (minification) were not tried.
- The lessons of U0 and U0b held: textures must be created while the render manager lives, and the `Rml::Shutdown()` order (font sources first) was kept without a crash across the whole run.

### 15.10 Does anything change the recommendation or the U1 plan?

**No change to decision 0015's recommendation (RmlUi first, our own Lua component layer, one backend behind a small protocol), and no change to the U1 schedule.** U0c closes the one open condition of 14.9 decision 4 that was a check (the real-renderer font draw) and the other items U0b listed, and finds nothing that blocks; the conditions that remain are effects (a decision) and the schedule. Adjustments to U1's scope and estimate, all small:

1. **Font engine: still 1 to 2 days, but its scope is now known.** Add the code page layer (done in 150 lines in the spike from the existing tables) and a VFS-backed read; add a **characterisation test against the real `FontLoader`/MyGUI** for the duplicate code point question (15.4) before the old path is removed; the Czech `á` behaviour is inherited and should be a recorded decision (keep or fix), not a surprise.
2. **Text sampling default:** linear (parity), with nearest as a theme or setting at whole-number scales (15.3); decision for toddhow below.
3. **Lists:** virtualisation in the list component is mandatory above roughly 2,000 elements (15.6); the spike's numbers give the budget.
4. **Controller:** a bridge of about 60 lines works; the real work is the per-window controller logic that MyGUI's windows carry, which moves into focus scopes in the Lua layer as windows are ported. Plan a hands-on check on a real pad when the first window is ported.
5. **IME/CJK:** the composition handler is about 30 lines; plan a hands-on check with a real OS IME (toddhow's machine) and decide the CJK fallback font and the composition styling before CJK is claimed.
6. **Checks to schedule with the first ported windows** (small, none blocks the GO): the MyGUI duplicate-glyph test, a real pad, a real IME, an integrated GPU. **Conditions that remain on the GO:** effects (section 14.9 decision 3) and the schedule.

### 15.11 What is still not verified after U0c

The Linux build of the spike's VSG harness (only the headless U0b spike was built on Linux); a weak or integrated GPU; the GUI pass inside the game's real frame; real non-English Morrowind fonts; MyGUI at a scaling factor other than 1 and the journal's book scale; a real controller; an OS IME and composition styling; right-to-left text; the clip mask, layers and filters (14.5); the `Fonts_Font_*` ini lookup and a `[GUI] font size` mapping; reading from a worker thread; the effect of RmlUi's per-frame element walk on a loaded game frame (the numbers above are an otherwise idle harness).

### 15.12 Decisions for toddhow

1. **Text sampling at fractional UI scales:** keep MyGUI's behaviour (linear, float edges) as the default, and add nearest for whole-number scales as an option? (Recommended: yes.)
2. **The Polish table applied to every windows-1250 install** makes Czech `á` an underscore: keep (parity) or fix in the new font engine? (Recommended: keep for the first port, record it, fix later with a test.)
3. **CJK:** a CJK fallback font would have to be added to the repository's data (an OFL font such as Noto Sans CJK is several megabytes; not added, not even fetched here). Decide before CJK locales are claimed. (Recommended: defer; Western locales first.)
4. **Is U0c enough to remove the remaining "conditional" from the GO** (the remaining items in 15.10 point 6 are checks to schedule with the first ported windows, not blockers)? (Recommended: yes.)

## 16. Lua UI compatibility layer: can `openmw.ui` survive the move to RmlUi? (survey, 2026-11-10)

Docs only, read-only survey; nothing was built, run or measured. Asked for by toddhow's answer of 2026-11-06 ("keep existing OpenMW Lua UI mods working during the Morrowind stage, by re-implementing `lua_ui` over the new system") and decision 0015 point 6. Every code claim cites a file and function at the working tree of 2026-11-10 (**READ**); anything recalled from memory of MyGUI, RmlUi or OpenMW is marked **RECALLED-VERIFY**. All effort figures are **my estimates, plus or minus 50 percent**, not measurements.

### 16.0 Verdict

**The hypothesis is true in its weak form and refuted in its strong form.**

- **True:** the geometry maths, the layout-table diffing, templates and slots, property parsing and the upward event bubbling are in OpenEden's own C++ (`components/lua_ui/`, 2,347 lines in 31 files) and not in MyGUI. Flex, Container, relative/absolute/anchor placement and Window dragging are plain integer arithmetic (`flex.cpp` `LuaFlex::updateChildren`, `widget.cpp` `WidgetExtension::calculateSize/calculatePosition`, `container.cpp` `LuaContainer::updateSizeToFit`, `window.cpp` `LuaWindow::notifyMouseDrag`).
- **Refuted:** MyGUI is not "only the leaf drawing and input layer". **Every Lua widget class inherits from a MyGUI class** (`widget.hpp` `LuaWidget : MyGUI::Widget`; `text.hpp` `LuaText : MyGUI::EditBox`; `image.hpp` `LuaImage : MyGUI::ImageBox` and `LuaTileRect : MyGUI::TileRect`; `flex.hpp`, `window.hpp`, `container.hpp` `: MyGUI::Widget`; `adapter.hpp` `LuaAdapter` is a MyGUI widget too). MyGUI owns the objects' memory (`element.cpp` `destroyWidget` calls `MyGUI::Gui::destroyWidget`; `createWidget` uses `MyGUI::Gui::createWidgetT` through the factories registered in `util.cpp` `registerAllWidgets`), type identity is MyGUI's `getTypeName()` (`element.cpp` lines 134 and 284), and the following are **all MyGUI's**: text extents (`getTextSize`, `getFontHeight` in `LuaText::calculateSize` and `LuaTextEdit::calculateSize`), word wrap and alignment, hit testing, mouse and key focus, capture, drag and double-click detection, clipping to the parent, alpha inheritance (`widget.cpp` `updateProperties`: `setAlpha`, `setInheritsAlpha`), visibility inheritance (with a bug workaround in `updateVisible`), layers (`layers.cpp` creates `MyGUI::OverlappedLayer`), the pointer (`setPointer("arrow")`), the texture size lookup (`image.cpp` line 52, `MyGUI::RenderManager::getTexture`), the special `"white"` texture (`windowmanagerimp.cpp` line 2295) and the font and skin (`files/data/mygui/openmw_lua.xml`, skins `LuaText`, `LuaTextEdit`, `LuaImage`, font "Default").
- **Another part of the hypothesis is refuted:** there are **no tooltips** in the Lua UI. `grep -i tooltip` over `components/lua_ui`, `files/lua_api/openmw/ui.lua` and `docs/source/reference/lua-scripting/widgets` finds nothing. Focus is MyGUI's key focus and mouse focus, surfaced as `focusGain`/`focusLoss` events only; there is no focus traversal.
- **Count of the leak (READ, `grep -c MyGUI`):** 235 lines in 22 of the 31 files of `components/lua_ui/` (`widget.cpp` 55, `widget.hpp` 39, `window.cpp` 13, `util.cpp` 10, `layers.hpp` 10, `image.cpp` 10, `flex.hpp` 10, `flex.cpp` 9, `container.cpp` 8, then 7 or fewer in the rest). Outside the component: `windowmanagerimp.cpp` `onKeyFocusChanged` does `dynamic_cast<LuaUi::WidgetExtension*>` to start SDL text input; `LuaManager::inputEvent` calls `MyGUI::InputManager::isModalAny()` (`luamanagerimp.cpp` line 695); `SettingsWindow` embeds Lua elements through `LuaAdapter` (see 16.5).

**Can OpenEden keep `openmw.ui` working over RmlUi?** Yes, with Option A below (keep the layout and bubbling code, replace the MyGUI inheritance by a leaf interface backed by the element protocol), and only if the text-measurement and clipping/alpha/z-order semantics are rebuilt on purpose. Option C (let RmlUi do the layout) cannot reproduce today's rectangles and should not be the compatibility strategy. The one thing that gates the switch is not the API but **the MyGUI windows that host Lua elements** (16.5).

### 16.1 Inventory of the API surface

Sources: `apps/openmw/mwlua/uibindings.cpp` (`registerUiApi`, `initUserInterfacePackage`), `files/lua_api/openmw/ui.lua`, `docs/source/reference/lua-scripting/widgets/*.rst`, `components/lua_ui/*`, `files/data/scripts/omw/ui.lua`, `files/data/scripts/omw/mwui/*`. "MyGUI" in the last column means the call reaches a MyGUI-based Morrowind window or a MyGUI widget today.

**Functions of `require('openmw.ui')`** (context: menu and player scripts, `ui.lua` header)

| Name | Behaviour (READ) | MyGUI-dependent? |
| --- | --- | --- |
| `ui.create(layout)` | `Element::make` at once, `Element::create` queued with `LuaManager::addAction` (applied in `applyDelayedActions`, main thread) | leaf yes (widget creation) |
| `element:update()` | queued; only when `mState == Created`; layout of child Elements is not refreshed | leaf yes |
| `element:destroy()` | queued; `layout` reset to nil | leaf yes |
| `element.layout` (get/set) | property of the `Element` usertype | no |
| `ui.updateAll()` | marks all Created elements and queues `update()` on each; documented "potentially extremely slow" | leaf yes |
| `ui.content(table)` | `loadContentConstructor`, `components/lua_ui/content.lua`; `insert`, `add`, `indexOf`, index by position or name | no |
| `ui.texture{path, offset, size}` | `LuaUi::ResourceManager::registerTexture`, stores path and atlas rectangle only | no (the load happens in the widget) |
| `ui.screenSize()` | `Settings::video().mResolutionX/Y`, **not** the window or layer size | no |
| `ui.layers` (`indexOf`, `insertAfter`, `insertBefore`, `#`, index, `pairs`) and `Layer.name`, `Layer.size` | `layers.hpp/cpp`: `MyGUI::LayerManager::createLayerAt(name, "OverlappedLayer", index)`, `setPick(interactive)`, `ILayer::getSize()` | **yes** |
| `ui.TYPE`, `ui.ALIGNMENT` | `widgetTypeToName()` (`util.cpp`), `Alignment` enum | no |
| `ui.showMessage(msg, {showInDialogue})` | `LuaManager::addUIMessage` into the message box or the dialogue window | **yes** (Morrowind windows) |
| `ui.setConsoleMode(mode)`, `getConsoleMode()` (bound, not in `ui.lua`'s docs), `printToConsole(msg, color)`, `setConsoleSelectedObject(obj)`, `ui.CONSOLE_COLOR` | `WindowManager::setConsoleMode` etc. | **yes** (console window) |
| `ui.registerSettingsPage(page)`, `removeSettingsPage(page)` | `scriptsettings.cpp`; pages hold an Element shown in the Settings window | **yes** (see 16.5) |
| Private: `_setHudVisibility`, `_isHudVisible`, `_getDefaultFontSize`, `_getMenuTransparency`, `_showInteractiveMessage`, `_getAllUiModes`, `_getUiModeStack`, `_setUiModeStack`, `_getAllWindowIds`, `_getAllowedWindows`, `_setWindowDisabled`, `_isWindowVisible` | used by `omw/ui.lua`, `omw/mwui`, `omw/settings`, `omw/console` | **yes** (all go to `WindowManager`/`Settings`) |

**Types.** `Element` (`layout`, `update`, `destroy`, `tostring`), `Layer`, `Layers`, `Content`, `Layout` (keys `type`, `layer`, `name`, `props`, `events`, `content`, `template`, `external`, `userData`; `element.cpp` `LayoutKeys`), `Template` (`props`, `content`, `type`), `MouseEvent` (`position`, `offset`, `button`; `widget.cpp` `mouseEvent`), `KeyboardEvent` (an `SDL_Keysym` built in `keyEvent`), `TextureResource`, `SettingsPageOptions` (`name`, `searchHints`, `element`).

**Widget types and properties** (docs `widgets/*.rst` plus `*::updateProperties`)

| Type | Properties | External | Events beyond the base ones |
| --- | --- | --- | --- |
| Widget (base) | `position`, `size`, `relativePosition`, `relativeSize`, `anchor`, `visible`, `propagateEvents`, `alpha`, `inheritAlpha`, undocumented `pointer` (default "arrow") | `slot` (templates) | base events: `keyPress`, `keyRelease`, `mouseMove`, `mouseClick`, `mouseDoubleClick`, `mousePress`, `mouseRelease`, `focusGain`, `focusLoss`, `textInput` |
| Text | `autoSize` (true), `text`, `textSize` (10), `textColor`, `multiline`, `wordWrap`, `textAlignH`, `textAlignV`, `textShadow`, `textShadowColor` | none | none |
| TextEdit | `text`, `textSize`, `textColor`, `multiline`, `wordWrap`, `textAlignH`, `textAlignV`, `readOnly`, `autoSize` (false; only for single-line or read-only) | none | `textChanged` |
| Image | `resource` (a `ui.texture`), `tileH`, `tileV`, `color` | none | none |
| Flex | `horizontal`, `autoSize` (true), `align`, `arrange` | on children: `grow`, `stretch` | none |
| Container | none (sizes to its content) | none | none |
| Window | `caption` | on children: `caption`, `move`, `resize`, `action` | `windowDrag` |

**Lua-side interfaces that sit on this API** (`files/data/scripts/omw/`): `I.UI` (`setMode`, `addMode`, `removeMode`, `registerWindow`, `isWindowVisible`, ...; `UiModeChanged` event) in `ui.lua`; `I.MWUI` (templates `padding`, `interval`, `borders`, `box`, `boxTransparent`, text and edit templates; override then `ui.updateAll()`) in `mwui/`; `I.Settings` (`omw/settings/*`, builds its pages with `ui.create`); console scripts (`omw/console/*`); `openmw_aux.ui` (`deepLayoutCopy`, `deepUpdate`, `deepDestroy`: Lua only). There is no `MainMenu` interface in the tree (`grep interfaceName` over `files/data/scripts`).

**Things the API cannot see:** no call returns a widget's computed rectangle, size, font metrics or the cursor position. The only layout results scripts can observe are `MouseEvent.position/offset`, the `windowDrag` table (`position`, `size`) and `Layer.size`. This matters for the test plan: layout fidelity is **visual and event-offset fidelity**, not a readable property.

**Quirks worth preserving on purpose:** (a) `screenSize` is the video setting while `Layer.size` is MyGUI's possibly scaled layer size (they differ when `[GUI] scaling factor` is not 1; `windowmanagerimp.cpp` line 211); (b) borders are eight separate `Image` widgets (`mwui/borders.lua`), there is **no nine-patch** in the Lua API, so a texture "resource" is a sub-rectangle plus two tile flags; (c) every position and size is an `int` (truncation toward zero, `widget.cpp` lines 338-339 and 355-356; caps of 10,000,000 at lines 10-13); (d) `update()` re-applies a whole layout tree by diffing (`element.cpp` `updateContent`).

### 16.2 Where MyGUI types and calls leak (READ)

| Concern | Where | What MyGUI does today |
| --- | --- | --- |
| Class hierarchy and ownership | `widget.hpp` `WidgetExtension`/`LuaWidget`; every widget class; `element.cpp` | widget objects are MyGUI-owned; `dynamic_cast<WidgetExtension*>` from the MyGUI widget; `getTypeName()` decides "same type, update in place" or "destroy and create" |
| Coordinates | `widget.hpp` (`IntCoord`, `IntSize`, `IntPoint`, `FloatCoord`, `FloatSize`), `properties.hpp` (`isMyGuiVector`, `Colour`) | the integer coordinate types are used as the arithmetic types of the layout code; `widget->setCoord`, `getSize`, `getParentSize` (the layer size for a root) |
| Text metrics | `text.cpp` `LuaText::calculateSize` (`getTextSize`), `textedit.cpp` `LuaTextEdit::calculateSize` (`getTextSize().height`, `getFontHeight`; **it mutates the edit box inside a const layout call**: `mEditBox->setSize(normalSize)`) | font glyph advances, line height, word wrap and multi-line breaking inside MyGUI's `EditBox`/`TextView` (RECALLED-VERIFY: rules not read this session); font "Default" built by `components/fontloader/fontloader.cpp` with the GUI scaling factor |
| Text drawing | `text.cpp` (`setTextColour`, `setTextShadow`, `setTextAlign` via `alignment.hpp` `alignmentToMyGui`, `setFontHeight`, `setEditMultiLine`, `setEditWordWrap`) | MyGUI draws the glyph quads |
| Text editing | `textedit.cpp` (`MyGUI::EditBox`, `eventEditTextChange`) | caret, selection, key handling, clipboard (`ClipboardManager` implemented in `windowmanagerimp.cpp`), max length; `isTextInput()` drives `SDL_StartTextInput` |
| Images | `image.cpp` (`setImageTexture`, `setImageCoord`, `setImageTile`, `LuaTileRect::_setAlign` for tiling), `resources.hpp` | MyGUI loads the texture by VFS path through the platform data manager; the atlas rectangle is an image coordinate; tiling is MyGUI's `TileRect` |
| Events | `widget.cpp` `registerEvents` (12 MyGUI delegates: key press/release, mouse click, double click, press, release, move, drag, mouse and key set/lost focus); `keyEvent` (`SDLUtil::myGuiKeyToSdl`), `mouseEvent` (`getAbsolutePosition`) | hit test, "needs mouse focus" pick, double-click timing, drag, capture on press, key focus, text input as `keyPress` with `KeyCode::None` and a character (`keyPress`) |
| Bubbling | `widget.hpp` `propagateEvent` | **not MyGUI**: OpenEden's loop up `mParent` |
| Layers and z-order | `layers.hpp/cpp`, `element.cpp` `setLayer` (`LayerManager::attachToLayerNode`, `isExist`), `files/data/mygui/openmw_layers.xml` (22 layers, some `overlapped`, some `pick="false"`) | draw order; `OverlappedLayer` brings a pressed top-level widget to the front (RECALLED-VERIFY) |
| Clipping and alpha | implicit | MyGUI crops children to their parent (`mCroppedParent`, used in `image.cpp`-side `LuaTileRect::_setAlign`; the general rule is RECALLED-VERIFY); alpha multiplies down the tree (`setInheritsAlpha`) |
| Visibility | `widget.cpp` `updateVisible` | works around "parent visibility doesn't affect added children" (a MyGUI bug) |
| Pointer | `widget.cpp` `setPointer("arrow")` | pointer resource names from `openmw_pointer.xml` (`arrow`, `hresize`, `vresize`, `dresize`, `drop_ground`, ...) |
| Textures | `image.cpp` line 52; `windowmanagerimp.cpp` line 2295 | `getTexture(path)` and the engine-created 1x1 `"white"` texture used by `I.MWUI.constants.whiteTexture` |
| Script settings | `adapter.cpp` `LuaAdapter` (a MyGUI widget holding a `LuaContainer`; `eventChangeCoord`), `scriptsettings.cpp`, `settingswindow.cpp` lines 989-1024 | the Settings window is a MyGUI window; the Lua page lives inside it |
| Application glue | `windowmanagerimp.cpp` (`registerAllWidgets`, `clearGameInterface`/`clearMenuInterface`, `onKeyFocusChanged`), `luamanagerimp.cpp` (`isModalAny`, `LuaUi::clearSettings`), `game.hpp` | lifetime and input gating |

**Threading (READ unless marked).** `ui.create/update/destroy/updateAll` and the layer inserts only queue a lambda with `LuaManager::addAction` (`uibindings.cpp`); the queue is applied by `LuaManager::applyDelayedActions`, called from `synchronizedUpdateUnsafe` (`luamanagerimp.cpp` lines 338-410), the main thread. The script keeps mutating `element.layout` between the call and the application, and the application reads the table as it is then. `MyGUI::Gui` is touched only from that point, so the layout pass is single-threaded on the main thread. Event callbacks (`WidgetExtension::mouseMove` and the others) run inside MyGUI's input injection, which the `apps/openmw/mwinput/*` managers and `WindowManager::injectKey*` drive; that this is the main thread is RECALLED-VERIFY (the SDL polling was not traced). `MWLua::Worker` is unused in this tree (0097 stage log), so Lua `update` is on the main thread too. A backend that renders on another thread needs the draw list handed over once per frame; the layout pass itself stays on the main thread.

### 16.3 Options

A common premise first. The element protocol of section 6 (create, set property, parent, class/state, events, layout box query, focus) is an engine-independent handle API (section 13.7: measured 1 protocol call per changed value). The compatibility layer is a client of that protocol, like any other Lua code, plus whatever extra the legacy semantics need.

#### 16.3.1 Option A: keep the `lua_ui` layout code, replace MyGUI by a leaf interface

Change `WidgetExtension` from "is a MyGUI widget" to "owns a protocol handle". The layout maths, `Element` diffing, templates/slots, property parsing, `propagateEvent`, Flex/Container/Window logic and the `Content` library stay. Replace the 235 MyGUI lines by one **`Leaf` interface** with (a) `setCoord(IntCoord)`, `size()`, `parentSize()`; (b) `setVisible`, `setAlpha`, `setInheritAlpha`; (c) `attach(parent)`, `detach()`, `attachToLayer(name)`; (d) text: `setText`, `setFont(height)`, `setColour`, `setShadow`, `setAlign`, `setMultiline`, `setWordWrap`, **`measureText(width) -> IntSize`**; (e) image: `setTexture(path, atlasRect, tile)`, `textureSize(path)`; (f) edit: text change event, read-only, caret and selection are the backend's; (g) pointer; (h) events: the 12 notifications plus `setKeyFocus`. One adapter class per widget type maps these onto protocol calls. In effect A is "un-inherit": each class holds a handle instead of being a MyGUI widget, and everything in 16.0 listed as MyGUI's must be rebuilt or bound.

- **The viable form** puts each Lua widget in RmlUi as `position: absolute` with the integer rectangle **`lua_ui` computed**, so RmlUi is renderer and hit-tester only and never lays anything out. This keeps today's exact arithmetic.
- **What must be rebuilt, not just bound:** (1) a **synchronous text measure** callable inside the layout pass (`calculateSize` runs while the pass runs, repeatedly: `LuaFlex::updateChildren` calls `calculateSize` on every child twice). Doing that through an RmlUi layout would hit the finding of section 13 that the *document* is the layout unit (a text change re-lays out the whole document: 1.15 ms and 6,200 allocations for 200 rows, against 0.026 ms in a separate document); so measure with the font engine directly and **own the line breaking** (a pure function over glyph advances, unit-testable without a window; then draw already-broken lines so RmlUi's wrapper cannot disagree). (2) Clipping to the parent: `overflow: hidden` per element, which uses scissor (axis-aligned) and so does not need the clip mask that RmlUi's Vulkan backend lacks (section 14; masks are for rounded corners; scissor stacking in the render interface is RECALLED-VERIFY). (3) Alpha: MyGUI multiplies alpha into each widget's own geometry; RmlUi's `opacity` on a parent is group opacity and wants layers, which the Vulkan backend lacks (section 14.1). So compute the effective alpha in `lua_ui` and push it per leaf (colour alpha); this also keeps the semantics. (4) Z-order and "raise on press" for `overlapped` layers. (5) Hit testing that matches MyGUI's: a pick on any widget that "needs mouse focus" (RECALLED-VERIFY: MyGUI's default is true, so every Lua widget absorbs the mouse today, with or without handlers) and the layer `interactive = false` option. (6) A text-edit control (below). (7) The tile/atlas image primitive built from explicit UVs (a custom element with its own geometry, not a decorator).
- **One document per root Element** (each `ui.create` root) is the natural unit, because of the document-as-layout-unit finding; with absolute positioning only, a document's layout cost should follow its element count and not text reflow (RECALLED-VERIFY: not measured for absolute-only documents).
- **Effort (mine, +-50%):** L1 extract the `Leaf` interface behind MyGUI with goldens unchanged 8 to 12 days; L2 own text measure and line breaking 5 to 8 days; L3 RmlUi leaf adapters for Widget/Flex/Container/Window/Text/Image, events, layers, clip, alpha, z-order 15 to 25 days; L4 TextEdit 5 to 10 days on an RmlUi form control, 8 to 15 days if a text editor of our own is written instead; plus the test plan of 16.4 (6 to 10 days) and the Settings-window dependency (16.5). **Total about 40 to 65 working days.**
- **Fidelity risks:** text (glyph advances, rounding, wrap rules, shadow offset, vertical alignment inside a line box, the GUI-scale font raster: `FontLoader` is given the scaling factor, `windowmanagerimp.cpp` line 226); integer rounding at tile edges and cropping; hit-test and focus quirks; edit-control behaviour (caret keys, selection, undo, max length: MyGUI's `EditBox` against RmlUi's form control, RECALLED-VERIFY); mouse capture during drag across widgets (the `Window` drag relies on `eventMouseDrag` being delivered to the pressed widget outside its bounds).
- **What breaks for mods:** nothing by design if the corpus passes; the expected differences are sub-pixel anti-aliasing of glyphs, the font's exact outlines (if the backend font engine differs from MyGUI's), and pointer resource names (`pointer` prop) that need a registry on the new side.

#### 16.3.2 Option B: reimplement the layout in the new component library

Rewrite Flex/Container/Window/Text/Image as `openeden.ui` components in Lua (or C++) on RmlUi's own box model and map `ui.create` layout tables onto them. It buys one code path (the compatibility layer *is* the library) and lets legacy and new UI share features (style sheets, focus tree). It costs the most and reproduces the least: every legacy quirk of 16.1 and 16.3.3 must be re-created in the new semantics while the new library is itself still being designed (decision 0015 point 1), and the golden rectangles stop being a regression guard because there would be no unchanged code to compare against. **Effort 12 to 20 weeks (mine, +-50%)**; fidelity risk as in A plus the layout, and mods break wherever the emulation is imprecise. Not recommended before the library has shipped real UI.

#### 16.3.3 Option C: translate layout tables to RmlUi documents and flex CSS

Cheapest on paper (a mapping table, 4 to 6 weeks to "mostly works", **open-ended tail**), and the right idea for *new* UI. It cannot be the compatibility strategy because `LuaFlex` is not CSS flex. Concretely (all READ, `flex.cpp`, `widget.cpp`, `container.cpp`):

1. `grow` is distributed as `static_cast<int>(growFactor * grow)` **per child** (truncated), so the children do not necessarily sum to the container size (`flex.cpp` line 70); CSS flex distributes remainders exactly.
2. `stretch` is `max(secondary(size), int(stretch * secondary(flexSize)))`, a floor and not `align-self: stretch` (lines 71-72).
3. `autoSize` defaults to **true** and means "at least the children's extent on both axes" (`calculateSize`, lines 91-99); and `childScalingSize` **zeroes the primary axis** when auto-sized (lines 82-89) to break the `relativeSize` feedback loop, so a child with `relativeSize` along the primary axis of an auto-sized Flex resolves to 0.
4. `align`/`arrange` use integer division `(container - content) / 2`, which rounds toward zero for negative free space (line 26).
5. `Container` reports `childScalingSize() == 0`, so `relativeSize`/`relativePosition` of its children resolve to 0 (`container.cpp` lines 173-176), and its size is the max of (position + size) of children *and* template children, negative offsets included.
6. Every `position`/`size` is truncated to int (`widget.cpp` 338-339, 355-356) and `anchor` subtracts a fraction of the size *before* truncation.
7. `Text` with `autoSize` takes MyGUI's text size (`text.cpp` line 156); `TextEdit` takes `max(size, text height)` only for single-line or read-only (`textedit.cpp` 202 and 231-241).

These are the **discriminator between A and C**: the same layout table gives different rectangles. C is the way to *write new UI*, not to keep old UI.

**Comparison**

| | A: keep layout, new leaf | B: reimplement layout | C: tables to CSS |
| --- | --- | --- | --- |
| Reuses today's tested arithmetic | yes | no | no |
| Golden rectangles can be matched | exactly (text aside) | only by emulation | no |
| Effort (mine, +-50%) | 40 to 65 days | 12 to 20 weeks | 4 to 6 weeks, open tail |
| Main risk | text measure, edit control, z-order and hit-test semantics | everything | visible layout drift in mods |
| Layout cost per update | our pass (integers) | RmlUi document layout | RmlUi document layout |
| Depends on the new library | no (only the protocol) | yes | no |

#### 16.3.4 Threading and update cost under A

Layout runs on the main thread inside `applyDelayedActions`; the protocol calls follow immediately on the same thread, so no synchronisation is needed. `Context::Update/Render` is also on the main thread (section 13: 0.04 ms to build the list for 212 elements). The draw list goes to the GUI command graph as MyGUI's vertex buffers do now. RmlUi events are delivered during `Context::Process*` on the main thread; the `Leaf` event callbacks call `WidgetExtension::mouseMove` and the others unchanged. Texture loads (`ui.texture` paths) are on-demand reads through the VFS in the render interface's `LoadTexture`; they must not block the frame (how MyGUI's data manager does it today was not read: RECALLED-VERIFY).

### 16.4 Fidelity test plan

**Principle:** record before changing. The corpus and its goldens are produced from the **current MyGUI-based** implementation, committed as golden data (never regenerated automatically; changes reviewed by toddhow, per the guide), then L1 (the `Leaf` extraction, still on MyGUI) must pass them unchanged, then L3 (RmlUi) must pass them within the stated tolerances.

**1. What to record.** Lua cannot read rectangles (16.1), so the recorder is C++: walk the `WidgetExtension` tree (`children()`, `templateChildren()`, `widget()->getCoord()` or `getAbsoluteCoord()`) of an `Element::mRoot` and emit `path / type / name / left top width height / visible / alpha` as canonical JSON. The runner needs no Lua manager: `Element::make(layout, false)` and `Element::create()` are public (`element.hpp`) and take a `sol::table`; callbacks are not part of the geometry corpus. Add an `Element::update()` pass for every case (layout mutated, then `update`) because `updateContent` diffs.

**2. Can the layout be computed without a window?** Partly.
- **Text-free layouts: probably yes.** The installed MyGUI headers contain `MyGUI_DummyPlatform.h` and `MyGUI_DummyRenderManager.h` (**READ**: `deps/vcpkg-x64-windows-2022-2026-02-24/installed/x64-windows/include/MYGUI/`). A `components-tests` file would create a `MyGUI::Gui` on them, call `LuaUi::registerAllWidgets()`, load `openmw_lua.xml`, create a layer of fixed size, and run the recorder. Open (RECALLED-VERIFY, not tried): what size the dummy render manager's view reports (the root's `getParentSize()` depends on it), and whether skins load without textures. No existing test creates a `MyGUI::Gui` (`grep MyGUI` over the three test directories finds only mwbase fakes and unrelated contract tests), so this is new harness work, 3 to 5 days. The only existing `lua_ui` test is `apps/components_tests/lua/testuicontent.cpp` (Content only).
- **Text layouts: only with a font.** `getTextSize` needs the "Default" font, which `FontLoader` builds from a TrueType or bitmap file through `RenderManager` textures; whether that works on the dummy render manager is **unverified**. Two escapes: inject a fixed-advance bitmap font generated by the test (then the corpus tests the *arithmetic*, which is the point of A), or record text cases in the real game in a quiet window (`OPENEDEN_QUIET_WINDOW=1`) with a debug dump hook.

**3. Corpus content.** Tier 1, text-free, exact match (0 px): each property of Widget alone and combined (`position`, `size`, `relativePosition`, `relativeSize`, `anchor` including negative and above 1, `visible`, nested visibility); Flex in all combinations (`horizontal`, `align` and `arrange` 3 x 3, `autoSize`, `grow` with fractions that truncate, `stretch`, mixed `relativeSize` children, nesting 3 deep, empty); Container with template children, `slot` templates, templates overriding `props`; Window with `move`/`resize`/`action` and a scripted drag sequence; Image with atlas rectangles, tile flags and `size = 0` (texture size) cases; content mutation (insert, remove, replace type, reorder, a shared Element used as child) through `update()`; caps (1e7) and the depth limit (250). Tier 2, text, exact on an injected fixed-advance font and **tolerance-based (+-1 px, equal line count) on the real font**: autoSize text, multiline, `wordWrap` at narrow widths, alignment 3 x 3, `textSize`, TextEdit autoSize, newlines and long words, `textShadow`. Tier 3, events: injected pointer sequences (move, press, release, click, double click, drag) over nested widgets with `propagateEvents` on and off, checking the `MouseEvent.position/offset/button` values and the order of callbacks; focus gain and loss; key and text input to a TextEdit. These need the input path of the new backend and run against both.

**4. Screenshots.** A menu-context gallery script (test data, no game text) lays the corpus out on a grid; capture with `--auto-screenshot` and `OPENEDEN_FIXED_FRAME_TIME` as the repository's other checks do. MyGUI against MyGUI is the repeat-noise baseline; RmlUi against MyGUI is judged per region: boxes and tiles exact except edges (tolerance 1 px), text by PSNR plus human viewing. Never claim a pixel match for text.

**5. What the corpus cannot prove:** real mods (none available, 16.9), mouse capture feel, performance with thousands of elements, IME.

### 16.5 What continues to need MyGUI until the 46 windows are ported

| Lua surface | Needs MyGUI because | Until |
| --- | --- | --- |
| `ui.registerSettingsPage` / `removeSettingsPage`, `omw/settings/*` pages | `SettingsWindow` is a MyGUI window; it hosts the Lua page through `LuaAdapter` (`settingswindow.cpp` 294, 989-1024; the **only** user of `LuaAdapter`, verified by grep) | the Settings window is ported, or the adapter shows an RmlUi surface as a texture inside a MyGUI widget with input routed by UV (the render-to-texture path of section 8; section 13 measured the pick round trip at 0.0 px, analytic) |
| `ui.showMessage`, `_showInteractiveMessage` | `MessageBoxManager`, dialogue window | those windows' port |
| `ui.setConsoleMode`, `getConsoleMode`, `printToConsole`, `setConsoleSelectedObject`, `omw/console/*` | the console window | its port (or leave: a developer window) |
| `I.UI` `setMode`/`addMode`/`removeMode`, `_setUiModeStack`, `_getAllowedWindows`, `_setWindowDisabled`, `_isWindowVisible`, `registerWindow` (a Lua element replaces a built-in window) | the `GuiMode` stack opens real MyGUI windows; "replace a window" hides the MyGUI one and shows a Lua element in its place | each window's port; the `_set*` calls stay MyGUI-backed until then. `registerWindow` is the one place where a **Lua element must appear in the same layer band as MyGUI windows** |
| `_setHudVisibility`, `_isHudVisible` | HUD windows | HUD port |
| `layer = 'HUD'` / `'Windows'` / `'Modal'` ... in a layout | layers are MyGUI's (`openmw_layers.xml`, 22 names); `ui.layers.insertAfter('HUD', ...)` indexes the same list; a Lua element in `Windows` interleaves with MyGUI windows (and raises on press) | both backends need one ordered layer list with the decision-0015 draw bands; Lua elements in a MyGUI layer must draw in that band |
| `Layer.size` | MyGUI layer size, scaled by the scaling layer | the new system's per-layer logical size (resolution over GUI scale) |
| `pointer` property | MyGUI pointer resources | a pointer registry on the new side; one cursor owner |
| `ui.texture` of `'white'` | engine-created MyGUI texture | the new backend registers the same name |
| `I.MWUI` templates | pure Lua over `ui.texture` of VFS paths (`textures/menu_*`) | nothing: they work over any backend |

**Consequence:** the switch of `lua_ui`'s backend cannot happen before either (i) the Settings window and the layer/band design are solved, or (ii) coexistence is done for those two things. The plan below therefore does all backend-independent work first and keeps Lua UI on MyGUI until the Morrowind stage, as decision 0015 point 6 already says.

### 16.6 Staged plan and recommendation

**Recommendation: Option A, in the order below; Option C only for new UI; Option B not planned.**

| Stage | Content | Effort (mine, +-50%) | Notes |
| --- | --- | --- | --- |
| L0 Corpus and recorder | the C++ recorder, headless MyGUI harness (or the in-game dump), Tier 1 and Tier 3 corpus, goldens recorded from today's code | 7 to 10 days | needs nothing else; **can start any time** (it also guards every other change to `lua_ui`); tests first per the guide |
| L1 `Leaf` extraction on MyGUI | `WidgetExtension` stops inheriting MyGUI types; `MyGuiLeaf` implements the interface; goldens pass unchanged; `CI/check_layer_rules.sh` gets a rule that `lua_ui` outside the MyGUI leaf directory includes no MyGUI header | 8 to 12 days | full loop; touches `components/lua_ui` and `windowmanagerimp.cpp` glue; behaviour unchanged |
| L2 Own text measure and wrap | pure line breaking over a `FontMetrics` interface; MyGUI implementation of the metrics; compare against `getTextSize` on the real font (Tier 2) | 5 to 8 days | shows how far MyGUI's rules are reproducible before RmlUi is involved |
| L3 RmlUi leaf (after decision-0015 U1) | adapters on the element protocol; layers mapped to bands; clip, alpha, z-order, hit-test, key focus | 15 to 25 days | only after U1 (backend, bands, arbiter) exists |
| L4 TextEdit | RmlUi form control or an own editor, behind `Leaf` | 5 to 10 days (8 to 15) | decide after a small spike |
| L5 Settings page and `registerWindow` | context-as-texture in a MyGUI widget, or port the Settings window first | depends on the window plan | the gate for turning the backend on |
| L6 Switch | a setting `[GUI] lua ui backend`, default MyGUI, flipped on toddhow's word after the corpus and a hands-on pass | 3 to 5 days | the MyGUI path stays until the last window is ported |

Stages L0 to L2 are useful even if the UI project stops: they remove MyGUI from `lua_ui` behind an interface and give `lua_ui` its first geometry tests.

### 16.7 Open questions for toddhow

1. **Option A as the compatibility strategy** (recommended), and C only for new UI?
2. **Start L0 now?** It needs no RmlUi and guards today's code (recommended yes; decision 0015 says no UI work before Track V and game-pack stage d, so this is an exception to ask for).
3. **Exact fidelity or "good enough" for text?** Recommend exact rectangles for text-free layouts and +-1 px with equal line counts for text; glyph shapes follow the new font engine.
4. **TextEdit:** an RmlUi form control first (cheaper), an own editor only if a mod-visible behaviour (undo, selection keys) regresses? (Recommended.)
5. **GUI scale:** keep today's semantics (layout in unscaled layer units, fonts rasterised at the scaled size)? (Recommended; it needs metrics at the scaled size, which can change wrapping at fractional scales.)
6. **Settings window first or the texture bridge first** (16.5)? The window port is more work but ends MyGUI sooner; the bridge unblocks the backend switch earlier.
7. **`pointer` property:** keep the MyGUI names (`arrow`, `hresize`, ...) as a registry in the new system?
8. **Accept that no real third-party mod corpus exists here** (16.9), and give me a few Lua UI mods for a hands-on check before the flip?

### 16.8 What I could not verify

- MyGUI's own rules, from memory only (RECALLED-VERIFY; `deps/` and the vcpkg install hold headers only, no `.cpp`): word-wrap and line-breaking rules of `EditBox`/`TextView`, the text shadow's offset, default "needs mouse focus", `OverlappedLayer` raise-on-press, child cropping, mouse capture on drag, whether `DummyRenderManager`/`DummyPlatform` can host skins and a TrueType font (headers exist; behaviour not tried), the dummy view size.
- RmlUi: scissor stacking in the Vulkan render interface, opacity without layers, form-control behaviour, absolute-only document cost: not tested (only read in sections 13 and 14).
- That input is injected on the main thread (the SDL polling was not traced).
- Nothing was built or run; all estimates are mine; no frame, no timing.
- Whether any real Lua UI mod depends on a behaviour outside the documented API (16.9).

### 16.9 Corpus scan (aggregates only)

- Built-in Lua scripts under `files/data` that use `openmw.ui`: **15 files** (`openmw_aux/ui.lua`, `omw/ui.lua`, `omw/mwui/*` (7), `omw/settings/{menu,renderers}.lua`, `omw/console/{menu,player}.lua`, `omw/input/{playercontrols,settings}.lua`, `omw/mechanics/playercontroller.lua`). Their widget use: `ui.TYPE.Flex` 14 uses, `Image` 10, `TextEdit` 4, `Container` 3, `Text` 3, `Widget` 1 (no `Window`). Functions they call include `ui.create`, `ui.texture`, `ui.setConsoleMode`, `ui.showMessage`, `ui.registerSettingsPage` and `ui.updateAll` (the last only in a comment); none inserts a layer.
- `I:\Projects\example-suite`: 1,006 files, **no `.lua` file** (assets only: dds, wav, dae, blend and similar); `scripts/` of the repository: 13 Lua files, **none uses `openmw.ui`**; the test profile (`OpenEden-work/testprofile/main/config/openmw.cfg`) lists Morrowind, Tribunal and Bloodmoon only; the installed Morrowind `Data Files` hold no Lua and no `.omwscripts`. (The `.claude/worktrees/` copies of `files/data` were excluded from every count.)
- So **no third-party Lua UI mod is available on this machine**: the only corpus is the engine's own 15 scripts. Every claim above about mod compatibility is therefore by design, not by test.

## 17. U2 built: input, scaling, resize and the Slang twins (2026-11-11, task 0111)

Status line for decision 0015 and for this note: **the U0b/U0c findings about input held in the engine.** What was built and what was found, in order of surprise (the stage log of `docs/tasks/0111-ui-u2-input-scaling.md` has the evidence).

- **RmlUi decides what is "over a document" itself.** `Context::IsMouseInteracting()` is true while the cursor is over any element that is not the root, so a full-screen document would take every click: such documents must put `pointer-events: none` on their `body`. The routing rule needs nothing else from the document model.
- **Focus is the keyboard's switch.** Keys, text and IME go to RmlUi only while a text input has the focus (`input` of a text type or `textarea`); a focused button does not take game keys. The controller is opt-in per document with `nav-capture` on its `body` and only while a control inside it has the focus; the first focus has to come from the mouse (or Tab, which is not routed).
- **`nav: auto` is enough for d-pad navigation**: `ElementDocument::ProcessDefaultAction` reads the focused element's `nav-*` property (set through the style sheet), moves the focus spatially, and Return on an element with `tab-index: auto` clicks it. B (Escape) drops the focus so the game gets the pad back.
- **SDL_TEXTEDITING was dropped by the engine** (`InputWrapper` read it and did nothing): the IME path needed a new virtual on `SDLUtil::KeyListener`. The composition logic is RmlUi's own SDL backend's (MIT), written against `Rml::TextInputContext` so a fake can test it.
- **The wheel leaks through the running total.** `MouseMotionEvent::z` is a running sum that the mouse manager forwards to MyGUI with every move, so a wheel the overlay takes had to be taken out of that sum as well, or a MyGUI widget under the panel scrolls too.
- **`dp` makes the scale trivial**: the context stays in framebuffer pixels, `SetDensityIndependentPixelRatio(scaling factor)` and sizes in `dp`; the bitmap font engine's existing `chooseSize` then gives nearest at whole scales and linear in between with no new code. Seen at 1.5 and 2 next to MyGUI's own scaled menu.
- **Resize needed nothing new** beyond U1's per-frame extent: the panel keeps its pixel size and place at 960 x 540 and back.
- **The Slang twin matched the GLSL exactly** in the panel (0 differing pixels outside the changing frame rate line, largest channel difference 1); `-matrix-layout-column-major` with `mul(matrix, vector)` reads the push-constant matrix as the GLSL does.
- **Testing aid.** An environment variable (`OPENEDEN_RMLUI_INPUT_SCRIPT`) makes the engine push synthetic SDL events at chosen frames of a running game, which is the first way this project has to drive the real input path in a quiet-window game; its frame counter must not count loading screens (the first runs' timing moved by about 1,000 frames with the shader cache's temperature).
- **Not verified:** a real controller, a real IME, a high-DPI display (SDL window pixels are assumed to be framebuffer pixels), clipboard (not wired), the Linux build, behaviour with several documents or a modal one, input into a render-to-texture surface (later).
