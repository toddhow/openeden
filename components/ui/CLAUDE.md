# The element protocol: `components/ui` and `components/ui_lua`

Decision 0015, task 0162. Engine-independent: nothing here names RmlUi, MyGUI or a renderer.

- `ElementProtocol` is what a UI engine offers (create, createDocument, destroy, insertBefore, detach, setText, setClass, setProperty, setAttribute, listen, focus); `Backend` adds ids, `commit()` and events. Ids are numbers (8 bit session, 24 bit counter); an id an implementation does not know is ignored.
- Implementations: `HeadlessBackend` (tests: tree plus call log), `CommandQueue` sessions (the real path: scripts write on the main thread, `commit()` once per frame, the GUI thread `drain`s into `UiRml::RmlProtocol` at the start of `Overlay::Impl::frame`; events come back through per-session inboxes).
- `ui_lua/backendpackage.*` is the Lua face (package `openeden.uibackend`, `open()` gives a session table). `commit()` of a session that wrote nothing does nothing, `poll` with no events allocates nothing.
- The library itself is Lua: `files/data/openeden/ui.lua` and `ui/reactive.lua` (VFS scripts, `require('openeden.ui')`). Tests load them from the source tree (`apps/components_tests/ui/`).

## Pitfalls
- RmlUi's unit of layout is the document: a value that changes every frame goes in its own small `ui.Hud`.
- A component built outside the `build` function of a `ui.Document`/`ui.Hud` (or `ui.withScope`) is an error: the scope owns its effects.
- Inline properties set by a component (flex layout, list row positions) beat a theme's style sheet; looks (colours, fonts, spacing) belong in `ui/base.rcss`.
- Event handlers run inside `ui.update()`; one that raises does not stop the others, the first error is raised after all ran.
