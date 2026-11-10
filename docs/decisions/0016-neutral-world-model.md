# 0016 — A neutral world model in the core, fed by every game pack

Date: 2026-11-10 · Decided by: toddhow ("go with option 3", 2026-11-10; recorded under his blanket approval of pending sign-offs) · **Status: ACCEPTED** (the direction; the open questions at the end of `docs/design/neutral-world-model.md` are NOT covered and keep their recommendations until he answers them).

Builds on decision 0013 (game packs, `docs/design/game-packs.md`) and answers the world-model question that task 0086's stage log left open ("which of options 2 and 3 to take for the engine").

## Decision
1. **Option 3.** The core gets a game-neutral world model: cells, the objects placed in them, and for each object a kind tag, a model, a collision hint, a transform, a few optional neutral components (owner, lock, teleport) and an opaque payload the pack owns. Each game pack (Morrowind and Skyrim SE first) feeds it from its own store. The engine renders, collides and simulates from the neutral model; **rules stay in the packs** (decision 0013).
2. **Not option 2.** Skyrim is not squeezed into the Morrowind-shaped `ESMStore`/`CellStore` record tuple, and the engine's existing experimental ESM4 path (`MWClass::ESM4Base`, `CellStore::loadRefs(const ESM4::Cell&)`, `MWWorld::Cell(const ESM4::Cell&)`) is not extended into the Skyrim route. Its fate is a question in the design document.
3. **Not option 1 as an end state.** Task 0086's Skyrim-specific store behind the `WorldView` contract stays as the pack's side of the adapter (and keeps feeding Eden Studio's cell viewer); it is not what the engine consumes.
4. **Staged and additive.** Morrowind keeps working unchanged through every stage: the first stages only add types, a read-only adapter and tests; the engine consumes the neutral model for non-Morrowind games first, behind a setting that is off by default; whether Morrowind ever reads its world through the neutral model is a separate, later decision.
5. **The value types live in the SDK** (`sdk/include/openeden/game/world_model.hpp`, `kWorldModelVersion` 0.1, a DRAFT that stage N2 freezes), because packs build against the SDK only and the types are plain values, exactly like `world_view.hpp`. The core's own store and the engine consumption live in `components/` and `apps/openmw/` in later stages. (An alternative, the types in `components/`, was considered; see open question 1.)

## What this decision does not do
It does not fix identity for saves, who builds cells on which thread, the engine's runtime handle (`MWWorld::Ptr` stays until a later decision says otherwise), the fate of the ESM4 path, or any date. Those are in `docs/design/neutral-world-model.md` with a recommendation each, and each stage is a task spec that toddhow approves. Stage N0 (types, the Skyrim adapter, tests, no engine change) is task 0099.
