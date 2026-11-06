# 0014 — Script runtimes (Papyrus) and Lua namespaces for game packs

Date: 2026-11-06 · Proposed by: Claude (at toddhow's question) · **Status: DRAFT, awaiting toddhow's signature.** Nothing is built and no file moves.

Builds on decision 0013 (game packs), and refines its Lua row. Background: toddhow asked whether future Bethesda games' Papyrus scripts should be loaded and made to call the Lua API, and whether extensions should be allowed to add to the Lua API so the Papyrus VM can live in the common pack ("so each game doesn't ship the VM").

## Decision (proposed)
1. **A Papyrus VM is a shared runtime in the common compatibility pack.** It loads compiled `.pex` scripts directly from the player's installed game data (never shipped, never committed) and runs them itself. It is **not** a `.pex` to Lua translator: Papyrus's latent calls, script states, event order and the saving of scripts mid-call are semantics a translation would have to rebuild on top of coroutines anyway, and the save format would need inventing. The VM is game-neutral across the Creation Engine games (Skyrim, Fallout 4, Starfield), with format differences handled by version tables, one copy of the code.
2. **The common pack gets no new permissions.** It stays an ordinary extension under decisions 0003 and 0005: no Lua tables, no save chunks, no physics changes. The VM offers a **contract** (provisionally `openeden.script.papyrus`) and holds no authority over the world or the save: everything that touches either goes through the game pack that loaded it.
3. **The game pack is the privileged tier, and supplies the natives.** It registers the native functions (`Game`, `Actor`, `ObjectReference`, `Debug`, `Utility` and the rest) with the VM over its own world model, and it **writes the save chunk**: it asks the VM through the contract to serialise script state (variables, states, suspended stacks) and stores it as part of the game's save data (decision 0013: a game pack's state is the game's state).
4. **Lua namespaces for the game-pack tier, as descriptors.** Decision 0003's rule (no Lua API from extensions) **stands for ordinary extensions and for the common pack**. For `kind: "game"` packs it is relaxed in the way decision 0013 already began for records: a pack registers **function descriptors** (namespace, function names, argument and result types, which thread may call it, version), and the core builds the Lua bindings from them in its single sandboxed Lua host. A pack never receives `sol` types or a Lua state. Namespaces are owned by the core, named `core.<game>.*` (for example `core.skyrim.*`), and versioned.
5. **A Lua script that needs a pack's namespace says so in its manifest** and is refused at load with a clear message when that game pack is not the session's game pack, instead of failing in the middle of play. A saved script keeps working because its game pack is always the session's (decision 0013, one game pack per session, recorded in the save header).

## Why this shape
- **Fits what is decided:** 0013 already lets a pack register record descriptors for Lua; this extends the same mechanism to functions, so the one-sandboxed-Lua-host reason of decision 0003 still holds.
- **Keeps the privilege model small:** two tiers only (extension, game pack). The common pack stays unprivileged, so "formats and runtimes share one pack" does not create a third tier.
- **The VM works with no Lua at all:** the Papyrus path needs neither point 4 nor the save relaxation beyond what 0013 grants a game pack, so the VM can be built and tested before any Lua namespace exists.
- **Saves stay deterministic and owned:** only the game pack writes them (as in 0013); the VM serialises on request.

## Alternatives considered
- **Translate `.pex` to Lua at load time.** Rejected as the main path: it rebuilds Papyrus's semantics in Lua and has no natural save format. A translation could be a later performance backend behind the same contract, not a replacement for it.
- **One VM per game pack.** Rejected: duplicates a large runtime and its tests in every game.
- **Allow every extension to register Lua tables.** Rejected for the reasons in decision 0013: it removes the 0003 and 0005 guarantees from every feature extension, and a save would depend on which extensions were installed.
- **A manifest capability `lua-namespaces` that a pack could grant to an extension it hosts.** Deferred, not rejected: a second way to be trusted needs a rule for who may grant it; no case needs it yet.

## What this decision does not do
It builds nothing and changes no behaviour. The first follow-ups, each needing toddhow's approval of its own spec:
1. A research note and a read-only spike: parse the `.pex` files of the player's installed Skyrim SE, report **aggregate** counts only (scripts, natives called and how often, `.pex` format versions), to size the native surface. The local `papyrus-reference` skill carries an offline list of function signatures to compare against; nothing is invented.
2. Sizing the SKSE problem: popular mods call functions from SKSE plugin DLLs (PapyrusUtil, JContainers and similar), which cannot be loaded; each would need reimplementation in the pack, or those mods do not work. The spike counts how many scripts of the installed set call natives outside the base game's list.
3. A task spec for the contract and the VM, and one for the descriptor mechanism (point 4), after the note.

## Open questions for toddhow
1. Is `core.<game>.*` the right Lua namespace shape, or `core.<game>` registered under a pack-chosen short name?
2. Should the contract's script-state serialisation be versioned independently of the save format (recommended: yes, a version number in the chunk), so a VM fix does not invalidate saves?
3. Does the VM run on the main thread only (recommended at first: Papyrus is single-threaded per stack but many stacks interleave; main thread keeps determinism) or on a worker with a handoff?
