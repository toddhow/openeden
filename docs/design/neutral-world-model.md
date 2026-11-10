# The neutral world model (decision 0016, task 0099)

Status: DRAFT design for stages N1 and later; stage N0 is built (task 0099). Everything marked **RECALLED-VERIFY** was not checked in the source at this commit. Numbers called estimates are mine. Section 7 open questions: confirmed by toddhow 2026-11-10 (go with recommendations); `Ptr` stays the runtime handle.

Companions: `docs/design/game-packs.md` (the tier this feeds from), `docs/tasks/0086-skyrim-se-game-pack-skeleton.md` (the Skyrim store and the world-model options), `docs/tasks/0098-moves-morrowind-rules-second-set.md` (what is moving out of the core), `sdk/include/openeden/game/world_view.hpp` and `world_model.hpp`.

## 1. What the engine consumes today (survey)

All paths are at commit `414f50d0c6` (the worktree this was written in), cited by file and function. Counts are from `grep` over `apps/` and `components/`; the method is stated each time.

### 1.1 The runtime object is a `Ptr` to a record-shaped `LiveCellRef`

- `MWWorld::Ptr` (`apps/openmw/mwworld/ptr.hpp`) is three pointers: a `LiveCellRefBase*`, a `CellStore*` and a `ContainerStore*`. Identity for the engine is the `LiveCellRefBase*` (`PtrBase::operator==` compares `mRef`; `PhysicsSystem` and the render scene key their maps by `ptr.mRef`: `PhysicsSystem::addObject`, `mObjects.emplace(ptr.mRef, obj)` in `apps/openmw/mwphysics/physicssystem.cpp`).
- `LiveCellRef<X>` (`apps/openmw/mwworld/livecellref.hpp`) is a template over a **record type** `X` (`X::sRecordId`, `mBase` is a `const X*` into the `ESMStore`). It already has constructors from `ESM::CellRef`, `ESM4::Reference` and `ESM4::ActorCharacter`.
- `CellStore` (`apps/openmw/mwworld/cellstore.hpp`) holds `mCellStoreImp->mRefLists` (`CellStoreTuple`, defined in `cellstore.cpp`), **a tuple of one `CellRefList<X>` per record type** (`CellStore::writeReferences` walks it with `Misc::tupleForEach`, `cellstore.cpp`). Loading a cell is `listRefs`/`loadRefs` overloaded for `ESM::Cell` and `ESM4::Cell` (`cellstore.cpp`, `visitCell4References`).
- The behaviour of a thing is a `MWWorld::Class` (`apps/openmw/mwworld/class.hpp`, about 110 virtual functions: rendering and physics insertion, activation, stats, inventory, tooltip, model, sounds, state reading and writing ...), chosen by the record's type number.

### 1.2 The seam where a placed thing enters the world

`apps/openmw/mwworld/scene.cpp`: `Scene::insertCell` runs `cell.forEach(insertVisitor)` and then `addObject(ptr, ...)` (two overloads). The first overload reads, per object: `getModel(ptr)` (`ptr.getClass().getCorrectedModel`), `makeDirectNodeRotation(ptr)` (rotation from `ptr.getCellRef().getPosition()`), then calls the class virtuals `insertObjectRendering(ptr, model, rendering.getObjects())` and `insertObject(ptr, model, rotation, physics)`, the mechanics manager (`Class::useAnim`, `isActor`), then the Lua and extension managers (`objectAddedToScene`). The second overload adds the physics object to the navigator. The incremental build of task 0060 queues the same `Ptr`s (`Misc::BuildQueue<Ptr>`, `Scene::insertCell`).

So the generic consumers of "a placed thing with a model, a transform, a collision shape, a type tag and per-game data" are:

