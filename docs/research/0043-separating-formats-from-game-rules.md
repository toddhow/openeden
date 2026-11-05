# Research note 0043: separating file formats from game rules (dependency map, other engines, NIF in any version)

2026-11-04. Docs only: nothing in the code changed. For `docs/design/game-packs.md`, decision 0013 and tasks 0081 to
0086. Method and honesty rules:

- **Measured** numbers come from `tools/include_count.sh` (a read-only shell script that
  counts `#include` lines by the directory they point at; run from the repository root) and from
  `grep` runs at commit `671c7676e9` of branch `vulkan`. Line counts are `wc -l` over `*.cpp`, `*.hpp`, `*.h`. An
  "include count" is the number of include lines, not distinct headers; a "file count" is the number of files with at
  least one such include.
- Every statement about OpenMW or OpenEden code cites a file and, where it matters, a function. Statements about
  other engines, `nif.xml`, NifSkope and licences are **recalled, verify**: no web page was fetched in this session.
  No code from any of them is taken.

## 1. What the code looks like now

### 1.1 Size of the pieces toddhow named (re-measured)

| Directory | Lines | What it is |
| --- | --- | --- |
| `components/esm3` | 17,735 | Morrowind record structs and readers/writers (`load*.cpp`), **and** the save-game records (`*state`, `savedgame`, `player`, `queststate`, `journalentry`...) |
| `components/esm` | 2,868 | Shared record infrastructure: `RefId`, `FormId`, `Position`, `FourCC`, `luascripts`, `esmbridge`, `esmterrain` |
| `components/esm4` | 20,853 | Oblivion/Fallout/Skyrim-style records (`load*.cpp`) and the `Reader` (`reader.cpp`: compressed records, `.STRINGS` files for localised strings) |
| `components/nif` | 11,887 | The NIF reader, 30 files, hand-written |
| `components/bsa` | 2,146 | TES3 BSA, TES4 BSA with LZ4 (`compressedbsafile.cpp`), BA2 general and DX10 |
| `components/compiler` | 7,360 | mwscript compiler (plus `components/interpreter`, separate) |
| `apps/openmw/mwmechanics` | 29,058 | Morrowind rules: stats, AI, combat, magic |
| `apps/openmw/mwworld` | 24,813 | World model: cells, `Ptr`, `ESMStore`, `Class`, containers, weather |
| `apps/openmw/mwclass` | 8,656 | One `MWWorld::Class` per record type |
| `apps/openmw/mwdialogue` | 3,340 | Dialogue filter and topic logic |
| `apps/openmw/mwscript` | 9,557 | mwscript command set (the interpreter's opcodes) |
| `apps/openmw/mwgui` | 41,379 | Windows (inventory, dialogue, stats, spell making...) |
| `apps/openmw/mwlua` | 15,845 | Lua bindings of the game objects and records (85 files) |
| `apps/openmw/mwrender` | 25,813 | Renderer integration (VSG) |
| `apps/openmw/mwphysics` | 6,844 | Jolt integration |
| `apps/openmw/mwbase` | 2,481 | The manager interfaces held by `MWBase::Environment` |
| `apps/opencs` | 93,012 | The editor (Eden Studio's base) |

The sum of the "Morrowind rules" rows toddhow listed (mechanics, world, class, dialogue, script, compiler) is 82,784
lines; with `mwgui` 124,163. The formats rows (esm3, esm, esm4, nif, bsa) are 55,489.

### 1.2 Who includes whom (include lines, top targets, standard library and third party left out)

**Formats are already clean.** The format directories point almost only at each other and at small utilities.

| Directory | Points at (include lines) |
| --- | --- |
| `components/esm3` | `esm` 157, `misc` 44, `esm3` 18, `debug` 8, `toutf8` 2, `files` 2 |
| `components/esm` | `esm3` 51, `esm4` 46, `misc` 10, `esm` 6, `lua` 3, `vfs` 1 |
| `components/esm4` | `esm` 176, `esm4` 81, `debug` 4, `files` 3, `vfs` 2, `misc` 2, `toutf8` 1, `bsa` 1 |
| `components/nif` | `misc` 5, `files` 4, `debug` 2, `vfs` 1, `toutf8` 1, `resource` 1, and VSG math (`vsg/maths/*`, 14) |
| `components/bsa` | `files` 12, `vfs` 3, `esm` 3, `misc` 1 |
| `components/compiler` | `interpreter` 9, `misc` 10, `esm` 2, `debug` 1 |

Two things are not clean:
- `components/esm` is meant to be the shared base, yet it includes `esm3` (51) and `esm4` (46) lines: `esmbridge.*`,
  `records.hpp`, `util.*`, `defs.hpp`, `exteriorcelllocation.hpp`, `esmterrain.*`, `luascripts.cpp`, `attr.cpp`. It
  is a base and a bridge in one directory. `esm/luascripts.*` also reaches `components/lua`.
- `components/nif` includes `components/resource` once (the `nif` records' one dependency on the resource layer) and
  `components/bsa` includes `components/esm` (3 lines).

**The rule code is wired into everything.**

| Directory | Points at (include lines) |
| --- | --- |
| `mwmechanics` | `mwworld` 151, `mwbase` 136, `esm3` 108, `misc` 61, `esm` 17, `mwrender` 14, `mwanimation` 14, `detournavigator` 11 |
| `mwworld` | `esm3` 150, `mwbase` 92, `mwmechanics` 47, `misc` 46, `esm4` 46, `esm` 34, `mwrender` 15, `detournavigator` 11, `mwphysics` 10 |
| `mwclass` | `mwworld` 152, `mwbase` 62, `esm3` 62, `mwmechanics` 43, `esm4` 36, `mwrender` 27, `mwgui` 20, `mwphysics` 7, `mwlua` 3 |
| `mwdialogue` | `mwbase` 26, `esm3` 20, `mwworld` 18, `compiler` 14, `mwmechanics` 7, `mwscript` 6 |
| `mwscript` | `mwbase` 60, `esm3` 60, `mwworld` 50, `interpreter` 50, `compiler` 27, `mwmechanics` 26 |
| `mwgui` | `mwbase` 207, `mwworld` 138, `mwmechanics` 79, `esm3` 78, `misc` 59, `settings` 47, `widgets` 27, `resource` 13 |
| `mwlua` | `lua` 100, `esm3` 98, `misc` 73, `mwbase` 71, `mwworld` 63, `apps/openmw` 53, `esm4` 21, `resource` 26 |
| `mwrender` | `vsgutil` 102, `render` 74, `mwanimation` 54, `pipeline` 51, `misc` 45, `mwworld` 42, `animation` 42, `esm3` 39 |
| `mwstate` | `mwbase` 63, `esm3` 12, `mwworld` 11 |
| `mwbase` | `mwworld` 6, `misc` 5, `mwdialogue` 3 (it is mostly forward declarations) |

**Files across `apps/openmw`** (877 `.cpp`/`.hpp` files in total): 262 include `components/esm3`, 94 include
`components/esm/`, 24 include `components/esm4`, 286 include `mwbase/`, 104 include `ptr.hpp`, 163 (in all of `apps`
and `components`) include `esmstore.hpp`. `mwlua`: 49 of its `.cpp` files name `ESM::` types. In `mwgui`, 86 files use
`MWWorld::Ptr`.

**Components that reach upward or sideways into Morrowind records** (the layering exceptions to fix first):
- `components/resource/foreachphysicsobject.cpp` includes `esm3/cellref`, `esm3/esmreader`, `esm3/loadcell`,
  `esm3/readerscache` and three `esmloader` headers: the resource layer reads cell references.
- `components/mwanimation/partbones.hpp` includes `esm3/loadarmo.hpp`; `components/mwanimation/light.cpp` includes
  `esm3/loadligh.hpp`.
- `components/detournavigator/navigatorimpl.cpp` includes `esm3/loadpgrd.hpp` (path grids).
- `components/vfs` includes `components/bsa` (one file): the VFS knows one archive kind.
- `components/esmloader` (a small loader used by the navmesh tool and the resource layer) takes 18 `esm3` includes.
- `components/contentselector` (the launcher's list) includes `esm3` 2 and `esm4` 1: it reads plugin headers.
- No file in `components/` includes `apps/` (checked: zero).

**Who else uses the formats** (files including `components/esm3`): `apps/opencs/model` 67, `apps/essimporter` 35,
`apps/opencs/view` 22, `apps/openmw_tests/mwrender` 11, `apps/esmtool`, `apps/launcher` (2 include lines). The editor
alone has 268 include lines of `esm3` in its model: Eden Studio cannot be separated from the Morrowind record structs
without a plan of its own.

**NIF consumers:** `components/vsgadapters/nif` (6 files: the NIF-to-VSG converter), `components/bodynif` (4),
`components/nifjolt` (2), `components/lod/nifflatten` (2), `components/resource/niffilemanager.hpp`,
`extensions/compat_morrowind` (3), `apps/openmw/bodyanalyser.cpp`, `bodybaker.cpp`, `bodycommon.hpp`,
`lodobjectbaker.cpp`, `mwrender/rendermanager.cpp`, `apps/niftest`, `apps/edenskelstat`, `apps/opencs/view` (1). A
small, well-bounded set.

### 1.3 The build shape

`components/CMakeLists.txt` builds **one** static library, `components` (`add_library(components STATIC
${COMPONENT_FILES})`, line 722), filled by the `add_component_dir` macro, plus `components_qt`. A directory boundary is
therefore not a link boundary today: a format library cannot be linked without the renderer, Lua and the rest. Any
"CMake target per layer" step is a real build change (link order, test targets in `apps/components_tests`, the tools'
link lines) and has to be staged as one.

## 2. Classification of the code

| Class | Directories (today) | Notes |
| --- | --- | --- |
| **Formats** (portable, no game rules) | `esm` (base part), `esm3` content records, `esm4`, `nif`, `bsa`, `esmloader`, `toutf8`, `files` | `esm3`'s save-game records belong to the Morrowind *game* (section 3.5), not to the format layer |
| **Generic engine** | `vfs`, `resource`, `render`, `vsgadapters`, `vsgutil`, `pipeline`, `view`, `lod`, `terrain`, `animation`, `creativemotion`, `detournavigator`, `lua`, `settings`, `misc`, `sceneutil`; in `apps/openmw`: `mwphysics`, `mwrender`, `mwsound`, `mwinput`, `engine`, `extensionhost` | the `mw` prefix is history; `mwrender` and `mwphysics` already work with `Ptr` |
| **Morrowind rules** | `mwmechanics`, `mwworld` (most), `mwclass`, `mwdialogue`, `mwscript`, `compiler`, `interpreter` (generic VM, but built for mwscript), `mwstate`, `esm3` save records, `mwlua` (bindings of the above) | this is what a game pack takes |
| **UI** | `mwgui`, `widgets`, `myguiplatform`, `fontloader`, `l10n` | UI windows are game-specific *content* on a generic toolkit |
| **Tools** | `opencs` (Eden Studio), `launcher`, `wizard`, `esmtool`, `bsatool`, `niftest`, `essimporter`, `mwiniimporter` | use the formats; some also use rules (the editor's script checker uses `compiler`) |

## 3. The worst tangles (each makes a stage harder than it looks)

1. **`ESMStore` is a tuple of Morrowind record stores.** `apps/openmw/mwworld/esmstore.hpp` declares
   `StoreTuple = std::tuple<Store<ESM::Activator>, Store<ESM::Potion>, ...>` over about forty `components/esm3` types
   and also holds ESM4 records (46 `esm4` includes in `mwworld`). 163 files include it. `ESMStore::load` and `write`
   are keyed by Morrowind record type ids. A record-store abstraction has to sit *below* this class, and
   `MWWorld::Class` implementations get their data from it.
2. **`MWWorld::Ptr` and `LiveCellRef<T>` are templated on the record type** (`apps/openmw/mwworld/ptr.hpp`,
   `livecellref.hpp`); 104 files include `ptr.hpp` and 86 `mwgui` files use `Ptr`. The generic engine (`mwphysics`,
   `mwrender`, `mwsound`) takes `Ptr`. Moving rules out while `Ptr` stays means `Ptr` is part of the core's world model
   and the record type is behind a type-erased handle.
3. **`MWBase` is the seam OpenMW already has, but it is Morrowind-shaped.** `mwbase/world.hpp`, `mechanicsmanager.hpp`,
   `windowmanager.hpp`, `dialoguemanager.hpp`, `scriptmanager.hpp` name Morrowind concepts. 286 files include `mwbase/`.
   They are the right *places* for contracts and the wrong *signatures* to publish.
4. **`mwclass` mixes game rules with generic object behaviour.** One class per record type holds the rule (value, use,
   `onActivate`) and the engine behaviour (`insertObject` into the render and physics worlds: 27 `mwrender` and 7
   `mwphysics` includes; 20 `mwgui` includes for tooltips). A pure split is a rewrite of each class, not a move.
5. **Save games hold Morrowind records in the format layer.** `components/esm3` contains `savedgame`, `player`,
   `queststate`, `journalentry`, `globalscript`, `*state` and others; `apps/openmw/mwstate/statemanagerimp.cpp` reads
   and writes them through `ESM::ESMReader`/`ESMWriter`. Each manager (`mwworld/worldimp.cpp`, `mwdialogue`,
   `mwscript`, `mwmechanics`) writes its own records. Moving the rules means moving their save records, with the
   `.omwsave` bytes unchanged (existing saves must still load, and the compare is a re-save).
6. **mwscript is a language of the game.** `components/compiler` + `components/interpreter` are generic enough; the
   opcode set and the extensions (`apps/openmw/mwscript/*extensions`) are Morrowind. Dialogue conditions compile
   through it (`mwdialogue`: 14 `compiler` includes). A game pack that owns "the script language" must supply a
   compiler front end and the opcode table; the editor (`apps/opencs/model/world/scriptcontext`, 12 `compiler` includes
   in the model) compiles with it for syntax checks.
7. **Lua bindings are written against the typed records.** `apps/openmw/mwlua/types/*.cpp` (`activator.cpp`,
   `armor.cpp`, `npc.cpp`, ...; 30 files in the folder) convert between sol tables and `ESM::` structs (`tableToActivator` in
   `types/types.hpp`). The stack table in the root `CLAUDE.md` says Lua APIs come only from the core, so a game pack
   cannot register tables (decision 0003); it needs a record-descriptor service in the core.
8. **The editor uses the same records.** `apps/opencs/model` has 268 `esm3` include lines. The editor's table model
   per record type is generated from the same structs. Eden Studio showing Skyrim records means a record-descriptor
   layer, not just a reader.
9. **The "formats" are not entirely rule-free.** `components/nif/node.cpp` line 188 replaces record 0's transform
   by the identity unless the node is named `bip01`: a Morrowind skeleton rule inside the reader (found in research
   note 0036). The `NIFFile::VER_MW` constants and the supported-version list in `niffile.cpp` (`Reader::parse`) are
   Morrowind's. `esm` has Morrowind-only `esm3exteriorcellrefid.*`.
10. **Global switch in the NIF reader.** `Reader::sLoadUnsupportedFiles` is a static `std::atomic_bool`
    (`niffile.cpp`, `Reader::setLoadUnsupportedFiles`); the tools set it, the game does not. Version support is a
    process-wide flag, not an option of a reader.
11. **One static `components` library** (section 1.3).
12. **The launcher and wizard** read plugin headers through `contentselector` and install Morrowind from CDs; both are
    Morrowind-specific user-facing flows outside the engine. They stay as they are until a second game needs
    them.

## 4. How other engines and ports separate formats from game rules

Each entry is marked: **read** (opened in this session) or **recalled, verify**. Only the OpenMW and OpenEden items
are read.

| Source | Pattern | Takeaway for OpenEden | Status |
| --- | --- | --- | --- |
| OpenMW itself | `components/` (shared, never includes `apps/`) against `apps/openmw` (game); `MWBase` interfaces between subsystems; `esmtool`/`bsatool`/`niftest` use only `components` | the precedent; works because `components` was kept free of `apps`; fails inside `components` where `esm` includes `esm3` and `resource` includes records | read (section 1) |
| Wine | Layered DLLs: `ntdll`, `kernelbase`, `kernel32`, `user32` built as separate libraries with a rule that a lower layer never calls a higher one; a "unixlib" boundary for host calls | layering enforced by build targets and a rule list, not by convention | recalled, verify |
| Godot | Core in `core/`, `servers/`, `scene/`; optional `modules/` that register themselves with the core by a `register_types` entry; GDExtension for loadable code behind a C API | modules register record-like types (classes) with the core at start; a stable C API is a separate, later layer (OpenEden's P6) | recalled, verify |
| Unreal | Modules declared in `*.Build.cs`, each with explicit `PublicDependencyModuleNames` / `PrivateDependencyModuleNames`; Plugins group modules and have a `Type` and `LoadingPhase` | an explicit dependency list per module checked by the build tool; a loading phase per module | recalled, verify |
| ScummVM | One engine core with many game "engines" that implement an `Engine` base; detection tables identify the game; shared format code (archives, video) in `common/` and `engines/*` | one backend per game behind a common `Engine` interface, game detection separate from game code; one game per run | recalled, verify |
| OpenRA / Spring | The engine knows no game; games are mods (data plus code) loaded by name | strong separation of data from code, one game per session | recalled, verify |
| NifSkope | Reads `nif.xml` (the niftools description of every block, its fields, and the version conditions) and builds the block model at run time; one file describes all versions | a schema-driven reader supports every version by data; adding a version is editing the schema | recalled, verify |
| niftools/nifxml and Niflib | `nif.xml` is the schema; Niflib's C++ block classes are generated from it | the same schema also drives code generation | recalled, verify |
| OpenMW's NIF reader | Hand-written `read()` per block with version tests (`NIFStream::generateVersion`, `mBethVersion` thresholds in `niffile.cpp` and `node.cpp`) | adequate for the versions someone wrote; each new game costs hand work | read |

Three patterns recur: (a) layers enforced by a **checked** dependency list, not convention; (b) the game is a thing
the engine **selects once** (detection, then one backend); (c) shared formats sit below, with game detection separate
from the game's behaviour.

## 5. NIF in any version

### 5.1 What the reader already does

`components/nif/niffile.cpp` (`Reader::parse`): header string `NetImmerse File Format` or `Gamebryo File Format`;
version; endian byte for >= 20.0.0.4; user version for >= 10.0.1.8; record type strings for >= 5.0.0.1; record sizes
for >= 20.2.0.5; string table for >= 20.1.0.1; groups for >= 5.0.0.6; the Bethesda stream header (`mBethVersion`) under
the rule in the file; the factory map (`makeFactory`) lists node, property, controller, particle, physics, skinning and
Bethesda blocks including `BSTriShape`, `BSDynamicTriShape`, `BSSubIndexTriShape`, `BSSkin::Instance`,
`BSSkin::BoneData`. Versions constants: `VER_MW` 4.0.0.2, `VER_OB_OLD` 10.0.1.2, `VER_OB` 20.0.0.5, `VER_BGS`
20.2.0.7; stream versions `BETHVER_FO3` 34, `SKY` 83, `SSE` 100, `FO4` 130, `F76` 155, `STF` 172.

What it **refuses or lacks**:
- Any version except 4.0.0.0 and 4.0.0.2 unless the process flag is set (`supportedVers`, `sLoadUnsupportedFiles`).
- Hashed record type names (20.3.1.2: "Hashed record types are unsupported") and big-endian files.
- A scene description a non-OpenMW consumer can use: a consumer walks `Nif::Record`s and casts. The renderer's
  converter `components/vsgadapters/nif/nif.cpp` handles `NiGeometry` only (research note 0036 section 2), so no
  `BSTriShape` is drawn; `components/bodynif/skinnedshape.cpp` also reads `NiGeometry` only.
- `BSTriShape` data: Skyrim SE skinned shapes store vertex count 0 in the shape (the data is in `NiSkinPartition`);
  Fallout 4 skin weights exist only in the packed vertex data (note 0036 section 2). Decoding these into plain arrays
  is reading and belongs below the renderer.

Research note 0036 read 43 files of toddhow's installed Skyrim SE (20.2.0.7, user version 12, stream 100) and Fallout 4
(stream 130) with the existing reader and none threw. So the first deliverable is **not** a new reader: it is version
coverage made explicit and tested, the removal of the process flag, the Morrowind-only rule moved out of the reader's
default path, and an engine-neutral scene.

### 5.2 The schema route (nif.xml)

- **What it is (recalled, verify):** `nif.xml` in the niftools/nifxml repository describes versions (`<version>` tags
  for each game's version and user versions), basic types, enums, bitfields, compounds and niobjects, with fields
  guarded by attributes (`ver1`, `ver2`, `vercond`, `cond`, `userver`, `userver2`, `since`, `until`). The tools in that
  family (NifSkope, Niflib, pyffi, nifly, the Blender add-on) all read or derive from it. Whether OpenMW or OpenEden
  may take the file: **licence of `nif.xml` and of NifSkope must be read before anything is copied.** I recall a BSD
  3-clause licence for nifxml and Niflib and GPL-3.0 for current NifSkope: both **recalled, verify**. Per the root
  guide a dependency is recorded in `docs/dependencies.md` only when adopted; this note adopts nothing.
- **Generated against hand-written:**

| | Generated from the schema | Hand-written (today) |
| --- | --- | --- |
| Version coverage | every block and field the schema knows, by data | the blocks and versions someone wrote |
| Cost of a new game | update the schema | write and test blocks |
| Build | needs a generator step (a build tool, outputs committed or built), or a run-time interpreter | none |
| Speed | generated code can match hand-written; a run-time interpreter is slower | tuned (arrays read in bulk) |
| Debuggability for a learner (CLAUDE.md: toddhow learns C++ here) | generated code is large and unfamiliar | 11.9k lines of readable, reviewed code |
| Risk | schema mistakes become silent misreads | missing blocks become exceptions |
| Licence | depends on `nif.xml` (verify) | clean |

- **Recommendation:** keep the hand-written reader as the reader, and use `nif.xml` **only as an oracle** in a tool and
  tests, if its licence allows reading it in a dev tool: a script lists, per version in the table below, which blocks
  the schema has and which the factory lacks, and which fields' conditions differ. Decide on generation later, from
  that list. It is cheaper and keeps the project in control of the reader; it answers "like NifSkope" as "loads any
  version NifSkope loads, at the blocks the game needs", checked rather than assumed.

### 5.3 The versions that matter

The brief lists these; the stream numbers are the reader's own constants (cited above), the games' mapping is
**recalled, verify** where marked.

| Priority | Game | File version | User version | Stream version | Source |
| --- | --- | --- | --- | --- | --- |
| 1 | Morrowind | 4.0.0.2 (and 4.0.0.0) | none | none | `VER_MW`, `supportedVers` |
| 2 | Skyrim SE | 20.2.0.7 | 12 | 100 | `VER_BGS`, `BETHVER_SSE`; note 0036 |
| 3 | Skyrim LE | 20.2.0.7 | 12 | 83 | `BETHVER_SKY`; games' mapping recalled, verify |
| 4 | Oblivion | 20.0.0.5 (older assets 10.0.1.2) | 11 (verify) | 11 (verify) | `VER_OB`, `VER_OB_OLD` |
| 5 | Fallout 3 / New Vegas | 20.2.0.7 | 11 | 34 | `BETHVER_FO3`; user version recalled, verify |
| 6 | Fallout 4 | 20.2.0.7 | 12 | 130 | `BETHVER_FO4`; note 0036 |
| later | Fallout 76, Starfield | 20.2.0.7 | | 155, 172 | constants exist; not tested here |

### 5.4 Keeping reading engine-neutral

A **scene structure** (`Nif::Scene`, in the common compatibility pack, no VSG, no Jolt types; math as plain structs
of floats) built from a parsed `NIFFile` by a function with a version-aware options argument: nodes (name, local
transform, flags, children, extra data as key/value, controller descriptions as data); geometry as flat arrays
(positions, normals, tangents, UV sets, colours, indices, per-vertex skin influences normalised to a bounded count
with the original bone index and the count preserved), materials/shader properties as plain value structs including
texture paths and flags; skin as bone names plus inverse binds; collision as data. The renderer's converter
(`vsgadapters/nif`) is then a function from `Scene` to VSG nodes; today it walks the records. The Morrowind and
Jolt consumers (`nifjolt`, `lod/nifflatten`, `bodynif`) can move to the scene one at a time, with their existing tests
as the safety net. The Morrowind-only rules (the `bip01` root rule, the Morrowind animation conventions) become
options or post-processing in the Morrowind pack.

## 6. Implications for the plan

1. The formats layer is nearly independent already; what it lacks is **enforcement**. Step one is a rule checker, not
   a move.
2. The rules cannot move as directories. Each of the tangles 1 to 8 needs an abstraction (record store, type-erased
   record handle, content loader, save chunk handler, script host, record descriptors for Lua and the editor) *before*
   the code behind it can change owner.
3. A game pack cannot be a separate DLL with C++ headers at today's Morrowind code size without a stable seam; start
   with the pack **compiled in** behind the same contracts and loaded the same way later.
4. The first real value is the NIF layer: it unblocks Skyrim meshes in Eden Studio, the AdvancedBodies work
   (`docs/research/0036`), and has no rules question.

## 7. Not measured

- The cost in compile time of splitting `components` into libraries.
- Transitive include depth (only direct includes were counted).
- Whether `nif.xml` or NifSkope may be used; any claim about other engines' current layout.
- Skyrim record semantics (`esm4` coverage of Skyrim SE records beyond what `ESM4::Reader` reads).
