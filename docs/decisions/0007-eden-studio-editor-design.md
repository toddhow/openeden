# 0007 — Eden Studio's editor design

Date: 2026-10-31 · Decided by: toddhow

## Decision
- **Core systems' editor tools are separate editor libraries (editor modules).** CreativeMotion, AdvancedBodies and ComplexClothing are core systems (decisions 0001 and 0003), so their tools do not come from extension manifests. Eden Studio loads them from a fixed list of core systems. The game never loads them.
- **The plan's design stays: a separate editor library that the game never loads.** The SDK's runtime-side hooks (`EditorService`, `EditorWorkspace`, `EditorInspector` and `EditorGizmo` in `sdk/include/openeden/editor.hpp`) are removed when the editor API is built, in Phase 3 of the Eden Studio plan.
- **Eden Studio keeps the editor's own render-only `SceneHost`** (Track V stage V6, task 0014) for its viewport. Phase 1 adds the Jolt world and the extension host beside it, read-only at first. Whether Studio converges with the game's renderer is decided later.

## Why
- **A fixed core list keeps "no feature code in Eden Studio"** (plan sections 2.2 and 9), and the Editor Module API has real users from the start.
- **One way to add a tool, and no editor code in the game:** the hooks would leave editor-facing code in every runtime library, for tools the game never shows.
- **The V6 viewport is built, and the game's `MWRender::RenderManager` is bound to the game's world types,** so refactoring it first would be a large change for no editing gain now. A tool still shows what the game does, through the shared Jolt world and extension host.

## Consequences
- **Hooks removed in Phase 3:** `EditorService` and the three hook classes go, with their implementation (`components/extension/editor_service.*`), their tests and the `Host` accessor, when the editor API is built. Until then they stay and nothing uses them. Removing the accessor changes `Host`'s layout, so the SDK version question comes back at that point.
- **Studio's Phase 1 scope:** the `SceneHost` viewport, plus a Jolt world with its debug draw pass and the extension host, read-only. Neither exists in the editor yet (on the `vulkan` branch at `ca9f6283d8`).
- **Plan updated:** `docs/design/eden-studio-plan.md` states these as decided, and section 4.10 of `docs/design/openeden-extension-system.md` agrees.
- **Not decided here:** the manifest format for extensions' editor libraries, the Editor Module API itself, and the format of the core list.
