# Game packs and compatibility packs: moving each game's rules out of the core

**Status:** decided; toddhow confirmed the terms of decision 0013 on 2026-11-06. The direction and the second game
were decided on 2026-11-04. **Date:** 2026-11-04. **Background:** `docs/research/0043-separating-formats-from-game-rules.md` (read
it first: it holds the measurements this document relies on). **Companions:** `docs/design/openeden-extension-system.md`
(the extension system), `docs/design/working-extension-system.md`, decisions 0001, 0003, 0005 and 0006.

## 1. What toddhow decided, and what is assumed

Decided (toddhow, 2026-11-04):
- Morrowind-specific code moves into its own pack, in preparation for more games.
- There is a **common compatibility pack** holding the ESM/ESP and art file loaders, and for NIFs it loads any
  version, like NifSkope.
- The structure is **game packs plus compatibility packs**.
- **The second game is Skyrim SE.** His installed Skyrim set (CBBE 3BBB, XPMSSE, HIMBO, The New Gentleman) and
  research note 0036 (the skeletons and bodies measured with the engine's NIF reader) are the test material.
  Skyrim's seamless body ties into AdvancedBodies (`docs/design/advancedbodies-in-openeden.md`).
  Oblivion, Fallout 3 and New Vegas (which `components/esm4` partly covers) are a later, cheaper step after Skyrim SE,
  not an alternative to it.

**Assumed, and flagged for confirmation** (decision 0013 lists them as questions):

| Term | Meaning in this document | Aliases (not used further) |
| --- | --- | --- |
| **Core** | Unchanged: groundwork (section 2 of the extension doc) | |
| **Common compatibility pack** | The shared **format layer**: ESM/ESP, archives, art and NIF of any version, in a game-neutral form. No game rules. | format layer |
| **Compatibility pack** (per game) | Adapts **one game's data conventions** to the core: units, skeleton and bone-name conventions, animation (`.kf`, later `.hkx`), asset path rules, `openeden.game`. Stays an **extension** under decisions 0003 and 0005. `compat_morrowind` is the first. | data adapter |
| **Game pack** (per game) | Carries **one game's rules**: record types, mechanics, script language, dialogue, UI windows, save state. A **new tier** with privileges extensions do not have (section 4). | game module (same thing), ruleset |

So a game normally has two packs: `compat_<game>` (an extension) and `game_<game>` (the privileged tier). The reason for
two and not one: the split follows what each needs from the core. Everything in `compat_*` fits today's extension
rules; only `game_*` needs saves, record types and per-frame mechanics. The alternative, one pack per game that is
a game pack (so `compat_morrowind` becomes `game_morrowind`), is simpler to name and was the reading of the first
analysis; if toddhow prefers it, sections 3 and 4 do not change and only the pack list in section 8 shortens. The single
name proposed for the tier is **game pack**; the manifest `kind` is `"game"`.

## 2. The tiers and the dependency rules

```text
hosts (openeden, Eden Studio, tools, test host) ──► core
                    │
                    ├─► common compatibility pack (formats)      no rules, no renderer, no Lua
                    │        ▲
game pack  (kind "game") ─────┤  uses formats, SDK, core services (section 4)
compatibility pack (extension)┤  uses formats, SDK
feature extensions ───────────┘  SDK + contracts only (unchanged)

core ──► no pack, ever (unchanged)
```

Rules (an extension of the diagram in the root `CLAUDE.md`, "Architecture rules and module boundaries"):

1. **The core depends on no game pack.** It starts, runs a world and shuts down with no game pack (an empty scene),
   as it does with no extension (extension doc 3.1 rule 1). With no game pack the game host refuses to start a game
   and says which pack is missing; Eden Studio and the tools start anyway.
2. **The common compatibility pack depends on nothing above it**: not on `apps/`, the renderer, Lua, the world model,
   `MWBase`, or any game's records. It may depend on `misc`, `files`, `debug`, `toutf8`, `vfs` interfaces and the math
   types. Its public types carry no game rule and no third-party renderer type.
3. **A game pack depends on the common pack and the SDK**; never on another game pack; never on a feature
   extension's headers. Game-specific record structs live in the game pack (Morrowind's `ESM::NPC` and the like) or
   in the common pack if they are a file layout and carry no rule (the file layout of a TES3 `NPC_` record is a format; what
   an NPC may do is a rule).
