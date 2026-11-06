# 0045 — A UI system to replace MyGUI: state, requirements, options, plan

Date: 2026-11-06 · Author: Claude, at toddhow's request · **Research note, docs only.** Nothing is built, no dependency is added, no code was run. Draft decision: `docs/decisions/0015-ui-system.md`.

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
- **U0 Spike (go/no-go, throwaway):** build RmlUi against the pinned VSG as a standalone test; render one window document with RCSS to the swapchain through a minimal `RenderInterface` on VSG; measure draw calls, per-frame allocations, CPU time for 500 elements; render a Context into an offscreen image and sample it on a quad in the 3D scene; push mouse events through a UV; load a bitmap font. Output: a note with numbers and a go/no-go table, nothing merged into the engine.
- **U1 Element protocol and backend:** the engine-independent protocol, RmlUi render interface and font engine, input/IME bridge, one `Layer` in the GUI graph, coexistence rules with MyGUI (bands, arbiter). Tests for layout results and event routing.
- **U2 Reactive library and base components in Lua:** `Value`/`Computed`/`Effect`/scopes, the component set of 6.1, the default neutral theme, focus model (tab order, spatial navigation hooks), l10n integration, tests with a headless backend.
- **U3 Theme system:** tokens, style sheets from the VFS, overrides, high-contrast and scale variants; Eden Studio preview module with live reload.
- **U4 First pack UI:** Skyrim SE HUD and menus (needs the game pack work of decision 0013 and the descriptor mechanism of 0014 for `core.skyrim.*`); in-game debug overlay.
- **U5 Render to texture and input routing** to world surfaces.
- **U6 Gamepad navigation.**
- **U7 Morrowind migration** window by window, `lua_ui` compatibility, then MyGUI removal.
Ordering constraints: U4 waits on the game pack tier (decision 0013 stages d to f); the root guide's current priority is Track V and the extension system, so U0 is a spare-time research stage, not an interruption.

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
