# components/puddle

The puddle map: where water collects on the terrain, baked on the user's machine from their own game data (task 0095, stage W-b). Read the root `CLAUDE.md` and `docs/tasks/0095-puddles-and-wetness-v2.md` first. Nothing in the game reads the map yet (stage W-d); `openmw --bake-puddles` writes it.

## Files

- `puddlemap.*`: the payload of one cell (`openeden.puddlemap` layout 1) and its strict, canonical reader and writer. The header comment is the layout. The reader is the part that meets untrusted bytes: it checks every size against the bytes given before it allocates, returns a located `Puddle::Error` and never throws, and accepts only what the writer would write.
- `puddlebake.*`: `bakeCell` (the pure bake of one cell: window of land, fill, pits, pond, catchment, quantisation), `terrainHeightAt` (the ground on the triangles the renderer draws), `inputDigest` and `parameterDigest`. Land comes through `LandHeightSource`; the game's implementation is in `apps/openmw/puddlebaker.cpp`.
- `puddlepack.*`: the manifest (canonical one-line JSON, integers only), the Eden Archive pack (`puddles.edenpak`: key tag 0x14, entry type 0x0014, provenance on every cell), `contentDigest`.
- `puddlepartial.*`: the work folder of a bake (one file per finished cell) that makes a killed bake resumable.
- `puddlejob.*`: the whole bake as a job run in slices (`BakeJob::step`): digest pass, reuse from the pack on disk and from the work folder, bake, assemble, write, read back. A second run with the same land writes nothing.
- `puddlewindow.*`: the run-time CPU window (7 x 7 cells) with the staleness check. The GPU array texture of stage W-d is fed from it.

The pure maths (fill, basins, catchment, level) is in `components/render/puddlefill.hpp` and `puddlelevel.hpp`.

## Rules

- **Never commit a pack, a work folder or a rendering of one.** It is derived from the user's game files. Tests use generated land (`apps/components_tests/puddle/puddletestland.hpp`).
- **Deterministic:** no clock, no random numbers, no unordered containers in the baker; fixed neighbour order; integers for the manifest and the path lengths; the input digest hashes the cells' vertex heights as bits (no normals: the corner normals differ in the last bit between compilers, task 0060 review finding 6). Two bakes are byte-identical on one machine; across machines the DIGESTS agree (the pack file is compressed unless stored; the staleness check compares digests, not bytes).
- **A cell's result depends on its window only** (`halo` cells round it), never on other cells or on the order cells are baked in. A pit that crosses the window's edge is cut there (the window's border is a sink), so the halo must be larger than the pits of interest.
- **Settings are integers** (`BakeSettings`): spacing in units, the cap in 1/16 units, the radius in units. They go into the manifest and the parameter digest unchanged.
- **The terrain triangulation:** `Terrain::CreateIndexBuffer` (`components/terrain/indexbuffer.hpp`) splits a quad along one diagonal chosen by the parity of its vertex index sum; `terrainHeightAt` repeats that. If the renderer's pattern ever changes, change both.
- **Decisions of toddhow (2026-11-10)** are in the task spec: capacity from the floor, a maximum puddle depth, a minimum pond of 16 texels, a per-cell height base, a bounded catchment.
