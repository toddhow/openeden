# 0013 — Game packs and compatibility packs: Morrowind's rules move out of the core

Date: 2026-11-04 · Decided by: toddhow (direction and second game) · **Status: proposed, awaiting toddhow's confirmation of the terms below** (the direction and Skyrim SE as the second game are decided)

Design: `docs/design/game-packs.md`. Background: `docs/research/0043-separating-formats-from-game-rules.md`. Tasks 0081 to 0086.

## Decision (as toddhow gave it)
- "Moving morrowind specific code into its own compatibility pack in preparation for future games. I'm thinking we'd have a common compatibility pack as well, this would have our esm esp and art file loaders in it. For nifs it would just be able to load any version similar to nifskope."
- After the analysis: "We will go with the game packs + compatibility packs."
- Second game: **Skyrim SE** (toddhow, 2026-11-04). His installed set (CBBE 3BBB, XPMSSE, HIMBO, The New Gentleman) and research note 0036 are the test material. Skyrim's seamless body ties into AdvancedBodies. Oblivion and Fallout 3/NV (partly covered by `components/esm4`) remain a later, cheaper step, not the second game.

## Assumptions to confirm (the terms)
1. **Three kinds of pack, and the core.** The **common compatibility pack** is the shared format layer (ESM/ESP, archives, art, NIF of any version), game-neutral, no rules. A **compatibility pack** adapts one game's data conventions (an extension; `compat_morrowind` is the first). A **game pack** (manifest `kind: "game"`) carries one game's rules; it is a new tier. "Game module" is an alias of game pack.
2. **A game normally has two packs:** `compat_<game>` and `game_<game>`. The alternative (one pack per game, `compat_morrowind` becoming the game pack) is accepted by the design with a shorter pack list.
3. **The game pack is compiled in first** (same registrar and contracts as a loaded pack), dynamic loading later. **The common pack is a set of libraries first.**
4. **One game pack per session**, chosen by profile and recorded in the save header.
5. **SDK 4.0** (kind in the manifest, services handed out by kind).
6. The NIF reader is hand-written and extended, with `nif.xml` as an oracle only if its licence allows reading it (verify).

## Consequences for earlier decisions
- **Decision 0003 (no physics changes, no Lua API for extensions) stands for extensions.** It also stands for game packs on physics (read-only, no Jolt ownership). For Lua, a game pack registers record **descriptors** and the core builds the tables; it never receives Lua types, so the core's single Lua host and sandbox are kept.
- **Decision 0005 (extensions do not affect save data) stands for extensions.** A game pack writes save chunks through a handler (the interface 0005 removed returns for `kind: "game"` only), because its state is the game's state, not an optional add-on's. The `.omwsave` bytes for Morrowind do not change; the check is a load, re-save and compare.
- **Decision 0001** is unchanged: CreativeMotion stays in the core. Decision 0006's naming holds ("extension" for the native system, "plugin" for ESP/ESM): "pack" is a new word for a loadable bundle and does not replace either.
- **Extension doc 3.1 rule 5 and the root guide's "Game rules live in the compatibility pack"** gain the game pack tier; open question 5 of the extension doc (one game per session) is answered as above.
- **The core keeps no feature or game rule**; the work of stage e is to remove the places where it still has one (research note 0043 section 3).

## Alternatives rejected
- **Relax the extension rules for all extensions** (allow saves, Lua tables and mechanics hooks for every extension). Rejected: it removes the guarantees of 0003 and 0005 from every feature extension to serve one tier, and makes "installing or removing an extension does nothing to saves" untrue.
- **Keep everything in the core** (add Skyrim next to Morrowind in `apps/openmw`). Rejected: it repeats the monolith the extension system was made to avoid (extension doc 1.1), makes each game edit the core, and gives no way to check that formats do not depend on rules.
- **A game pack as a dynamic library from the start.** Deferred, not rejected: the seam between the rules and the world model (`Ptr`, `ESMStore`, `MWBase`) is not stable yet.

## What this decision does not do
It moves no files and changes no behaviour. Stage 0081 only adds a checker. Stages are separate task specs, each approved by toddhow before work starts.
