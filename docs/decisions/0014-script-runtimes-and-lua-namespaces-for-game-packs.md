# 0014 — Script runtimes (Papyrus) and Lua namespaces for game packs

Date: 2026-11-06 · Proposed by: Claude (at toddhow's question) · **Status: SIGNED (decided) by toddhow on 2026-11-06**, together with the ObScript amendment below. The ObScript first slice is task 0092 (approved the same day). The Lua descriptor mechanism (point 4) and Papyrus are not started.

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

## Amendment draft 2026-11-06: ObScript

**Signed 2026-11-06 (toddhow) with the numbered decision text above. Answers recorded 2026-11-06 (toddhow), below.**

**Answers recorded 2026-11-06.** (1) **Both runtimes live in the common compatibility pack as contracts; decision 0014 stands.** `docs/design/game-packs.md` section 5.6 (and the table row, and its open question 6) are updated accordingly, dated and pointing here. The Morrowind interpreter stays in the core until the Morrowind game pack takes it (task 0085); the seam work of task 0092 moves toward the common pack. (2) **ObScript first, Papyrus after.** (3) **Extender policy:** tiered built-ins (platform set first, libraries later as optional extensions); an unknown function logs once and does nothing in tolerant mode; a strict mode exists for tests. Open questions 4 to 6 of this amendment are answered by these. **Signed and answered later the same day (toddhow, 2026-11-06):** decision 0014 is signed; no extra rights for the common pack; game packs supply the functions and write script state; Lua namespaces as descriptors for game packs only. Question 7 (the function table's source): clean-room, derive command names and argument shapes from the user's own installed data, then supplement by hand with cited sources; nothing is copied from xNVSE, FOSE or xOBSE source or tables; no game executable is read; unnamed commands print as `fn_0x1xxx`; reading engine executables at run time stays rejected.

**Request.** toddhow, 2026-11-06: "Can you get started on the Papyrus/ObScript VM; we will need ObScript support for Fallout 3 / New Vegas." ObScript is the Morrowind-style procedural script language of Oblivion, Fallout 3 and Fallout New Vegas (records `SCPT`, with source text `SCTX` and compiled `SCDA`, plus fragments in `INFO`, `QUST`, `PACK`, `TERM`, `REFR`, `PERK`). Research: `docs/research/0046-script-runtimes-papyrus-and-obscript.md`.

**Proposed placement.**
1. **Both runtimes are shared runtimes in the common compatibility pack**, as contracts of one family, provisionally `openeden.script.papyrus` and `openeden.script.obscript`, with shared pieces (value type, located error report, per-frame budget, versioned save blob). Points 2 to 5 above apply to both unchanged: no new rights for the common pack, the game pack supplies the natives or the function table, the world and the globals, and the game pack writes the save chunk by asking each runtime to serialise.
2. **ObScript reuses OpenMW's compiler and interpreter where they are game-neutral** (`components/compiler`: scanner, parsers, generator; `components/interpreter`: `Runtime`, `Program`, segment dispatch and the control, math and local-variable opcodes), through a seam (parser options and a command table that the caller fills). The Morrowind command set and the Morrowind-shaped parts of `Interpreter::Context` stay Morrowind's and move with the Morrowind pack later. Morrowind must compile and run exactly as before. **Recompiling from `SCTX` source is the primary route** (every one of the 3,715 `SCPT` records of the installed New Vegas masters carries source); decoding `SCDA` is a fallback and a cross-check.
3. **Both runtimes are designed together and built in sequence**, ObScript first (its source and loader exist and it serves the two games named), Papyrus after, sharing the contract family from the start.

**This conflicts with `docs/design/game-packs.md` section 5.6**, which keeps the interpreter in the core, moves the compiler and command set into `game_morrowind`, and leaves Papyrus out of scope. If this amendment is signed, 5.6 is rewritten in the same change (recorded as a question below, not decided here).

**Open questions added.**
4. Both runtimes in the common pack (recommended), or the interpreter stays in the core as 5.6 says?
5. Order: ObScript first (recommended) or Papyrus first?
6. Extender policy for SKSE/F4SE/NVSE/FOSE-added functions: the measured share in the installed Skyrim mods is 17.9 percent of native call sites (6.2 percent in Fallout 4; New Vegas unmeasured). Recommended: tiered built-ins, platform set first, libraries as optional extensions, tolerant no-op for the rest (research note 0046 section 6).
7. The ObScript function table has no source in the plugin data; where it comes from (note 0046 open question 1) decides the licence position.

Follow-up 1 of "What this decision does not do" (the Skyrim read-only survey) is done in note 0046 for Skyrim SE, Fallout 4 and Fallout New Vegas; follow-up 3 becomes task 0092 (ObScript first slice, draft).

## Amendment, 2026-11-11 (toddhow): sources for what Skyrim functions do

toddhow: "You're allowed to use xEdit, the Creation Kit wiki or CommonLibSSE to figure out what those functions do" (the Skyrim condition functions, task 0122). This relaxes the clean-room rule above for the meanings of Skyrim's condition and native functions: these three may be read and cited, one citation per fact. Code or tables are copied only with their licence notice, after the licence file has been read (xEdit is recalled as MPL-2.0 and CommonLibSSE as MIT; both GPL-compatible, to be confirmed from the files). From the Creation Kit wiki, whose content licence is not known here, only facts are taken, in our own words. Unchanged: no Havok SDK or SDK-derived material, and the Havok decoder's own source rules (task 0119).
