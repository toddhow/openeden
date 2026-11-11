# The RmlUi UI backend: `components/ui_rml`

Decision 0015, tasks 0104 (U1) and 0111 (U2). Off by default behind `[GUI] rmlui`; with it off nothing here is created.

## Threads

- RmlUi has global state and runs on **one thread only: the one that records the GUI command graph** (the one MyGUI also draws on). `Overlay::Impl::frame` is the only place that calls into the context.
- The game's input arrives on the main thread. `Overlay` implements `InputTarget`: its calls append to a bounded queue (512 events, then dropped and counted) that `frame` takes at its start. The routing questions the main thread has to answer (over a document? text field focused? controller captured?) are answered from atomics that `frame` publishes after `Update`, so they are at most one frame old.
- `syncTextInput`, `setGuiScale` and `setExtent` are main-thread calls; the first is the only one that touches SDL.

## Input rules (task 0111, spec in `docs/tasks/0111-ui-u2-input-scaling.md`)

- Decisions live in `InputRouter` (pure, tested); `apps/openmw/mwinput/uiinputfilter.*` applies them between SDL's dispatch and the managers. With no overlay the filter forwards everything unchanged.
- A mouse press goes to the overlay only over an element that takes pointer events (`Context::IsMouseInteracting`). A full-screen document must put `pointer-events: none` on its `body`, or it takes every click.
- Keys, text and IME go to the overlay only while a text input has the focus. A focused button does not take game keys.
- A document opts into the controller with `nav-capture="true"` on its `body`; while a control in it has the focus the d-pad (arrows), A (Return), B (Escape, which also drops the focus) and the right stick (scroll) are the overlay's.
- Everything is GUI mode only. Leaving it makes the overlay let go of its focus.
- A focus entry key or controller button (`[GUI] rmlui focus button` / `rmlui focus key`, default unbound) moves the controller focus into the first visible `nav-capture` document, or out of it again (`controllerfocus.*`); the router takes the press and drops its release, only in GUI mode.
- The clipboard is `ClipboardBridge`: RmlUi's copy is queued on the GUI thread and written to SDL by `Overlay::syncTextInput` (main thread); a paste chord (`isPasteChord`) makes `Overlay::key` read SDL's clipboard on the main thread before the key is queued. Never call SDL's clipboard from the GUI thread.

## Scaling

The context is in framebuffer pixels. `[GUI] scaling factor` becomes the density-independent pixel ratio (`dpRatioFor`); write sizes in `dp`. Bitmap fonts draw nearest at whole-number ratios to their design size, linear otherwise (`chooseSize`).

## Pitfalls

- RmlUi has no default style sheet: `div` is inline until a sheet says otherwise; scrollbar elements have no size until styled.
- A text change re-lays out its whole document: give live values their own small elements.
- Vertex data stays unwritten unless the compiled buffers go through `CompileContext::onCompiled`.
- `vsgUtil::viewerCompileMutex()` keeps `Viewer::compile` and loader compiles apart; do not compile on another thread without it.