| Consumer | What it reads from the object | Evidence |
| --- | --- | --- |
| Rendering | model path, position/rotation/scale from the cell ref, `useAnim`, and **two Morrowind records**: `ESM::Light` (`ptr.get<ESM::Light>()->mBase`) and `ESM::Container`'s organic flag | `MWRender::Scene::insertModel`, `apps/openmw/mwrender/scene.cpp` |
| Physics | the `Ptr` as key, the model (shape from the mesh), rotation, scale, `useAnim`, collision layer chosen by the class | `PhysicsSystem::addObject` |
| Navigator | the physics object's AABB, the cell's interior flag | `addObject` (second overload), `scene.cpp` |
| Activation and tooltips | `Class::activate`, `getToolTipInfo`, `canLock`, `getDoorState` ... | `class.hpp` |
| Scripts (mwscript) | `ptr.get<ESM::X>()` for many record types, `ESM::Script` text, `Class::getScript` | 39 `get<ESM::...>()` sites in `mwscript` |
| Lua | `ObjectId = ESM::RefNum` (`using ObjectId = ESM::RefNum`, `apps/openmw/mwlua/object.hpp`), `GObject`/`LObject` wrap a `Ptr` | 46 `get<ESM::...>()` sites, 147 `getClass()` sites |
| AI and mechanics | `CreatureStats`/`NpcStats` through the class, records through `ptr.get<ESM::NPC>()` and the store | 207 `get<ESM::...>()` and 532 `getClass()` sites |
| Saves | per-type `ESM::ObjectState`, `RefNum` written by `CellStore::writeReferences`, moved objects as `MVRF` records | `cellstore.cpp`; `WorldModel::write/readRecord` |

Counts of record reads (`.get<ESM::X>()`/`->get<ESM::X>()` on a Ptr, `apps/openmw` only): mwclass 216, mwmechanics 207, mwgui 146, mwworld 129, mwlua 46, mwscript 39, mwrender 39, mwsound 9, others under 6; `getClass()` calls in total 1,254 (mwmechanics 532, mwgui 265, mwworld 198, mwlua 147, mwrender 42, mwclass 36, mwphysics 19).

**What this shows.** Rendering, physics, the navigator and the build queue need only "model + transform + scale + a type tag + identity" (plus lights). Everything that reads a Morrowind record is rules, UI or scripting, which is exactly what decisions 0013 and 0014 move to packs. The model data and the rules data are separable; the *handle* (`Ptr`) is what every consumer shares, and it is record-shaped.

### 1.3 What already exists for non-Morrowind data (the ESM4 path)

The inherited engine already has an experimental ESM4 route, which this decision must account for:

- classes: `MWClass::ESM4Base<Record>` and `ESM4Impl::insertObjectRendering/insertObjectPhysics` (`apps/openmw/mwclass/esm4base.hpp/.cpp`: insert a model, add a physics object on `Layers::WORLD`), `esm4npc.*`, `light4.*`, registered in `mwclass/classes.cpp`;
- cells: `MWWorld::Cell(const ESM4::Cell&)` (`mwworld/cell.hpp`), `CellStore::listRefs/loadRefs(const ESM4::Cell&)` and `loadRef(const ESM4::Reference&)`;
- identity: `ESM::RefId` already has a `FormId` alternative (`components/esm/refid.hpp`, `RefIdType::FormId`), and `ESM::RefNum` is an alias of `ESM::FormId`, an `{ index, contentFile }` pair (`components/esm3/refnum.hpp`, `components/esm/formid.hpp`).
It loads Oblivion, Fallout 3/NV and Skyrim LE style data through `components/esm4`, as experimental upstream code. It does not read Skyrim SE form version 43/44 plugins with the pack's decoders, and nothing in it is a contract.

### 1.4 How many files touch it (and why two sets of numbers exist)

Method used here: `grep -rlE '#include [<"].*mwworld/ptr.hpp'` etc. over `apps` and `components` (direct includes of that exact header, `.cpp` and `.hpp`). Result: `ptr.hpp` 82 files, `cellstore.hpp` 49, `class.hpp` 116, `esmstore.hpp` 127, `mwbase/world.hpp` 138, `esm3/cellref.hpp` 30, `esm3/loadcell.hpp` 49. Files that **name** `MWWorld::Ptr`/`ConstPtr` (transitive users count too): 376, 347 of them under `apps/openmw`; files naming `ESMStore`: 96; files including any `mwbase/` header: 301.

The guide and research note 0043 quote 104 includers of `Ptr` and 163 of `ESMStore`. I could not reproduce them: the method is not recorded there and the tree has changed since (moves 3 to 8a of task 0085/0098 took dialogue, crafting and the object classes out of `apps/openmw`). Treat the 104/163 as **RECALLED-VERIFY**; the order of magnitude agrees: **a Ptr-free engine is a rewrite of well over three hundred files**, which drives stage N3's shape (section 5).

