# Eden Archive Format and Asset Streaming Guide

Status: draft, 2026-11-01; open decisions settled by toddhow the same day (section 17; `docs/decisions/0010-rendering-and-studio-answers-2026-11-01.md`, items 24 to 26 and 29). Living document. Companion to the root `CLAUDE.md`, `docs/design/world-lod-and-streaming.md` (which reviews this draft in its section 12 and sets what streaming needs from the format) and, for the container work, task specs 0050 to 0052. Earlier drafts named a Body System guide and a UI System guide as companions; neither exists in this repository, so they are not referenced.

This guide covers how OpenEden stores and streams assets: the separate data files, the Eden Archive Format (the pack container), KTX2 textures, how glTF meshes and Slang shaders fit in, the derived-data cache, and the plan to make the container an open standard. It is a design document. Nothing here is built yet. Words, as in the root `CLAUDE.md` (decision 0006): an **extension** is OpenEden's native code system; a **plugin** is an ESP or ESM data file (or a Qt or OpenSceneGraph plugin); a **record file** is a file of merged records (ESP/ESM today). Every format, license, or library fact that has not been checked against a source is marked **verify**.

## How to use this document

- Update it in the same change as any decision, milestone result, or scope change, and add a row to the changelog (section 20).
- Tick milestone checkboxes only when the gate in that milestone passes.
- Settled choices move into numbered decision records under `docs/decisions/`. Section 17 lists the decisions and their status.
- Never state a format or library fact as settled without a source. Mark it **verify** until checked.

## 1. Goals

- Stream textures, meshes, and other assets on demand so high-resolution modded content does not hit a hard VRAM wall.
- Keep records (small, merged) and assets (large, streamed) in separate files built for their different access patterns.
- Use open formats throughout: glTF 2.0 for meshes and animation, KTX2 for textures, Slang for shaders, and an open archive format for packaging.
- Keep existing content usable through read-only importers, legacy archive reading and loose-file support.
- Serve the streaming plan of `docs/design/world-lod-and-streaming.md`: keyed tile entries, verifiable chunks, a safe update story, and derived packs written beside a mod's packs.
- Make the archive container an open standard that mod tools and other engines can implement.

## 2. Decisions so far

