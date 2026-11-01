# Eden Archive Format and Asset Streaming Guide

Status: draft, 2026-11-01. Living document. Companion to `CLAUDE.md`, the Body System Design Guide, and the UI System Design Guide.

This guide covers how OpenEden stores and streams assets: the separate data files, the Eden Archive Format (the pack container), KTX2 textures, how glTF meshes and Slang shaders fit in, the derived-data cache, and the plan to make the container an open standard. It is a design document. Nothing here is built yet. Every format, license, or library fact that has not been checked against a source is marked **verify**.

## How to use this document

- Update it in the same change as any decision, milestone result, or scope change, and add a row to the changelog (section 20).
- Tick milestone checkboxes only when the gate in that milestone passes.
- Settled choices move into numbered decision records under `docs/decisions/`. Section 17 lists what is still open.
- Never state a format or library fact as settled without a source. Mark it **verify** until checked.

## 1. Goals

- Stream textures, meshes, and other assets on demand so high-resolution modded content does not hit a hard VRAM wall.
- Keep records (small, merged) and assets (large, streamed) in separate files built for their different access patterns.
- Use open formats throughout: glTF 2.0 for meshes and animation, KTX2 for textures, Slang for shaders, and an open archive format for packaging.
- Keep existing content usable through read-only importers and loose-file support.
- Make the archive container an open standard that mod tools and other engines can implement.

## 2. Decisions so far

| Decision | Status |
| --- | --- |
| Assets live in separate data files, not inside the plugin (record) files | Direction chosen |
| Create a new, minimal archive container instead of reusing BSA, BA2, or ZIP at runtime | Direction chosen |
| Name: Eden Archive Format (short name and extension provisional, see section 15) | Direction chosen, collision check pending |
| The container is an open standard with a permissive reference library | Direction chosen |
| Textures use KTX2 as the payload format, with DDS read for legacy content | Direction chosen |
| Meshes and animation use glTF 2.0 (animation already implemented) | Direction chosen |
| Shaders use Slang | Direction chosen |
| Derived data lives in a separate cache, not inside packs | Direction chosen |

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
| Data plugin | Manifest, records, patches (the plugin format) |
| Asset pack | Meshes, textures, audio, shader blobs, other binary data |
| Scripts | Lua scripts, declared in the manifest with their permissions |

The manifest declares which packs and scripts belong to a mod. Loose files in a folder remain supported for development and for existing mods.

### How records refer to assets

Records reference assets by a namespaced path such as `mymod:meshes/sword.glb`, and the virtual file system resolves the path. See section 13 for the open decision on paths versus stable asset IDs.

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

- **Compression:** zstd in independent chunks, with the chunk size tuned by measurement (a starting range of 64 to 256 KiB). **Verify** the zstd license and whether to use its seekable-format framing or the pack's own chunk table.
- **Already-compressed data** (block-compressed textures, audio codecs) is stored uncompressed or lightly compressed, since recompression gains little.
- **Hashing:** BLAKE3 for content hashes. **Verify** its license and the library to use. Do not design a custom hash or codec.
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
- **Tools:** the reference tools (KTX-Software, including `toktx`) are believed to be Apache 2.0. **Verify** the license before depending on them.
- **Partial installs:** a mod can ship a low-resolution texture pack and an optional high-resolution pack as a separate dependency.

### Platform note

If Windows DirectStorage with GPU decompression is a target, **verify** which compression format it requires (it may have its own GPU-friendly codec), because that could change the texture codec. The per-chunk codec ID allows adding it later for those entries, but decide before the first packs ship. See decision 3 in section 17.

## 9. Meshes and animation: glTF

glTF 2.0 is the open alternative to NIF. The animation system already uses it. Core coverage: node hierarchy, meshes with multiple UV sets and vertex colors, skinning, morph targets (needed by the body system), cameras, lights, PBR materials, and animation. The binary form (`.glb`) packs a model into one file.

### Extensions to evaluate (verify status of each)

| Need | Extension |
| --- | --- |
| KTX2 textures | `KHR_texture_basisu` |
| Compressed geometry | `EXT_meshopt_compression`, `KHR_mesh_quantization` |
| Mesh LODs | `MSFT_lod` |
| Instancing | `EXT_mesh_gpu_instancing` |
| Animating arbitrary properties | `KHR_animation_pointer` |
| Material variety | the `KHR_materials_*` family |
| Physics | `KHR_physics_rigid_bodies` (check whether ratified or still a draft) |

### What glTF lacks and NIF has

Particle systems, text keys and animation events (footsteps, hit frames), collision for older games, per-object game flags, and legacy shader properties. Define namespaced custom extensions (`EDEN_*`) for these as a published spec. The body system's region maps and soft-tissue profiles are further candidates. Evaluate the Khronos physics extension before inventing a collision extension, since collision maps onto Jolt shapes.

### Runtime use

