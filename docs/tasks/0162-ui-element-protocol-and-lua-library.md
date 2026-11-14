# Task: The UI element protocol and the `openeden.ui` Lua library (decision 0015, stage U1b/U2 of research note 0045)

Status: DRAFT, requested by toddhow on 2026-11-13 ("Can you start on the ui stuff"). First slice only.
Design doc section: `docs/research/0045-ui-system.md` sections 6 (components, reactive model), 10 (stages), 13.5 to 13.7 (document scope, shared interface, the spike's reactive layer), 14 to 17; `docs/decisions/0015-ui-system.md` points 1, 3, 5.
Goal: a small engine-independent element protocol with an RmlUi implementation and a headless one, and a Lua component library (`openeden.ui`: reactive values, scopes, a base component set with a virtualised list) that talks only to that protocol, shown by the debug panel rewritten as a Lua document.
Inherited code touched (at commit `0f4c82e94f`): `components/ui_rml/overlay.{hpp,cpp}` (`Overlay::Impl::frame`, `OverlayConfig`), `apps/openmw/vsgengine.cpp` (overlay creation), `apps/openmw/mwlua/luabindings.cpp` and `luamanagerimp.*` (package registration), `components/settings/categories/gui.hpp`, `files/settings-default.cfg`, `components/CMakeLists.txt`, `files/data/CMakeLists.txt`, `CI/layer_rules.txt` (new `dir` lines only). MyGUI and `components/lua_ui` (the existing Lua UI) are not touched.

## Design

### The protocol (`components/ui`, layer `engine`, standard library only)
- `Ui::ElementProtocol` (write side): `create(id, tag)` (detached), `createDocument(id, name)`, `destroy(id)` (element and descendants; an unknown id is ignored), `insertBefore(parent, child, before)` (`before` = 0 appends; moves an attached child), `detach(child)`, `setText`, `setClass`, `setProperty` (inline style: name and value as text, `dp` units allowed), `setAttribute`, `listen(id, kind, on)`, `focus(id)`.
- `Ui::Backend : ElementProtocol` adds `allocateId()`, `commit()` (what was written since the last commit becomes visible to the consumer as one batch) and `takeEvents(out)`. Events are `{target, kind, value}`; kinds `click`, `change`, `focus`, `blur`, `scroll` (value = scrollTop in dp).
- Ids are 32 bit: the top 8 bits name the session (one per script that opens the backend), the low 24 bits count. Events return only to their session.
- Implementations: `Ui::HeadlessBackend` (records every call, keeps the element tree, `emit` simulates an event; used by tests), `Ui::QueueBackend` over `Ui::CommandQueue` (a thread-safe batch queue: the Lua side writes on the main thread, commits once per frame, the GUI thread drains it; events come back through per-session inboxes), and `UiRml::RmlProtocol` (the consumer: applies commands to an `Rml::Context`, one `Rml::ElementDocument` per `createDocument`).
- Threads: RmlUi runs only on the GUI record thread (components/ui_rml/CLAUDE.md), the Lua scripts on the main thread, so the real path is always queue then apply at the start of `Overlay::Impl::frame`, before `Context::Update`. A document under construction is never drawn half built: only committed batches are applied.
- Not in this slice: a layout box query (it needs a synchronous read; with the GUI on another thread it will be published per frame), render to texture, theme loading beyond `ui/base.rcss`.

### The Lua library (`files/data/openeden/ui/*.lua`, a VFS script package, `require('openeden.ui')`)
- Reactive: `Value`, `Computed` (lazy; an unchanged recompute keeps its version), `Effect`, `Scope` (owns effects, cleanups, child scopes), one `flush` per frame running dirty effects once each in creation order (the spike's design, note 13.7; effects that dirty effects loop with a guard of 100 rounds and then raise a located error). No closure or table is created in a frame where nothing changed.
- Components run once and return nodes. A property is a plain value or a `Value`/`Computed`; a reactive one installs one effect that calls the protocol only when the value changed. Base set: `Document`, `Hud` (a document whose body takes no pointer events), `Frame`, `Row`, `Column`, `Stack`, `Text`, `Button`, `Image`, `TextInput` (two way `value`), `List` (keyed, virtualised from the start: fixed row height, only rows in view plus an overscan exist, recycled by key), custom components are plain Lua functions.
- The document-scope rule (note 13.5): a `Document`/`Hud` is the only thing that owns an RmlUi document; a value that changes every frame goes in its own small `Hud`.
- Focus hooks: `focusable`, `autofocus`, `onFocus`/`onBlur`, `ui.focus(node)`, `nav` (maps to RmlUi `nav-*`), no gamepad logic.
- `ui.update()` is the one per-frame call (poll events, flush reactive, commit).
- Style: classes and inline `style` tables only; looks come from `ui/base.rcss` in the VFS if present (another agent's theme work: class names `hud`, `window`, `row`, `column`, `stack`, `text`, `button`, `image`, `input`, `list`, `list-row` are used and kept stable) plus a minimal built-in layout sheet written into each document so the library works with no theme.

### In the game
- `[GUI] rmlui` (existing, off by default) creates the overlay as before; the overlay now also owns a `CommandQueue`. With it on, player and menu scripts get the package `openeden.uibackend` (open a session, the calls above). With it off the package is absent and nothing is created.
- New setting `[GUI] rmlui debug panel` (default true): false does not create the C++ debug panel, so a Lua document can be compared with it. In the test profile.
- Demo: `tools/ui_check/mod` (an `.omwscripts` content file, not in `builtin.omwscripts`): the debug panel as an `openeden.ui` document, the frame rate line in its own `Hud`.

## Inputs / outputs
Types: `Ui::ElementProtocol`, `Ui::Backend`, `Ui::Event`, `Ui::CommandQueue`, `UiRml::RmlProtocol`. Files: `components/ui/*`, `components/ui_lua/*` (the sol binding of a `Backend` as a Lua table, engine layer), `components/ui_rml/rmlprotocol.*`, `files/data/openeden/ui/*.lua`, `apps/openmw/mwlua/uibackendbindings.*`.

## Constraints
- Off by default; with `[GUI] rmlui` off no queue, protocol or package exists.
- No heap allocation in a frame where nothing changed (queue, binding and Lua side); zero protocol calls.
- Determinism: effects run in creation order; no clock in the library.
- Threads: stated above; `CommandQueue` and the event inboxes are the only shared state.
- Layer rules: new `dir components/ui engine` and `dir components/ui_lua engine` lines, no new exceptions.

## Acceptance criteria
- [ ] Reactivity (headless): an Effect runs once per change however many writes, a Computed whose result is unchanged does not wake its readers, nested scopes clean up (cleanups in reverse order, effects stop), a cycle is a located error.
- [ ] A component tree produces exactly the expected protocol calls (a `Text` with a reactive string: 1 create, 1 setText, 1 attach, then 1 setText per change).
- [ ] `List` virtualisation: 10,000 items, only the visible rows plus overscan exist as elements; a scroll event creates and destroys only the rows that enter and leave; a changed item list with equal keys reuses rows.
- [ ] A frame with nothing changed makes 0 protocol calls and allocates 0 bytes of Lua memory; `CommandQueue::commit` of an empty batch allocates nothing.
- [ ] `RmlProtocol` over a headless `Rml::Context`: documents, nesting, classes, text, inline style, listeners and event values behave as the protocol says; destroying a document destroys its elements.
- [ ] The queue delivers batches in order and only committed ones; events return to their own session.
- [ ] With `[GUI] rmlui` off: the fixed Balmora frame `1c5d47ad0b5dc32d...` and the `tools/worldobjects_check` dump equal main's build.
- [ ] In game (quiet window): the Lua debug panel document draws; the frame rate line updates every frame in its own `Hud`; frames viewed; CPU per frame compared with the C++ panel.

## Out of scope
Themes and `ui/base.rcss` contents, MyGUI and `lua_ui` changes, `lua_ui` compatibility (note 16), render to texture, gamepad navigation logic, l10n integration, a markup format, Dropdown/Slider/Tabs/Window and the other components of note 6.1 (listed as the next slice), saving UI state.

## Stage log (2026-11-13; same hand wrote tests and code; no fresh reviewer; Windows only; local commits on branch `uilib`, rebased onto `8218d5a14c`, not pushed)

**Built.** `components/ui` (protocol, `HeadlessBackend`, `CommandQueue`), `components/ui_lua` (`openeden.uibackend`), `files/data/openeden/ui.lua` and `ui/reactive.lua`, `UiRml::RmlProtocol`, the overlay owning a queue and draining it before `Context::Update`, `LuaManager::setUiQueue` (player and menu scripts only, read only package), `[GUI] rmlui debug panel` (default true), `tools/ui_check` (demo mod and `check.sh`).

**Tests first.** 63 of 120 new `Ui*` tests failed against stubs (C++ stubs, then Lua stub; the rest test absence and passed); after the code all pass. The RmlUi protocol tests (19) and the openmw-tests allocation test were written after the code and are not shown red. `components-tests` filtered to `Ui*:*Theme*`: 273 pass; `UiAllocation` (0 operator-new allocations over 200 idle frames through a real queue session) passes. Not run: the whole of either test binary, the Linux gate, clang-format 14, clang-tidy, a fresh review, mutation checks.

**What a frame costs (RTX 4070, 1280 x 720, Seyda Neen start, fixed frame time, 1067 frames; frame wall time over the last 400 frames)**

| variant | overlay frame on the GUI thread (mean us: drain / update / render) | median wall ms (p95) |
| --- | --- | --- |
| `[GUI] rmlui` off | none | 3.78 (5.16) |
| rmlui on, nothing in it | 0.6 / 5.2 / 6.5 | 3.87 (5.06) |
| C++ debug panel | 0.5 / 12.8 / 39.5 | 3.76 (5.03) |
| Lua panel, counter text every 8 frames | 1.3 / 12.8 / 33.6 | 3.69 (4.92) |

All four medians are inside run noise. After the rebase onto the theme work (second runs): C++ panel 0.4 / 11.4 / 34.9 us, Lua panel 1.5 / 13.5 / 34.7 us.

**Findings.**
- **A value that changes EVERY frame is not drawn**: `UiRml::RenderInterface` makes geometry drawable one frame after it was compiled (`compilePending`, `mDrawable`), and a text change makes RmlUi build new geometry every frame, so the newest geometry is never old enough (a per-frame counter Hud showed nothing; the same Hud updated every 8 frames shows in 7 of 8 frames). The U1 panel hides this by updating its frame rate line twice a second. The cost of the per-frame updates was measured (drain 75 us a frame with a per-frame counter), but the picture needs a render interface change (upload in the same frame) that is not part of this task. Decision for toddhow.
- A `ui.List` inside a flex column needs an explicit `width` (a percentage width of its rows resolved to nothing: the rows showed one letter); not explained, worked round with the `width` property.
- RmlUi layout is per document: the per-frame cost above is dominated by the two panels' own layout, not by Lua (a Lua `ui.update()` with nothing changed makes 0 protocol calls and 0 bytes of Lua allocation; the idle commit is skipped).
- `getRealFrameDuration` is 0 under `OPENEDEN_FIXED_FRAME_TIME`; use `core.getRealTime`.
- Rebased onto 0163: documents are loaded from `ui/document.rml` (links `ui/base.rcss`, so a theme change reloads them; falls back to memory when the file is absent), components carry the base sheet's classes (`btn`, `input text`, `list scroll`), the demo uses classes and tokens only, no colour literals; the C++ panel (`ui/debugpanel.rml`) and the Lua panel COEXIST: the Lua one replaces it only with `rmlui debug panel = false`.

**Off means unchanged.** With `[GUI] rmlui` off (default): a fixed-time Balmora frame (`--start Balmora --random-seed 7`, frame 600, plain config) has the same SHA-256 as the reference build of `0f4c82e94f` (`0d6dd399...`; not the `1c5d47ad...` of the changelog, which was taken with another configuration), and `tools/worldobjects_check` (110 lines) is OK against it. The reference was an older tree (theme work absent), so the comparison also covers 0163's off path.

**Decisions for toddhow.** (1) The per-frame geometry latency of the render interface (above). (2) `Button` and `TextInput` carry the classes of the base sheet (`btn`, `text`): a theme author styles them there; the library's inline properties (flex layout, list positions) still beat a theme. (3) `ui.update()` as the one per-frame call a script makes (no engine hook yet); a hook that calls it for every script that required the library is a small follow-up. (4) Ids: 8 session bits, so at most 255 scripts open the backend at a time.

**Not done (next slice).** Checkbox, Radio, Toggle, Slider, Dropdown, Tabs, ScrollView, Window, Menu, Tooltip, Grid, Icon, ProgressBar, l10n integration, a layout box query, gamepad logic beyond the `nav`/`focusable` hooks, clicks and typing in the Lua panel in a running game (events were proven through the queue in unit tests, not with synthetic SDL events), the Linux build.