## 2. The model

### 2.1 Where the types live (decision 0016, point 5)

`sdk/include/openeden/game/world_model.hpp`, namespace `openeden::sdk::game`, `kWorldModelVersion` {0, 1} (DRAFT until N2). Reasons: packs build against the SDK only (guide, architecture rules); the types are plain values like `world_view.hpp`; the header is **types only**, because a contract carries no implementation. It reuses `WorldId`, `BaseKind` (as `ObjectKind`) and `WorldVec3`-adjacent ideas from `world_view.hpp`. `CI/check_extension_sdk.sh` now keeps its version policy paragraph. The pack already links `components`, so a pack could also include a `components/` header; I chose the SDK so that a third-party game pack (a future stable ABI, extension phase P6) needs nothing else. This is open question 1.

### 2.2 Types

- `WorldCell { id, editorId, name, interior, hasGrid, gridX, gridY, world, std::vector<WorldObject> objects, unresolvedBases, droppedObjects }`. Objects are in ascending placement-id order (determinism).
- `WorldObject { id, base, kind (ObjectKind = BaseKind), baseEditorId, modelPath, collision (CollisionHint), transform (WorldTransform), persistent, initiallyDisabled, optional owner, optional lock, optional teleport, payload }`.
- `WorldTransform { Vec3d position; Quatd rotation; double scale }`: **canonical** (double precision, quaternion, uniform scale). The per-game Euler order and sign are the pack's job, done at one named function (`game_skyrimse::eulerToQuat`; the Morrowind side is the engine's `makeDirectNodeRotation` in `scene.cpp`, from `ESM::Position`'s three angles, **RECALLED-VERIFY** that it can be reproduced exactly from a quaternion for a round trip).
- `CollisionHint { source: None | FromModel | Box; bounds }`. Today the engine derives every placed object's shape from the model (`PhysicsSystem::addObject` asks the shape manager for the mesh); `FromModel` says so, `Box` is for objects with bounds and no model (invisible collision volumes, triggers), `None` for markers.
- `OwnerComponent`, `LockComponent`, `TeleportComponent`: the neutral parts of ownership, locks and doors (a destination cell and door and a destination transform). Skyrim SE's store does not decode XOWN, XLOC or XTEL yet, so the adapter leaves them empty and a test pins that. Morrowind's `ESM::CellRef` has owner, faction, lock level, key and door destination (`apps/openmw/mwworld/cellref.hpp`), so they map directly.
- `PackPayload { uint64 handle }`: an opaque number the pack owns and the core only stores and returns; **never a pointer** (guide: IDs not raw pointers). For Skyrim the base form id. A pack that needs more than a number keeps its own table keyed by `WorldObject::id`.

What is deliberately not in the model: actors' stats, inventories, AI packages, scripts, dialogue, quests, record data of any game. Actors and items appear as typed handles (`ObjectKind::Actor`, `Item`) with their placement; their *data* is the pack's, reached through the payload (stage N5).

### 2.3 Identity

- `WorldId` is an opaque 32-bit number the pack mints; for Skyrim SE it is the load-order form id (`world_view.hpp` already says consumers must not parse it).
- The engine's persistent identity today is `ESM::RefNum { mIndex, mContentFile }` (`components/esm3/refnum.hpp`: `using RefNum = ESM::FormId`; `WorldModel::mPtrRegistry`, `getPtr(RefNum)`, `assignSaveFileRefNum`), also used as the Lua `ObjectId`. `ESM::FormId::fromUint32` already splits a 32-bit form id into a 24-bit index and an 8-bit content file; that is **not** the same as the engine's content-file index (it is the plugin's slot in the *game's* load order, and a light plugin (0xFE) has a different split, **RECALLED-VERIFY**). So the map from `WorldId` to `RefNum` must be a **session table** (pack id + `WorldId` to a fresh `RefNum`), written into the save, not an arithmetic conversion. Recommendation in question 4.

### 2.4 Ownership, lifetime, threading