glTF is excellent as an authoring and interchange format but is not laid out for a streaming or cluster-based renderer. Load `.glb` and convert to an engine-native cached form (meshlets, LOD chains, quantized streams) keyed by source hash. The `.glb` stays the source of truth. The importer from NIF is lossy: it carries meshes, skeletons, weights, materials, and basic animation, and must report what it dropped.

### Legacy materials

glTF's native model is metallic-roughness PBR. Preserve original legacy parameters (ambient, diffuse, specular) in an extension and convert at load, so nothing is lost.

### Typed layouts in the pack

Meshes start as plain chunked blobs. Add a typed layout for cluster hierarchies only when the virtualized-geometry work defines what it needs.

## 10. Shaders: Slang

Slang is an open-source shading language, believed to be hosted under Khronos. **Verify** its current hosting, license, and platform coverage.

- It compiles to SPIR-V for Vulkan and can target HLSL, GLSL, Metal, and WGSL.
- Modules, interfaces, and generics help manage the permutation explosion from many material types and skins.
- A reflection API lets the engine discover bindings at runtime.
- One language covers compute passes (culling, skinning, morphing, soft-body) and graphics.

Plan for:

- Compile offline where possible, ship SPIR-V with a pipeline cache, and compile at runtime only for mod shaders or development mode.
- Decide whether mods may ship shaders at all. Material parameters and pre-approved modules are safer than arbitrary shader code. If shaders are allowed, validate them and cap resource use.
- Migration or compatibility for OpenMW's existing GLSL shader features. **Verify** what the current shader API exposes to mods.
- Pin toolchain versions, since the language is changing quickly.

Compiled shader blobs are typed entries in packs.

## 11. Derived data cache

Several designs generate data from source assets: converted animations, derived body proxies, meshlet hierarchies, transcoded textures. Keep all of it in a separate on-disk cache keyed by source hash plus tool version.

- Packs stay exactly as authors shipped them. The cache can be deleted and rebuilt at any time.
- Authors may optionally ship pre-baked derived data in a pack for faster first load.
- Cache invalidation is by key only. Define one clear rule and test it.
- Cache size limits and cleanup are part of the settings.

## 12. Virtual file system, priority, and conflicts

- One virtual file system resolves a path to a file across loose folders, packs, and legacy archives.
- Pack priority and loose-file overrides follow the same rules as the record layer, with the user layer winning.
- If two mods supply different content for the same path, write a conflict report. If they supply identical content (same hash), there is no conflict, which improves on archives where duplicates silently shadow each other.
- Key the engine on content hash where possible so identical assets are shared in the cache and in memory.
- Normalize paths in one place only: case, Unicode normalization, and separators differ across platforms. Test it.

### Legacy support

Read BSA and BA2 archives and OpenMW's existing data arrangement through the same virtual file system, so legacy mods work without conversion. Provide a packer that turns a loose folder into a pack, and an unpacker, with round-trip tests.

## 13. Asset references: paths or IDs

Paths are simpler and keep legacy compatibility trivial. Stable asset IDs, mapped to paths by the manifest, make renames and replacements safer and allow content-addressed dedupe, but add indirection for every authoring tool. This is open decision 5 in section 17.

## 14. Streaming requirements on the format

These requirements come from the streaming plan and must be checked against what streaming actually needs.

- Index up front (or a fixed-size footer) so a pack never needs scanning.
- Aligned, independently decompressible chunks, so partial reads work.
- Mip-aware texture layout, tail mips first and contiguous.
- Mesh and LOD data addressable by level or cluster once the virtualized-geometry work defines it.
- Memory-mapped or direct asynchronous reads, with a dedicated transfer path and a staging ring buffer on the GPU side.
- VRAM budgets driven from memory-budget queries, with LRU-style eviction.
- A prefetcher that uses player velocity and cell adjacency.

## 15. The open standard

### Name and identification

The format is the Eden Archive Format. The obvious abbreviation is EAF, but `.eaf` may already be used by a linguistics annotation tool and the acronym has other meanings. **Verify** with file-extension databases and a code search before committing. Options include a distinctive extension such as `.eden` or `.edenpak`, or keeping EAF as the spec name and using a different extension on disk. Choose distinctive magic bytes (4 to 8 bytes, for example ASCII text plus a version byte) and record all identifiers in one place in the spec: format name, short name, extension, MIME type if registered, and magic bytes.

### What makes it a standard in practice

- A written specification separate from the implementation, with precise byte layouts, required and optional features, and explicit error behavior. A second team must be able to write a reader from the spec alone.
- A permissively licensed reference reader and writer (MIT or Apache 2.0 are common, **verify** which fits), so mod managers, converters, and other engines can adopt it. A permissive library can still be linked into the GPL engine.
- A conformance suite of valid and invalid packs with expected results, including hostile files (truncated, oversized, path traversal) that must be rejected.
- Versioning: major.minor, where minor versions add things older readers can safely ignore and major versions break compatibility. Define how readers treat unknown flags and codec IDs.
- A public registry of codec IDs, entry type tags, and flags, with an allocation process.
- Reserved header space and an extension mechanism.
- A deterministic canonical form: the same input folder always produces the same pack bytes, so hashes are meaningful and builds are reproducible.

