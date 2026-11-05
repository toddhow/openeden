# Task: The game-pack tier, SDK 4.0 and the loader changes
Design doc section: `docs/design/game-packs.md` sections 4 to 7 and stage d of section 10; decisions 0003, 0005, 0013.
Goal: a pack with manifest `kind: "game"` can be discovered, validated, resolved and started, and is handed the privileged services extensions do not get; with no game pack the core is exactly as before.
Inherited code touched, at commit `671c7676e9`: `sdk/include/openeden/` (`version.hpp` `kSdkVersion` {3,0}, `host.hpp` `Host` and its `get*Service()` members, `phases.hpp`, `extension.hpp`, `registrar.hpp`, `contracts/game_contract.hpp` `kVersion` {1,0}), `components/extension/` (manifest parser and canonical writer, `LifecycleManager`, discovery, the handshake, `HostImpl`), `apps/openmw/extensionhost.*` and `extensionworld.*` (`OMW::ExtensionHost`, frame phases from `Engine::frame` and `Game::run`), `CI/check_extension_sdk.sh`, `extensions/compat_morrowind/extension.json` and `CMakeLists.txt` (`openeden_add_extension()`), `extensions/example`, `apps/components_tests/extension/`.
Inputs / outputs: manifest format `openeden.extension` v2 (adds `kind` values `feature`, `compatibility`, `game`; v1 manifests are read as before with `kind` unchanged); SDK headers `contracts/records_contract.hpp`, `content_contract.hpp`, `scripts_contract.hpp`, `assets_contract.hpp`, `game_rules.hpp` and `save_chunks.hpp` (drafts from tasks 0083 and the design doc 5.x, finalised here); `Host::getSaveChunkService()`, `getRecordService()`, `getContentService()`, `getScriptHostService()`, `getUiService()`, returning `nullptr` to every kind except `game`; new phases `GameSimulation` and `GameUpdateUi` (game packs only, run before the same-named extension phases, fixed order); `openeden_add_game_pack()` CMake helper (static or dynamic); a test game pack in `apps/openmw_tests/` / `apps/components_tests/extension/`.
Status: draft for toddhow's approval. **Full loop** (extension loader, registry, SDK and contracts).

## Design
- **Capabilities by kind.** The loader sets a capability mask from the manifest kind before `register`; `Host` returns the privileged services only if the mask allows. A feature extension that asks gets `nullptr` and may refuse to start. Tested both ways.
- **One game pack per session.** The loader refuses a second `kind: "game"` pack (clear error naming both); the game host takes `game=<id>` from `openmw.cfg` (profile line) and refuses a pack whose id differs.
- **SDK 4.0** (`kSdkVersion` {4,0}). The reasons are in the design doc, section 6; 3.x extensions are refused with "built for SDK 3.0; this host provides 4.0" (the pattern of decision 0006), the extensions in this repository are rebuilt, and `CI/check_extension_sdk.sh` is updated to know the new rule and to forbid a feature extension including a game-only header.
- **Static first.** `game_morrowind` (task 0085) is linked into the game through the same `Registrar` as a loaded pack; the test pack proves the dynamic route (an ordinary shared library on Windows and Linux, ABI phase A).
- **Save chunk service.** Named chunks keyed `{packId, chunkName, version}`; writes and reads on the main thread only (the existing `SaveGame`/`LoadGame` worker discipline in `apps/openmw/CLAUDE.md` applies: the pack's callbacks are invoked from the thread that serialises; the contract states it); unknown chunk of a missing pack is preserved on re-save (the property decision 0005 removed for extensions returns here for game packs). The Morrowind byte format is not touched by this task.
- **Contracts carry no third-party types and no implementation**; the record store and content loader sketches in the design doc are fixed here as signatures with tests against fakes.

## Constraints
- The core starts, runs N steps and shuts down with zero extensions **and** zero game packs (the empty-host test exists for extensions; it is extended).
- Determinism: pack load order by dependency, then manifest id; the determinism gate runs with packs discovered in a different order.
- Allocation: game-phase callbacks and contract calls per step allocate nothing (the allocation gate extended).
- No exception crosses the pack boundary; a faulting game pack is a session error (design doc 7).
- Off by default: a game host with no `game=` line behaves as today (Morrowind's code is still compiled in until task 0085 moves it, so "no game pack" is the existing path).

## Acceptance criteria
- [ ] Manifest v2 parsing, validation with located errors and canonical round trip (byte for byte); v1 manifests unchanged
- [ ] Capability tests: a `feature` extension asking for each privileged service gets `nullptr`; a `game` pack gets them; a `compatibility` pack gets what extensions get; the second game pack is refused
- [ ] Lifecycle tests: register, resolve, start, stop in the documented order with a game pack; the game phases run before the extension phases of the same name; a failing game pack ends the session with its id in the message and refuses saving
- [ ] Save chunk round trip with the test pack: write, read, unknown-pack chunk preserved on re-save; a save naming another game is refused cleanly
- [ ] Empty host with zero extensions and zero game packs: identical to before (the existing test, plus a frame-count comparison); `extension` gates (determinism, allocation, boundaries) pass
- [ ] `CI/check_extension_sdk.sh` and `CI/check_layer_rules.sh` pass; every in-tree extension rebuilt against SDK 4.0
- [ ] All unit tests and the 32 Lua tests pass; Morrowind plays the same (the three extension game checks under `tools/extension_*_check/` pass with `compat_morrowind` enabled)
- [ ] Linux `-Werror`, sanitizers, and the pitfall "a contract in an anonymous namespace" avoided (named namespaces; the Linux build proves it)
Out of scope: moving any Morrowind code; GUI windows and Lua descriptor services (stubs with the contract only, finalised in task 0085); a stable C ABI (P6); dynamic loading of `game_morrowind`.

## Open questions (recommendation first)
1. SDK 4.0 or 3.1? Recommend 4.0 (design doc section 6).
2. Is `compat_<game>` required to exist next to `game_<game>`? Recommend no: a game pack declares `requires` on the compatibility pack's contracts if it needs them.
3. Are game phases separate from the existing phases, or do game packs simply register in them with higher priority? Recommend separate, so the order is explicit and testable.