- A `WorldCell` is an immutable snapshot owned by the core's store (stage N1) once built; packs hand over copies by value. Objects are referred to by index while the snapshot lives; the runtime handle for gameplay is separate (section 5, N3).
- **Who builds cells, on which thread.** Today `WorldView::loadCell` is main thread only (its header) and `SkyrimWorldView` keeps a 16-cell cache without locks. The engine builds cells on the main thread (`Scene::loadCell/insertCell`) with a read-only preload on a worker (`apps/openmw/mwworld/preloadcell.hpp`, used by `Scene`). Neutral cells need a thread-safe read to be preloaded; N2 decides between (a) main-thread only, built just ahead through the incremental queue of task 0060, and (b) a documented thread-safe `loadCell` on packs (a mutex around the cache, the decoder is already pure). Recommendation: (a) first.
- A pack's store (`SkyrimWorldView`) lives until the pack is destroyed (`world_view.hpp`); the core never keeps a pointer into it past `stop()`, it keeps the snapshot.

### 2.5 How `world_view.hpp` relates

`WorldView` is the pack's pull-style read interface (lists, lookups, one cell on demand, `PlacedObject` with the game's float Euler angles). `WorldCell` is what loading a cell produces for the engine: the same ids and kinds, canonical transform, components and payload. The first version is therefore an **extension in a sibling header** rather than a new major of `world_view.hpp`: the 1.0 contract stays frozen, any `WorldView` can be adapted to a `WorldCell` by a pack (the adapter takes a `WorldView&`, so it works over any view, including the scripted one in the tests). If the types prove right, N2 may fold `PlacedObject` into `WorldObject` in a 2.0 of `world_view.hpp`; until then nothing breaks.

## 3. How Morrowind migrates

1. **Adapter first, read-only (N1).** A projection from `ESMStore`/`CellStore` to `WorldCell` for any Morrowind cell: `ESM::CellRef` gives position, rotation, scale, owner, lock, key, door destination, `ESM::Cell` gives the cell fields, the class gives the model. It changes no engine code; its value is a comparison (counts and fields against the live cell) and the Eden Studio and tooling consumers.
2. **The engine consumes the neutral model for non-Morrowind games first (N3).** Morrowind keeps the existing path, so "Morrowind unchanged" is true by construction until a later decision.
3. **Native later or never.** Morrowind placements carry script state, AI state, container contents, moved objects, respawn, `RefData` (`apps/openmw/mwworld/refdata.hpp`). The neutral model carries placement, not state. Recommendation: Morrowind never reads its world *through* the neutral model at run time; it feeds it only as a projection. If toddhow later wants a single path, that is stage N7 and its own decision.

## 4. Rules that apply to every stage