### Governance, kept light

A public repository for the spec with an issue tracker and a documented change process. Decisions in numbered records. A short statement of compatibility promises. A naming and trademark policy so the name cannot be attached to incompatible formats. No standards body is needed.

### Scope

The standard describes the container and its typed layouts. Record schemas, load order, and conflict rules belong to the plugin format and the engine.

### Tools and bindings

Ship a command-line packer, unpacker, validator, and lister under the permissive license, plus a C reference library with simple, stable bindings. Publish a draft early to get feedback from mod tool authors, and mark early versions as draft until the streaming layouts are proven.

## 16. Security and pitfalls

### Security rules for the reader

- Bounds-check every offset and size. Cap decompressed size and entry count. Reject path traversal. Fuzz the parser.
- Verify hashes before trusting content.
- No executable content and no nested archives.

### Pitfalls

- **File locking:** memory-mapped files can be locked on Windows, which interferes with mod managers updating files while the game runs. Decide the mapping lifetime and test it.
- **Duplicate assets across mods:** dedupe only works if the engine keys on hash, not path.
- **Freezing too early:** do not lock layouts before the streaming prototype proves them.
- **Platform behavior:** verify direct I/O and alignment behavior on each target platform.

## 17. Open decisions

Each is settled in a numbered decision record before the related milestone starts.

1. **Short name and extension:** resolve the EAF collision check (section 15).
2. **Packer behavior:** may the packer rewrite entries (recompress, reorder, convert textures to a preferred format), or only store what the author provided? Rewriting gives better streaming layouts and lets the engine standardize on a few formats, but the pack then differs from the source, so hashes and what the author shipped need clear rules. Storing as-is is simpler and more transparent, but streaming quality then depends on how the author prepared files.
3. **DirectStorage and GPU decompression:** target it on Windows, or keep I/O plain and portable. This decides the codec question more than any other factor.
4. **Texture codec default:** BC7 direct or Basis Universal, and per-asset override rules.
5. **Asset references:** paths versus stable asset IDs.
6. **Runtime mesh loading:** load `.glb` directly with a fast path, or always convert to an engine-native cache first (leaning toward the cache).
7. **Mod shaders:** whether mods may ship shaders, or only material parameters and approved modules.
8. **Permissive license choice** for the reference library and the spec's documentation license.

## 18. Testing

- **Round trip:** folder to pack to folder is byte-identical for contents, and packing is deterministic.
- **Conformance corpus:** valid packs, invalid packs, and hostile files with required results.
- **Fuzzing:** the reader runs under a fuzzing harness in CI.
- **Streaming benchmarks:** read patterns, mip-streaming hitches, and VRAM behavior on fixed camera paths in dense cells.
- **Virtual file system:** priority, override, conflict reports, identical-hash dedupe, and path normalization across platforms.
- **Cache:** invalidation by source hash and tool version, plus rebuild from empty.
- **Importers:** DDS to KTX2 and NIF to glTF conversions report what was dropped and are checked against reference assets (not redistributed).
- **Locking and updates:** replacing a pack while the game is closed and while it is running, on each platform.

## 19. Milestones

Each milestone has a spec in `docs/tasks`, tests written from the spec, and a changelog row. The full workflow loop (spec, independent test-writer, implementer who may not edit tests, fresh reviewer) applies to the container reader, the virtual file system, the cache, and anything handling untrusted input.

### E1. Virtual file system and legacy reading

- [ ] Loose files and legacy archive reading through one virtual file system
- [ ] Path normalization in one place, with cross-platform tests
- [ ] Conflict reporting

### E2. Spec v0.1 draft

- [ ] Container only: header, index, chunk table, hashing, canonical form
- [ ] Registry for codec IDs, entry types, and flags
- [ ] Name and extension collision check done (decision 1)

### E3. Reference library

- [ ] Reader, writer, and validator under the permissive license
- [ ] Deterministic packer and unpacker with round-trip tests
- [ ] First conformance corpus and a fuzzing harness

### E4. Texture layout

- [ ] KTX2 payload with mip-aligned chunks and tail mips first
- [ ] DDS reader and converter
- [ ] Proven against the streaming prototype (mip streaming with a VRAM budget)
- [ ] Decisions 3 and 4 recorded

### E5. Derived data cache

- [ ] Cache keyed by source hash plus tool version
- [ ] Rebuild-from-empty and invalidation tests

### E6. Mesh pipeline

- [ ] `.glb` loading and conversion to the engine-native cached form
- [ ] Custom `EDEN_*` extensions drafted as a public spec
- [ ] NIF importer reporting dropped features (decision 6)

### E7. Cluster mesh layout

- [ ] Typed layout defined from the virtualized-geometry requirements
- [ ] Streaming of cluster hierarchy parts

### E8. Shaders

- [ ] Slang toolchain pinned, with offline SPIR-V compilation and a pipeline cache
- [ ] Shader blobs as typed pack entries
- [ ] Decision 7 recorded

### E9. Standard 1.0

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