| Decision | Status |
| --- | --- |
| Assets live in separate data files, not inside the record files (ESP/ESM today) | Direction chosen |
| Create a new, minimal archive container instead of reusing BSA, BA2, or ZIP at runtime | Direction chosen |
| Name: Eden Archive Format; short name and extension per decision 1 of section 17 (`.eaf` rejected, `.edenpak` provisional) | Decided, toddhow 2026-11-01; final extension fixed by the spec task (0051) |
| The container is built **first**, before the world-LOD tile cache (no private cache layout stage) | Decided, toddhow 2026-11-01 (decision record 0010, item 24) |
| The container is an open standard with a permissive reference library | Direction chosen |
| Textures use KTX2 as the payload format, with DDS read for legacy content | Direction chosen |
| Meshes and animation use glTF 2.0 (glTF animation clips can be read by CreativeMotion's reader; mesh loading from glTF is not implemented, see section 9) | Direction chosen |
| Shaders use Slang, compiled to SPIR-V at build time (decision 0009; runtime Slang is not adopted) | Direction chosen |
| Derived data is never written inside an author's pack. It lives in the cache folder, **using this same container** (derived packs, section 11) | Decided, toddhow 2026-11-01 (world-lod doc 12.4) |

## 3. Principles

1. **Two kinds of data, two kinds of file.** Records merge field by field. Assets cannot merge and only need clear priority and a report.
2. **Packs are immutable and addressed by content hash.** That makes caching, dedupe, and integrity checks simple.
3. **Source form and packed form.** A plain folder (or ZIP of one) is the authoring and interchange form. The packed form is optimized for streaming and produced deterministically.
4. **Derived data is rebuildable.** Anything generated from source assets goes in a cache keyed by source hash plus tool version, and the cache can always be deleted.
5. **Own the thin layer, use proven parts inside it.** The container is custom and small. Compression, hashing, and texture formats are existing, well-tested components.
6. **Treat every pack as untrusted input.** Bounds-check everything, cap sizes, and fuzz the parser.
7. **Priority and conflicts belong to the engine.** The virtual file system resolves them identically for packs, loose files, and legacy archives. The container does not.
8. **Keep the standard small.** The container describes how to store and address chunked, hashed files. Game-specific meaning lives elsewhere.

## 4. Why assets get their own files

- **Access patterns differ.** Records are small and parsed or memory-mapped at startup. Assets are large and loaded on demand, in pieces once streaming exists.
- **Update cadence differs.** A mod author fixing a stat should not force a multi-gigabyte re-download.
- **Conflict handling differs.** Records merge. Assets only have priority.
- **Integrity differs.** Assets are immutable once packed, so content hashes work well.

### File kinds

| Kind | Contents |
| --- | --- |
| Record file | Records and patches (ESP/ESM today; a new OpenEden record format is not designed) |
| Asset pack | Meshes, textures, audio, shader blobs, other binary data |
| Derived pack | An asset pack written by an OpenEden tool (for example the world-LOD baker) from other data, kept in the cache folder (section 11) |

Which packs belong to a mod is today decided by the `data=`, `content=` and `fallback-archive=` lines of `openmw.cfg` (see task 0050). A mod manifest naming a mod's packs is a **proposal only**: no such manifest exists in this repository (the only manifest is the `openeden.extension` v1 manifest for extensions), and it is not part of this container standard. Lua scripts keep OpenMW's `.omwscripts` mechanism; extensions have no Lua API (decision 0003), so the earlier idea of script permissions in a manifest is dropped. Loose files in a folder remain supported for development and for existing mods.

### How records refer to assets

Records reference assets by a path, and the virtual file system resolves the path (the namespaced form `mymod:meshes/sword.glb` of the first draft is not adopted; paths stay as OpenMW uses them). Section 13 and decision 5 of section 17 settle paths versus IDs.

## 5. Why a new container

| Candidate | Why not as the runtime format |
| --- | --- |
| ZIP | Entries compress as one stream, so you cannot seek inside them. No alignment guarantee. A history of parser edge cases (duplicate names, odd paths). Good for authoring, weak for streaming |
| BSA / BA2 | Legacy Bethesda formats, documented mostly by community reverse engineering. No per-entry content hashes and not designed for streaming layout. Support as read-only import. **Verify** the details of BA2 texture chunking |
| SquashFS-style filesystems | Designed around mounting, awkward cross-platform |
| tar plus an index | No built-in index or alignment, so the missing parts would be invented anyway |

The part to write is small: a header, an index, a chunk table, and aligned data blocks. Owning it gives exactly the layout streaming needs.

## 6. Container layout (draft)

All multi-byte values are little-endian. All offsets and sizes are 64-bit, since packs will pass 4 GB.

- **Header and footer:** magic bytes, format version, flags, and a checksum, so tools can identify and validate a pack cheaply. Reserved fields for growth.
- **Index:** sorted for fast lookup, memory-mappable. Each entry has the normalized path (hash of the path plus a string table for the real name), entry type, size, content hash, and the offset of its chunk table.
- **Chunk table per entry:** chunk offsets, compressed and uncompressed sizes, and a **codec ID per chunk**. The per-chunk codec ID lets a codec be added later without changing the format.
- **Data blocks:** aligned to 4 KiB so they work with memory mapping and with direct, sector-aligned asynchronous reads.
- **Entry types:** texture, mesh, audio, shader, generic. The type tells the engine which typed layout applies. Everything else is a plain chunked blob.
- **Limits:** maximum path length, entry count, chunk size, and decompressed size are defined in the spec and enforced by readers.
- **Excluded:** executable content, nested archives, encryption, and DRM.

## 7. Compression and hashing

- **Compression:** zstd in independent chunks, with the chunk size tuned by measurement (a starting range of 64 to 256 KiB). zstd is dual-licensed BSD or GPLv2 (project README, https://github.com/facebook/zstd, checked 2026-11-01); the BSD option is the one to use. Whether to use zstd's seekable-format framing or the pack's own chunk table: the pack's own chunk table (section 6), so each chunk can be verified on its own (section 14).
- **Already-compressed data** (block-compressed textures, audio codecs) is stored uncompressed or lightly compressed, since recompression gains little.
- **Hashing:** BLAKE3 for content hashes. Its README (https://github.com/BLAKE3-team/BLAKE3, checked 2026-11-01) states CC0 1.0, Apache 2.0, or Apache 2.0 with LLVM exceptions, and describes it as a Merkle tree capable of verified streaming and incremental updates. Which library build to use (official C implementation, SIMD optional) is **verify** and decided with the dependency record. Do not design a custom hash or codec.
- **Verification:** hashes are checked before content is trusted, lazily for large entries.

## 8. Textures: KTX2

KTX2 is a Khronos open standard designed around Vulkan.

### Why KTX2

- It has a per-mip-level index, which suits mip streaming: the small tail mips stay resident and higher mips load on demand.
- It supports supercompression (zstd) and the Basis Universal transcodable format, so one file can be transcoded to the block-compressed format the GPU supports. **Verify** the current details of both.
- It carries format and color-space metadata properly.

### A note on DDS

DDS is a Microsoft format, but it is publicly documented and reading or writing it is not a licensing problem. The reasons to prefer KTX2 are technical, not legal. Existing mods ship DDS, so keep it readable.

### How it works in a pack

- A texture entry stores KTX2 data, and the pack aligns its chunks to mip-level boundaries so each level is one read.
- The small tail mips are contiguous so a single read fetches the always-resident part. Higher mips are separate chunks.
- The pack knows the chunk boundaries. The payload remains a standard KTX2 file, so other tools can still read it. **Verify** the spec's level ordering and alignment rules against this layout.
- **BCn versus Basis Universal:** direct BC7 gives the best quality and a simple path on desktop GPUs. Basis is more portable (mobile, handhelds) and smaller, but costs a transcode step and some quality. Choose per asset, with a documented default.
- **Legacy DDS:** convert at pack time or on load, and cache the result.
- **Tools:** KTX-Software's licence page (https://github.com/KhronosGroup/KTX-Software/blob/main/LICENSE.md, checked 2026-11-01) says its own files are Apache-2.0, but the repository bundles material under many other licences, and one bundled file (`external/etcdec/etcdec.cxx`, Ericsson) is stated **not** to be open source. So depending on KTX-Software means checking every file under `LICENSES/` for what is actually linked, or using only a KTX2 reader/writer we write ourselves from the specification; this is **verify** and nothing is added by this document.
- **Partial installs:** a mod can ship a low-resolution texture pack and an optional high-resolution pack as a separate dependency.

### Platform note

If Windows DirectStorage with GPU decompression is a target, **verify** which compression format it requires (it may have its own GPU-friendly codec), because that could change the texture codec. The per-chunk codec ID allows adding it later for those entries, but decide before the first packs ship. See decision 3 in section 17.

## 9. Meshes and animation: glTF

glTF 2.0 is the open alternative to NIF. What exists today: CreativeMotion's reader (`components/creativemotion/anim/gltf`, `opas::anim::readGltf`, built on cgltf) reads glTF **animation clips**, and a unit test feeds it a `.glb` the Morrowind compatibility pack produced from a `.kf` file (`apps/components_tests/extension/test_compat_morrowind.cpp`). I found no engine code that loads glTF **meshes** for rendering; `docs/dependencies.md` lists vsgXchange as providing glTF loaders, but whether the engine calls them is **verify**, and cgltf's licence is not yet recorded in `docs/dependencies.md` (the task that relies on it records it). Core glTF coverage: node hierarchy, meshes with multiple UV sets and vertex colors, skinning, morph targets (needed for facial and body animation), cameras, lights, PBR materials, and animation. The binary form (`.glb`) packs a model into one file.

### Extensions to evaluate (verify status of each)

| Need | Extension |
| --- | --- |
| KTX2 textures | `KHR_texture_basisu` |
| Compressed geometry | `EXT_meshopt_compression`, `KHR_mesh_quantization` |
| Mesh LODs | `MSFT_lod` (**verify** whether ratified or in use; our LOD data is a different, hierarchical structure and must not depend on it) |
| Instancing | `EXT_mesh_gpu_instancing` |
| Animating arbitrary properties | `KHR_animation_pointer` |
| Material variety | the `KHR_materials_*` family |
| Physics | `KHR_physics_rigid_bodies` (check whether ratified or still a draft) |

### What glTF lacks and NIF has

Particle systems, text keys and animation events (footsteps, hit frames), collision for older games, per-object game flags, and legacy shader properties. Define namespaced custom glTF extensions (`EDEN_*`, not to be confused with OpenEden extensions) for these as a published spec. Evaluate the Khronos physics extension before inventing a collision extension, since collision maps onto Jolt shapes.

### Runtime use

glTF is excellent as an authoring and interchange format but is not laid out for a streaming or cluster-based renderer. Load `.glb` and convert to an engine-native cached form (meshlets, LOD chains, quantized streams) keyed by source hash. The `.glb` stays the source of truth. The importer from NIF is lossy: it carries meshes, skeletons, weights, materials, and basic animation, and must report what it dropped.

### Legacy materials

glTF's native model is metallic-roughness PBR. Preserve original legacy parameters (ambient, diffuse, specular) in an extension and convert at load, so nothing is lost.

### Typed layouts in the pack

Meshes start as plain chunked blobs. Add a typed layout for cluster hierarchies only when the virtualized-geometry work defines what it needs.

## 10. Shaders: Slang

Decision 0009 (accepted) adopts Slang for new shaders, compiled to SPIR-V at build time by a pinned `slangc`; the game loads the `.spv` files and never runs Slang. **Runtime Slang is not adopted** here and would be a separate decision. Slang's hosting, licence and platform coverage are recorded by decision 0009 and `docs/dependencies.md` (**verify** there, not restated here).

- It compiles to SPIR-V for Vulkan and can target HLSL, GLSL, Metal, and WGSL.
- Modules, interfaces, and generics help manage the permutation explosion from many material types and skins.
- A reflection API lets the engine discover bindings at runtime.
- One language covers compute passes (culling, skinning, morphing, soft-body) and graphics.

Plan for:

- Compile offline and ship SPIR-V with a pipeline cache. Mod shaders: the existing `.omwfx` post-processing mechanism stays, kept working through Slang's GLSL input (root `CLAUDE.md`, 2026-10-29); whether packs hold SPIR-V for mods is decision 7 of section 17 (deferred).
- Material parameters and pre-approved modules are safer than arbitrary shader code; if shaders are ever allowed in packs, validate them and cap resource use.
- Pin toolchain versions, since the language is changing quickly.

Compiled shader blobs are typed entries in packs.

## 11. Derived data cache and derived packs

Several designs generate data from source assets: converted animations, derived body proxies, meshlet hierarchies, transcoded textures. Keep all of it in a separate on-disk cache keyed by source hash plus tool version.

- Packs stay exactly as authors shipped them. The cache can be deleted and rebuilt at any time.
- Derived data is stored in the cache folder **using this same container** (a *derived pack*, flagged as such in its header, section 14.7). It is never written inside an author's pack. Authors who want to ship pre-baked data ship it as their own, separate pack, which the engine treats like any other pack (it is not a derived pack and is not tied to this machine's inputs).
- Cache invalidation is by key only. Define one clear rule and test it.
- Cache size limits and cleanup are part of the settings.
- The **baker** (world-LOD design) writes derived packs both ahead of time (an offline run whose result can be shipped or kept) and in the background while the game runs (applied after a restart): decision record 0010, item 23. The container therefore needs the update story of section 14.8.

## 12. Virtual file system, priority, and conflicts

- One virtual file system resolves a path to a file across loose folders, packs, and legacy archives.
- Pack priority and loose-file overrides follow the same rules as the record layer, with the user layer winning.
- If two mods supply different content for the same path, write a conflict report. If they supply identical content (same hash), there is no conflict, which improves on archives where duplicates silently shadow each other.
- Key the engine on content hash where possible so identical assets are shared in the cache and in memory.
- Normalize paths in one place only: case, Unicode normalization, and separators differ across platforms. Test it.

### Legacy support

Read BSA and BA2 archives and OpenMW's existing data arrangement through the same virtual file system, so legacy mods work without conversion (task 0050). Provide a packer that turns a loose folder into a pack, and an unpacker, with round-trip tests (task 0052). Fallout: New Vegas is the game targeted later: its archives must read through the VFS; which BSA version it uses is **verify** (the repository's reader accepts versions 103, 104 and 105, `components/bsa/compressedbsafile.cpp`).

## 13. Asset references: paths or IDs

Paths are simpler and keep legacy compatibility trivial. Stable asset IDs would make renames and replacements safer, but add indirection for every authoring tool. Decided (decision 5 of section 17): paths for author-facing references, the content hash as the cache and dedupe key, stable IDs only if a concrete need appears.

## 14. What streaming needs from the format

These requirements come from the streaming plan (`docs/design/world-lod-and-streaming.md`, sections 3 to 5 and 12.3) and are **accepted** by toddhow (decision record 0010, items 25 and 29). Where a layout is not yet known they are requirements on the specification task (0051), not byte layouts; typed layouts are frozen only after the streaming prototype proves them (section 16, "freezing too early").

### 14.1 Index, chunks and reads

- Index up front (or a fixed-size footer) so a pack never needs scanning.
- Aligned, independently decompressible chunks, so partial reads work.
- A **range-read API** on the reader: positional reads, thread-safe, no shared mutable state. The engine's IO threads use `readRange(file, offset, size, destination)`; the VFS gets a range interface beside `istream` (task 0050). No memory mapping in the first version, so files can be replaced at will (14.8).
- The wider streaming machinery (VRAM budgets from memory-budget queries, LRU eviction, a prefetcher using player velocity and cell adjacency, a staging ring buffer on the GPU side) belongs to the engine, not the container; the container only has to make its reads cheap.

### 14.2 Keyed entries and tile entry types

- **Keyed entries.** Besides the path index (normalized path hash plus a string table), a second index form addresses entries by a **64-bit key**: a type tag plus the Morton-coded tile key (worldspace, level, x, y; world-lod doc 3.2). It has no strings, is sorted and binary-searched or perfect-hashed, and stores the entries of a tile tree in **Morton order** so spatial neighbours are neighbours on disk. A hundred-by-hundred-cell map has about 13,000 tiles over all levels (the world-lod doc's estimate), which is why they are never looked up by name.
- **Tile and cluster entry types** (terrain tile, object cluster / HLOD, impostor atlas, grass coverage map). The container reserves the type tags and per-entry fields now; the byte layouts of their payloads are defined by the LOD stage specs (S1 onward of the world-lod doc) from the payloads of its section 3.2, after the prototype, and registered in the registry (task 0051).
- Limits for tile entries (maximum counts and sizes) are part of the spec and the conformance corpus.

### 14.3 Textures: mip tail layout

For a texture entry the pack states which mips form the always-resident **tail** (all mips up to a stated size, for example 64 x 64, to be set), stores them contiguously and first, and gives one chunk per larger mip. The payload stays a standard KTX2 file with valid level offsets if the layout allows it (**verify** against the KTX2 specification, which from memory stores the smallest mip first; not yet checked). Block-compressed legacy textures (DDS in BC1, BC3, BC5, BC7) are wrapped in KTX2 **without re-encoding**; BC7 is used only for textures we author, such as baked tiles (decision 4 of section 17).

### 14.4 Verified streaming

Each **chunk** must be verifiable on its own, not only the whole entry. Either use BLAKE3's tree structure (its README describes verified streaming) or store a per-chunk hash; the choice is made in the spec task (0051) and recorded in the registry.

### 14.5 Content-hash keys, provenance and hints

- Every entry carries a content hash. A **derived** entry also carries a **provenance record**: the digest of its source hashes, the tool id and version, and the parameters, so a loader can decide validity without opening the sources (world-lod doc 4.5).
- Per-entry **hints**: `pinned` (never evict), `prefetch-with` (co-request a group), uncompressed in-memory size, and a coarse-first ordering flag. Hints are advice; readers ignore unknown hints.

### 14.6 Deterministic compression

Packing from many threads must give identical output (section 15, canonical form): fixed zstd level and parameters, and independent chunks, so the thread count changes nothing. Whether zstd output is byte-identical across zstd versions and threads is **verify**; the format stores the compressor version but makes validity depend on **content hashes, not compressed bytes**.

### 14.7 Derived packs beside a mod's packs

A pack header flag `derived`, plus fields naming the **generation** and the digest of the packs and inputs it was built from. The loader ignores a derived pack whose source digest does not match the current inputs (stale data is never shown, world-lod doc 4.6). Derived packs live in the cache folder, never inside an author's pack; the baker may write them ahead of time or in the background.

### 14.8 Safe update while the game runs

Packs are immutable and content-named: a tool only ever adds files with new names, never edits or replaces one in use. A generation is switched by a tiny pointer file replaced by atomic rename and read only when the game starts. Files are not memory-mapped in the first version, and on Windows the game opens pack files with delete sharing so a tool may remove obsolete ones (a removal checks an `in-use` marker that expires if its process died; world-lod doc 4.7, **verify** the marker approach). Hot-swapping a pack in a running game is not a goal (hot reload is "Not yet" in the root `CLAUDE.md`). Replacing a pack while the game is closed and while it is running is tested on each platform (section 18).

## 15. The open standard

### Name and identification

The format is the Eden Archive Format. The obvious abbreviation is EAF, but `.eaf` is the extension of ELAN Annotation Format files, the linguistics annotation tool's XML format (ELAN manual page of the Max Planck Institute for Psycholinguistics, https://mpi.nl/corpus/html/elan/ch01s02s05.html; file-extension sites also list unrelated uses, https://file.org/extension/eaf). **Decided** (toddhow, 2026-11-01): `.eaf` is **not** used. The extension on disk is provisionally `.edenpak`; one web search on 2026-11-01 found no format using `.edenpak` or `.eden`, which is not a clearance. The final extension, the short name and the magic bytes (at least 6 ASCII characters plus a version byte) are fixed by task 0051 after a collision check from sources (file-extension databases, a search of public code for the magic bytes, `shared-mime-info` if reachable), and recorded in one place in the spec: format name, short name, extension, MIME type if registered, and magic bytes.

### What makes it a standard in practice

- A written specification separate from the implementation, with precise byte layouts, required and optional features, and explicit error behavior. A second team must be able to write a reader from the spec alone.
- A permissively licensed reference reader and writer (Apache-2.0 decided, section 17; MIT is the simpler alternative), so mod managers, converters, and other engines can adopt it. A permissive library can still be linked into the GPL engine (one-way compatibility of Apache-2.0 with GPL-3.0 is **verify**, to be checked in task 0052 before the library is added).
- A conformance suite of valid and invalid packs with expected results, including hostile files (truncated, oversized, path traversal) that must be rejected.
- Versioning: major.minor, where minor versions add things older readers can safely ignore and major versions break compatibility. Define how readers treat unknown flags and codec IDs.
- A public registry of codec IDs, entry type tags, and flags, with an allocation process.
- Reserved header space and an extension mechanism.
- A deterministic canonical form: the same input folder always produces the same pack bytes, so hashes are meaningful and builds are reproducible.

### Governance, kept light

A public repository for the spec with an issue tracker and a documented change process. Decisions in numbered records. A short statement of compatibility promises. A naming and trademark policy so the name cannot be attached to incompatible formats. No standards body is needed.

### Scope

The standard describes the container and its typed layouts. Record schemas, load order, and conflict rules belong to the record files and the engine.

### Tools and bindings

Ship a command-line packer, unpacker, validator, and lister under the permissive license, plus a C reference library with simple, stable bindings. Publish a draft early to get feedback from mod tool authors, and mark early versions as draft until the streaming layouts are proven.

## 16. Security and pitfalls

### Security rules for the reader

- Bounds-check every offset and size. Cap decompressed size and entry count. Reject path traversal. Fuzz the parser.
- Verify hashes before trusting content.
- No executable content and no nested archives.

### Pitfalls

- **File locking:** memory-mapped files can be locked on Windows, which interferes with mod managers updating files while the game runs. Answered by section 14.8: no memory mapping at first, immutable content-named files, delete sharing; test it.
- **Duplicate assets across mods:** dedupe only works if the engine keys on hash, not path.
- **Freezing too early:** do not lock layouts before the streaming prototype proves them.
- **Platform behavior:** verify direct I/O and alignment behavior on each target platform.

## 17. Decisions

The eight decisions the first draft left open were settled by toddhow on 2026-11-01, accepting the recommendations of the review in `docs/design/world-lod-and-streaming.md`, section 12.4 (decision record 0010, item 25). They are to be written into a numbered decision record with the spec task (0051).

| # | Question | Answer | Status |
| --- | --- | --- | --- |
| 1 | Short name and extension | Spec name "Eden Archive Format". `.eaf` is rejected (ELAN's extension, section 15). Provisional extension `.edenpak`; final extension and magic bytes (at least 6 characters plus a version byte) fixed by the collision check in task 0051 | decided, toddhow 2026-11-01 (extension final in 0051) |
| 2 | Packer behaviour | Two modes. **Store** (default for repacking existing mods): entries stored as provided, only chunked and hashed, no re-encoding. **Optimise** (only to write derived packs): may recompress, reorder, convert. Both record the source hash. Original files are never changed or deleted | decided, toddhow 2026-11-01 |
| 3 | DirectStorage and GPU decompression | Plain, portable IO is the baseline. DirectStorage is Windows-only (Microsoft SDK, terms **unknown/verify**) and can only be an optional fast path behind the range-read interface, after measurement. No GPU codec chosen now; the per-chunk codec id leaves the door open | decided, toddhow 2026-11-01 |
| 4 | Texture codec default | Preserve the source block format (BC1, BC3, BC5, BC7) inside KTX2 with no re-encode; BC7 only for textures we author (baked tiles). Basis Universal later, per asset, for portability | decided, toddhow 2026-11-01 |
| 5 | Paths or stable IDs | Paths for author-facing references, the content hash as cache and dedupe key; stable IDs only if a concrete need appears. Tile addressing is separate (keyed index, 14.2) | decided, toddhow 2026-11-01 |
| 6 | Runtime mesh loading | Always convert to an engine-native cached form keyed by source hash; the tile baker produces that form | decided, toddhow 2026-11-01 |
| 7 | Mod shaders | Deferred. Material parameters and approved modules only for now; the existing `.omwfx` mechanism stays; SPIR-V in packs is not needed by the LOD design. Consistent with decision 0009 | decided (deferred), toddhow 2026-11-01 |
| 8 | Permissive licence | Apache-2.0 for the reference library (patent grant); CC BY 4.0 for the specification text. One-way compatibility of Apache-2.0 with GPL-3.0 and the CC BY 4.0 choice are **verify** before the library is added | decided, toddhow 2026-11-01 (verification in task 0052) |

Also decided the same day: the container comes **before** the LOD tile cache (record 0010, item 24); the container reader and writer join the full-loop list of the root `CLAUDE.md` because they handle untrusted input (item 26; the change to the root guide is toddhow's); the archive milestones are renamed A1 to A9 (design doc D14), because the root guide already uses E1 to E6 for Eden Studio phases.

## 18. Testing

- **Round trip:** folder to pack to folder is byte-identical for contents, and packing is deterministic.
- **Conformance corpus:** valid packs, invalid packs, and hostile files with required results.
- **Fuzzing:** the reader runs under a fuzzing harness in CI.
- **Streaming benchmarks:** read patterns, mip-streaming hitches, and VRAM behavior on fixed camera paths in dense cells.
- **Virtual file system:** priority, override, conflict reports, identical-hash dedupe, and path normalization across platforms.
- **Cache:** invalidation by source hash and tool version, plus rebuild from empty.
- **Keyed entries and verification:** lookup by 64-bit key, Morton ordering, per-chunk verification with a damaged chunk, derived packs with a mismatching source digest ignored.
- **Determinism across threads:** the same input packed with 1 and with 4 threads gives identical bytes.
- **Importers:** DDS to KTX2 and NIF to glTF conversions report what was dropped and are checked against reference assets (not redistributed).
- **Locking and updates:** replacing a pack while the game is closed and while it is running, on each platform.

## 19. Milestones

Renamed A1 to A9 (they were E1 to E9, which collided with Eden Studio's E1 to E6 in the root `CLAUDE.md`). Each milestone has a spec in `docs/tasks`, tests written from the spec, and a changelog row. The full workflow loop (spec, independent test-writer, implementer who may not edit tests, fresh reviewer) applies to the container reader, the virtual file system, the cache, and anything handling untrusted input. **Order (decided): A1 to A3 first, then the LOD stages S1 onward of the world-lod doc**, which are built directly on the container; the launcher's built-in mod organizer follows A3.

### A1. Virtual file system and legacy reading (task 0050)

- [ ] Loose files and legacy archive reading through one virtual file system
- [ ] Path normalization in one place, with cross-platform tests
- [ ] Conflict reporting
- [ ] A range-read interface beside `istream`

### A2. Spec v0.1 draft (task 0051)

- [ ] Container only: header, index, keyed index, chunk table, hashing, canonical form
- [ ] Registry for codec IDs, entry types, and flags
- [ ] Name and extension collision check done (decision 1)

### A3. Reference library (task 0052)

- [ ] Reader, writer, and validator under the permissive license
- [ ] Deterministic packer and unpacker with round-trip tests
- [ ] First conformance corpus and a fuzzing harness
- [ ] Plugged into the VFS of A1

### A4. Texture layout

- [ ] KTX2 payload with mip-aligned chunks and tail mips first
- [ ] DDS reader and converter
- [ ] Proven against the streaming prototype (S6 of the world-lod doc: mip streaming with a VRAM budget)
- [ ] Decisions 3 and 4 confirmed in practice

### A5. Derived data cache

- [ ] Cache keyed by source hash plus tool version, held in derived packs
- [ ] Rebuild-from-empty and invalidation tests

### A6. Mesh pipeline

- [ ] `.glb` loading and conversion to the engine-native cached form
- [ ] Custom `EDEN_*` glTF extensions drafted as a public spec
- [ ] NIF importer reporting dropped features (decision 6)

### A7. Cluster mesh layout

- [ ] Typed layout defined from the virtualized-geometry requirements
- [ ] Streaming of cluster hierarchy parts

### A8. Shaders

- [ ] Slang toolchain per decision 0009, with offline SPIR-V compilation and a pipeline cache
- [ ] Shader blobs as typed pack entries, if decision 7 is revisited

### A9. Standard 1.0

- [ ] At least two independent readers pass the conformance suite, or the format has survived real use
- [ ] Compatibility promises published

## 20. Guardrails and changelog

### Guardrails

- Never invent a format or library fact. Cite a source or ask.
- No game assets in the repository or in test corpora. Reference assets are identified by hash and kept outside the repository.
- Do not invent a compression codec or a hash function.
- No encryption or DRM in the container.
- Packs are untrusted input at every layer.
- Anything marked **verify** is checked against a source before it is relied on.

### Changelog

| Date | Change |
| --- | --- |
| 2026-11-01 | Initial draft: separate asset packs, Eden Archive Format container, KTX2 textures, glTF meshes, Slang shaders, derived data cache, open standard plan |
| 2026-11-01 | toddhow's decisions applied (record 0010, items 24 to 26 and 29): the eight open decisions settled (section 17); container first, before the LOD tile cache; milestones renamed A1 to A9 with A1 to A3 as tasks 0050 to 0052; terminology fixed (record file, extension, plugin); Body System and UI System references and the non-existent manifest and plugin format removed; the glTF animation claim corrected; Slang pointed at decision 0009; new section 14 listing what streaming needs (keyed and tile entries, mip tail, per-chunk verification, provenance and hints, derived packs, safe updates); `.eaf` rejected after the collision check; zstd, BLAKE3 and KTX-Software licence checks recorded (KTX-Software bundles one non-open-source file); the baker may bake ahead of time or in the background; Fallout: New Vegas named as the later target |