- **Off means unchanged.** No stage changes Morrowind's behaviour. N0 adds files; N1 adds a store nothing reads; from N3 on the Skyrim route is behind `[Game] neutral world` (off by default, in the living test profile with a comment), and the proof is the one the guide asks for: `git diff --stat` shows what engine files changed, plus a frame comparison of Morrowind with the setting at its default against the commit before.
- **Determinism.** Cells list objects by placement id; building a cell twice, or from two fresh views, gives equal results (tested in N0 on one platform; bit-identical floating point across compilers is not claimed, the guide records GCC's fused multiply-add differences).
- **Allocation.** Cell building is a load-time operation and allocates. Per-frame code never sees a `WorldCell`: after N3 it sees the runtime handle, as today.
- **Hostile input.** The adapter refuses non-finite or huge values and counts them (`droppedObjects`); the core store (N1) re-validates because it must not trust a pack.
- **Layers.** `CI/check_layer_rules.sh` stays at 104 exceptions or fewer: the SDK header is outside the checked tree; N1's `components/worldmodel` is layer `engine` (it may include `base` and `formats` only; it takes the SDK header), which forbids it from including `apps/` or any game pack.
- **Full loop.** Every stage from N1 touches a core area the guide lists for the full loop (`apps/openmw` world code, saves) or the SDK; none qualifies for the lighter loop.

## 5. Stages

Estimates are mine, in working days of one agent, plus or minus 50 percent. "Full loop" means the guide's loop with a separate test writer and a fresh reviewer.

| Stage | What | Acceptance (summary) | Risk | Est. |
| --- | --- | --- | --- | --- |
| **N0** (built, task 0099) | the SDK value types, the Skyrim adapter (`eulerToQuat`, `toNeutral`, `loadNeutralCell`), tests, a read-back on the installed masters | tests red against stubs then green; the five masters load as neutral cells with 0 failures and the same object counts as the contract; no engine source file changed | low | done |
| **N1** | the core store `components/worldmodel` (layer `engine`): `WorldModelService` in the SDK for packs to register a source, snapshot ownership, re-validation, a handle table; a read-only Morrowind projection from `ESMStore` | equality tests of the projection against `CellStore` for generated plugins and nine real cells (counts, transforms, models, door destinations); the store refuses hostile snapshots; layer check unchanged | medium (reading `CellStore` without side effects) | 4 to 6 |
| **N2** | decisions and freeze: identity (section 2.3), thread rule (2.4), units per game (Skyrim and Morrowind both use game units, the unit-per-metre value is in `Misc::GameConventions` for Morrowind; Skyrim's is **RECALLED-VERIFY**), types to 1.0; Eden Studio's viewer reads `WorldCell` | toddhow signs the questions; version policy takes effect; viewer frames unchanged | low | 2 to 3 |
| **N3a** | engine consumption keeping `Ptr` as the runtime handle: a `NeutralBase` record type appended to `CellStore`'s tuple, `MWClass::Neutral*` classes that implement `insertObjectRendering`/`insertObject` from the neutral data (static, door, light), `Scene::insertCell` unchanged; `game=skyrimse` stops ending the session when `[Game] neutral world` is on | a Skyrim SE interior (the Whiterun inn of task 0086) renders its meshes and the player walks on its floor without falling through; Morrowind frames at the default setting equal the commit before; Lua `ObjectId` works for neutral objects | **high** (the record tuple, the registry, `LiveCellRef` templates; light and container special cases in `MWRender::Scene::insertModel`) | 10 to 15 |
| **N3b** (alternative to N3a) | a Ptr-free engine path: render and physics keyed by a neutral handle | not recommended: 376 files name `Ptr` (section 1.4) | very high | 40+ |
| **N4** | doors and cell changes (teleport component, door classes), Skyrim record decoding for owner, lock, teleport (XOWN, XLOC, XTEL) | a door between two Skyrim interiors works; locks and owners round-trip through the adapter | medium | 5 to 8 |
| **N5** | actors and items as typed handles with pack payloads (placement only; stats, AI and inventory stay the pack's) | NPCs appear at their placements with their meshes; none of the Morrowind actor code is entered | high (skeleton conventions, task 0070) | 10+ |
| **N6** | saves: the session identity table and the pack's chunks (decision 0013) | a save made in Skyrim SE loads back with the same objects; a changed load order is detected and refused | high (save compatibility) | 5 to 8 |
| **N7** | the old ESM4 path retired; optionally Morrowind native (decision for toddhow) | tests of the removed path deleted with reasons | medium | 3 to 5 |

**Parallel work.** N1 and the Skyrim record additions of N4 (XOWN/XLOC/XTEL in `records.cpp`) are independent. N2's Studio change can run alongside N3a's design. N3a itself, N5 and N6 are sequential because they share the record tuple and the registry.

## 6. What happens to the existing ESM4 path

Recommendation: leave it untouched until N3a works for one Skyrim interior, mark it experimental in the guide, then retire it in N7 (its tests, if any, deleted with reasons recorded). It is the only code in the tree that already runs a non-Morrowind cell through `Scene`, so N3a should read it for the places a new record type must be registered (`mwclass/classes.cpp`, `CellStore`'s tuple, `ESMStore`'s store tuple) before writing anything. Not removed, not extended now.

## 7. Open questions for toddhow

1. Types in the SDK (my choice, so a pack needs nothing else) or in `components/worldmodel`?
2. Keep `Ptr` as the runtime handle (N3a, recommended) or a Ptr-free engine (N3b)?
3. The existing ESM4 path: keep until N3a, then retire (recommended), or keep as a second route?
4. Save identity: a session table written into the save (recommended) or form-id arithmetic?
5. Cell building: main thread only, built ahead through the incremental queue (recommended), or a thread-safe `loadCell` on packs?
6. Does Morrowind ever read its world through the neutral model at run time (recommended: no, projection only)?
7. When do the types freeze to 1.0 (recommended: at N2, after the Studio viewer and N1 consume them)?
8. Should `ObjectKind` stay an alias of `BaseKind` (cheap, couples the two enums) or become its own enum?
9. Units: does the model carry a units-per-metre value per game (recommended: per game, in the pack's `GameContract`, as `Misc::GameConventions` does for Morrowind)?