4. **Feature extensions ask `openeden.game`** for anything game-specific (unchanged). They never include a game
   pack's headers.
5. **The core and feature extensions never name a game.** Where code in the core knows Morrowind today (research note
   0043 section 3) it is a defect to remove in stage e, not a licence to add more.
6. **One game pack per session** (extension doc open question 5, answered by this document's recommendation): chosen
   by a profile line (`game=<id>` in `openmw.cfg`, or the launcher's choice), recorded in the save header. A save of
   another game is refused with a clear message. Two game packs at once is a load error.

## 3. The common compatibility pack (formats)

What it holds, in the order the stages deliver it (tasks 0081 to 0083):

- **NIF of any version**: the existing reader (`components/nif`), made version-explicit; a `Nif::Scene` structure
  (flat arrays, plain structs) built from a parsed file; no rendering. Priority versions: Morrowind 4.0.0.2 and
  Skyrim SE 20.2.0.7 user version 12 stream 100 first, then Skyrim LE, Oblivion, Fallout 3/NV, Fallout 4.
- **Archives**: `components/bsa` (TES3 BSA, TES4 BSA with LZ4, BA2) behind a VFS archive-factory registration
  (`VFS::Manager::addArchiveFactory` exists, task 0050).
- **Plugins (ESM/ESP/ESL)**: a `Content::Plugin` reader that yields the header (game, masters, flags), and records as
  (type, id, flags, bytes) without decoding them; typed decoders are registered per game. `ESM::RefId` already carries
  both Morrowind string ids and form ids (`components/esm/refid.hpp`), so the identity type is shared today.
- **Art and sound formats** the core already reads (DDS, TGA, COLLADA through assimp) stay where they are; they are
  not game specific.

It is a set of in-tree libraries with a small contract header (`openeden.formats`, stage c), linked into the game, Eden
Studio and the tools. It is **not** a dynamically loaded extension at first (section 9, question 3). Tools and the
editor use it directly, so a format never needs a running game.

## 4. What a game pack may do that an extension may not

Decisions 0003 and 0005 stay in force **for extensions**. A pack of `kind: "game"` gets these capabilities, each
through a named core service; the loader hands a service out only to the kinds allowed to have it
(`Host::getXService()` returns `nullptr` otherwise, so a feature extension cannot use them):

| Capability | Extension | Game pack | How it works |
| --- | --- | --- | --- |
| Frame phases | the six per-frame phases | the same, plus the game phases below | the lifecycle manager orders them |
| Mechanics hooks | no | yes | phases `GameSimulation` (mechanics and scripts) and `GameUpdateUi`, driven from `Game::run` (`apps/openmw/game.hpp`) where the mechanics manager is updated today (exact call: verify in stage d) |
| Record types | no | yes | the **record store contract** (section 5.1): the pack registers record types and the core stores them |
| Content loading | no | yes | the pack registers decoders for plugin records (section 5.2) |
| Save data | no (decision 0005) | yes | a **save chunk handler**: named, versioned chunks written to and read from the save by the main thread; unchanged bytes for Morrowind (section 7) |
| Script language | no | yes | a **script host contract** (changed 2026-11-06, decision 0014): the script runtimes (ObScript, Papyrus) live in the common pack behind `openeden.script.*`; the pack supplies commands or natives; the Morrowind interpreter stays in the core until the Morrowind game pack takes it (task 0085) |
| GUI windows | no | yes | through the core's UI service: a window description and callbacks; the toolkit (MyGUI) stays in the core |
| Lua API surface | no (decision 0003) | **record descriptors only** | the pack registers record-type *descriptors* (field names, types, accessors); the core builds the Lua tables from them. A pack never receives `sol` types. |
| Physics | read-only | read-only | **unchanged and deliberate**: a game pack does not own Jolt. Anything the rules need (actor shapes, collision filters) is asked of the core's actor model. |
| Rendering | none | none | a pack hands the core `Scene` data or asks it to attach models; it never renders |

What does not change for game packs: the determinism rule (no wall clock, no unordered iteration where order affects
results, ordered phases, `dt` and step index from the core), no allocation in per-frame code, no exception across the
boundary (errors are values), off-by-default for anything that changes Morrowind's behaviour, GPL-3.0-or-later, no
game assets in the repository.

**Why this does not break the reasons behind decisions 0003 and 0005.** Those decisions protect saves from being
affected by optional add-ons, and the core's Lua and physics from third-party code. A game pack is not optional in
that sense: it *is* the game, exactly one is active, and its save chunks are the game's save data (they are part of the
file the way `ESM::SavedGame` is today). The restrictions on physics and on the Lua tables stay because the reason there
(one Jolt world, one sandboxed Lua host) still holds.

## 5. The contracts a game pack needs

Contracts are headers only, with no implementation and no third-party types (root guide). Each is versioned; a pack
that needs one lists it in its manifest. They are sketched; stage specs fix the signatures.

### 5.1 Record store (`openeden.records`)

Today `MWWorld::ESMStore` (`apps/openmw/mwworld/esmstore.hpp`) is a `std::tuple` of `Store<ESM::X>` for forty Morrowind
types, and `LiveCellRef<T>` and `Ptr` are templated on them. The contract: a record type has a stable id (`RecordTypeId`,
a FourCC like `NPC_`), a size, and a registration `registerRecordType(TypeInfo)`; the core owns the storage
(`Store` of type-erased records), looks records up by `RefId`, and hands out typed views through
`record_cast<T>(handle)` that the pack implements. The first implementation **keeps Morrowind's tuple inside
`game_morrowind`** and exposes it through the contract; the core's generic code uses the type-erased view. Stage c
builds the abstraction; stage e moves the tuple.

### 5.2 Content loader (`openeden.content`)

`ContentLoader::probe(plugin header) -> accepted?`; `load(Content::Plugin&, Progress&)` called in load order; a decoder
registry from record type id to decoder. Replaces `MWWorld::EsmLoader` (`apps/openmw/mwworld/esmloader.cpp`), which
today dispatches on the TES3/TES4 header. The launcher's content list (`components/contentselector`) uses `probe`.

### 5.3 VFS archive registration

Packs register archive kinds (BSA versions, BA2) by file extension and magic with the existing archive-factory list
(`VFS::Manager`); the core's `registerArchives` stops naming BSA. Already partly built (task 0050). **Built 2026-11-08 (task 0083 stage 3):** `components/archivekinds/` holds the three Bethesda kinds as factories (`tes3ArchiveFactory`, `tes4ArchiveFactory`, `ba2ArchiveFactory`, by magic bytes) and `registerBethesdaArchiveKinds()`, which each program calls at start-up; `VFS::makeArchive` knows only the added factories and the Eden pack; the four `vfs -> bsa` layer exceptions are gone (206 to 202). The directory is in the `host` layer because only a layer that may include both `vfs` (engine) and `bsa` (formats) can hold it; moving it into the common compatibility pack is task 0084's call.

### 5.4 Model and animation loader (`openeden.assets`)

Given a VFS path and options, return a `Nif::Scene` (the common pack) or an animation clip description; a game pack or
compat pack supplies the *conventions* (which node is the root, which bones, units, texture path rules), the common
pack the parsing. Replaces the Morrowind rules inside the reader (`components/nif/node.cpp`, the `bip01` rule).
`KfConverterContract` is the example of the pattern.

### 5.5 Mechanics hooks

An interface `GameRules` with the calls the core makes each frame and on events: `update(dt)`, `onCellChanged`,
`onActorDeath`, `onActivate(entity, actor)`, `isHostile`... Derived from `MWBase::MechanicsManager`
(`apps/openmw/mwbase/mechanicsmanager.hpp`) and `MWBase::World` by **removal of what is Morrowind-only**, not by
copying; the first version is exactly what Skyrim SE would also need, found by writing both packs' skeletons (stage f).

### 5.6 Script host (`openeden.scripts`)

`compile(source, context) -> Program or located errors`, `run(program, target)`, `registerCommand(...)`, plus the
variable storage the save chunk covers.

**Changed 2026-11-06 (decision 0014, toddhow's answers):** both script runtimes live in the **common compatibility
pack** as contracts of one family, `openeden.script.*`: ObScript (Oblivion, Fallout 3, New Vegas; first) and Papyrus
(Skyrim, Fallout 4, Starfield; after). The runtimes hold no authority; a game pack supplies the function table or
natives and world access and writes the save chunk. ObScript reuses the game-neutral parts of `components/compiler`
and `components/interpreter` through a seam (task 0092). **Until the Morrowind game pack takes it (task 0085) the
Morrowind interpreter, compiler and mwscript command set stay where they are, in the core and `apps/openmw`.** The
earlier text of this section (interpreter in the core, compiler and commands to `game_morrowind`, Papyrus out of
scope) is superseded; see research note 0046 and task 0092 for the contract and the first slice.

### 5.7 Save chunk handler

`SaveChunkHandler` with `write(ChunkWriter&)` and `read(ChunkReader&, version)`; chunks keyed by the pack's id and a
name; ordering by registration order. Resurrects the interface removed by decision 0005, for `kind: "game"` only.

### 5.8 GUI windows and Lua descriptors

A window registry (`registerWindow(id, description)`) and the record descriptors of section 4. Both are late; stage
e starts with the pieces that have fewer callers and leaves `mwgui` (41k lines) last.

## 6. SDK version impact

**Status (2026-11-06, task 0084):** SDK 4.0 is built. `kind` is checked in manifest format 2; the game services are handed out by kind through a per-extension `Host` (`PackScopedHost`); phases `GameSimulation` and `GameUpdateUi` exist; one game pack per session by `game=`; the save header records the pack (`GPCK`, `GPVR`). The earlier sketch of `game_rules.hpp` and the Lua descriptor service are not built; they belong to task 0085.

The SDK is 3.0 (`openeden::sdk::kSdkVersion`, `sdk/include/openeden/version.hpp`). Under the extension doc's own
rules adding services and contracts is a **minor** change. This document recommends **4.0** anyway, because three
things change that a 3.x extension must not silently meet: the manifest gains `kind: "game"` (manifest format
version 2), `Host` hands out some services by kind (a changed contract about who may call what), and the
phase list grows. A 3.x extension still loads on a 4.0 host only if the host chooses to support it (recommend: no,
since nothing outside this tree was built; same reasoning as decision 0006). `CI/check_extension_sdk.sh` is updated
to know the new rule. Stage d lists exactly what changes.

## 7. Determinism, threading, saves, failures

- **Determinism:** game packs load in dependency order, then manifest order; their phases run after the core's
  corresponding step and before extensions' phases of the same name (recommended; fixed by stage d). The determinism
  gate (a scenario run twice, packs discovered in a different order) covers them.
- **Threading:** a game pack's hooks run on the main simulation thread except where the pack declares a parallel
  callback; the Jolt async worker threads (`apps/openmw/mwphysics/mtphysics.cpp`) never call into a pack. Each
  contract states its thread.
- **Saves:** the `.omwsave` keeps its record-stream form. The Morrowind state records (`components/esm3`'s `*state`,
  `player`, `savedgame` and the managers' writers) become the chunks of `game_morrowind`, byte for byte. The check is
  a round trip: load an existing save, save it, compare the files (and load the result). A save header records the
  game pack id and version; an unknown pack or a newer version is refused with a message and never partially loaded.
- **Failure behaviour:** a missing or failing game pack: the game host shows one message at the main menu (as
  extension failures do, `apps/openmw/CLAUDE.md`), starts no game, and leaves Eden Studio and tools working. A game pack
  that fails at run time is a fatal error for the session (it is the game), reported with its id and a log; it never
  corrupts a save (saving after a pack fault is refused). A failing compatibility pack follows the extension rules (its
  dependants are disabled).
- **Content that the pack does not know** (a record type from a different game in the load order): logged and skipped
  with the record type and the file, never a crash.

## 8. The editor boundary

Eden Studio (`apps/opencs`) reads the same formats and uses the same records: 268 `esm3` include lines in
`apps/opencs/model` (research note 0043). The rule: **the editor links the common compatibility pack directly and
loads a game pack as a host would** (it is a host of the core, extension doc 2). A game pack therefore also provides,
when the editor loads it, the record descriptors (field names and types, shown in tables and inspectors) and the
script syntax check. Until record descriptors exist the editor stays on the Morrowind structs; it is a Morrowind-only
editor until stage e's descriptors reach it, which is acceptable because Eden Studio comes after the extension
system (root guide, "Not yet"). Eden Studio can show Skyrim meshes after the NIF stage (task 0082) without any game
pack; it shows Skyrim records only after the record descriptors.

## 9. Open questions (recommendations first)

1. **Names.** Recommend: tier = **game pack** (manifest `kind: "game"`), per-game adapter = **compatibility pack**,
   shared format layer = **common compatibility pack**. Alternatives: one pack per game (merge `compat_*` into
   `game_*`); "game module" instead of "game pack".
2. **Static first.** Recommend: build `game_morrowind` as a library linked into the game executable through the
   same registrar and contracts, and load dynamic game packs only when a second game exists in a separate library
   (ABI phase A: same toolchain as the core). Reason: Morrowind's rules call the world model constantly (research
   note 0043 sections 1.2 and 3); a dynamic library boundary before the seam is stable is risk with no benefit.
3. **Common pack as libraries.** Recommend: libraries first, a loadable extension later if ever. Reason: tools and
   the editor use formats without a running game.
4. **SDK 4.0 against 3.1.** Recommend: 4.0 (section 6).
5. **Generated or hand-written NIF.** Recommend: hand-written reader plus `nif.xml` as an oracle in a dev tool,
   subject to the licence reading (research note 0043 section 5.2).
6. **Papyrus and `.hkx`.** `.hkx` is not planned in these stages. **Papyrus (changed 2026-11-06, decision 0014):** the VM is in
   the common pack, ObScript first, Papyrus after (research note 0046, task 0092); the Skyrim game pack supplies its natives.

## 10. Staged migration plan

Every stage keeps Morrowind playing the same, **verified by comparison** as the root guide requires: the existing
unit tests, the 32 Lua integration tests, and a named in-game comparison per stage. Full loop for 0084 to 0086
(extension loader and SDK, saves, and the changes under `apps/openmw`); the first two stages may be marked lighter
by toddhow (CI and tools) except where they touch the NIF reader (full loop: it handles untrusted input).

| Stage | Task | Deliverable | Moves files? | Verification |
| --- | --- | --- | --- | --- |
| a | 0081 | The dependency map made executable: a rule file and `CI/check_layer_rules.sh`; optional CMake object libraries per layer | No | The check passes on today's tree (with a recorded exceptions list that only shrinks); builds identical |
| b | 0082 | NIF of any version: version-explicit reader, `Nif::Scene`, tests on generated and user-supplied files | Small | Existing nif tests, a byte-identical converter result for Morrowind meshes, the 43 files of note 0036 read |
| c | 0083 | Record-source abstraction and the common ESM/ESP loaders | Some | `esmtool` output unchanged on Morrowind data; `ESMStore` loads through the abstraction; load time within noise (**stages 1 and 2 built 2026-11-06 on Windows: `components/contentsource/` layer 1, the decoder registry, `ESMStore` and `EsmLoader` through it, `esmtool` byte-identical, load time within noise; stage 3 built 2026-11-08: the Bethesda archive kinds are VFS factories in `components/archivekinds/`, the `vfs -> bsa` exceptions are gone (206 to 202), Morrowind VFS counts identical; stages 4 and 5 wait for toddhow, stage log in the task**) |
| d | 0084 | The game-pack tier: SDK 4.0, contracts, loader changes, a trivial test game pack (**built 2026-11-06 on Windows; Linux gate, Lua tests and a fresh review pending**, stage log in the task) | No | Zero game packs changes nothing; a test pack proves each capability; extension gates still pass |
| e | 0085 | Moving Morrowind's rules into `game_morrowind`, three candidate moves first | Yes | Per move: unit and Lua tests, a recorded scenario compare, save round trip |
| f | 0086 | Skyrim SE game pack skeleton | New | Loads Skyrim SE's plugin headers and meshes in Eden Studio and the game host's content step, runs nothing of its rules |

Order notes: a and b can start at once; c needs a; d needs a and c's contracts to be written, not finished; e needs
all of a to d; f needs b to d. The first value is stage b.

## 11. Risks that stay (from research note 0043 section 3)

`ESMStore` and `Ptr` are the hardest seam (163 and 104 includers). `mwclass` mixes rule and engine behaviour. Saves
must stay byte-compatible. The editor's dependence on Morrowind structs. A Morrowind rule sits inside the NIF reader
(`components/nif/node.cpp`). None of these block stages a to d; they decide how many moves in stage e are cheap.
