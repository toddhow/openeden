# Task: Eden Archive Format specification v0.1 (archive milestone A2)
Design doc section: `docs/design/eden-archive-format.md`, sections 3 (principles), 6 (container layout), 7 (compression and hashing), 14 (what streaming needs from the format), 15 (the open standard), 16 (security), 17 (decisions) and 19 (milestone A2; it was called E2 before the rename); `docs/design/world-lod-and-streaming.md`, sections 3.2 (tile keys), 4.5 to 4.7 (cache keys, generations, safe updates) and 12.3 (what the format must offer); decision record `docs/decisions/0010-rendering-and-studio-answers-2026-11-01.md`, items 24 to 26.
Goal: write the Eden Archive Format container as a standalone, precise specification that a second team can implement a reader from alone, with byte layouts, a registry of codec ids, entry types and flags, the name, extension and magic-byte collision check done from sources, the canonical deterministic form, limits and versioning, using the codecs the accepted answers chose.
Inherited code touched: none. This task writes documents (and the decision record that the archive doc section 17 promises). It reads, to stay consistent with the engine, `components/bsa/*` (what the legacy formats store, for the comparison table only) and `components/vfs/*` (task 0050's interfaces), at commit `522b1a7951`.
Inputs / outputs: a specification document (proposed home `docs/spec/eden-archive-format-0.1.md`; a new folder, because the spec is meant to be copied out to a public repository one day; toddhow confirms the location), a registry document or section (`docs/spec/registry.md` or a section of the spec), a decision record (`docs/decisions/0011-eden-archive-format.md`, number to be confirmed, since 0011 is the next free at this commit), hand-written valid and invalid example byte listings small enough to include in the spec; no code.

Status: draft, written 2026-11-01. Docs only, but it fixes the byte layout every later stage depends on, so it gets the **full loop** (an independent reader of the draft, then a fresh reviewer; the conformance corpus of task 0052 is written from this document alone). Depends on 0050 (the VFS interfaces it must fit) and precedes 0052. The streaming typed layouts (tile, cluster and impostor payloads) are **not** frozen here: only their type tags and the per-entry fields are reserved (archive doc 14.2).

## Design, in plain language

A pack is one file holding many assets. The specification says exactly which bytes come where, so anyone can write a reader. The shape follows the accepted design: a header that says what the file is, an index that says where everything is, and aligned blocks of data that can each be read and checked on their own.

What the specification must define (each item is a section of the document; the values in this spec are proposals marked *proposal* until the stage that settles them):

1. **Identification.** Format name (Eden Archive Format), short name, file extension, MIME type if registered, magic bytes. Decided already (archive doc 15 and 17): `.eaf` is not used (it is ELAN's annotation extension, https://mpi.nl/corpus/html/elan/ch01s02s05.html); `.edenpak` is provisional; magic of at least 6 ASCII characters plus a version byte.
2. **Encoding rules.** Little-endian; all offsets and sizes 64-bit; every structure either fixed-size with named fields or length-prefixed; reserved fields must be zero when written and ignored or rejected as stated when read; all text is UTF-8 and must be valid.
3. **Header and footer.** Magic, version (major, minor), pack flags (including `derived`, archive doc 14.7), counts, index offsets and sizes, a checksum of the header, a footer that repeats the essentials so a reader can find the index from the end of the file (*proposal*), and a block of reserved space.
4. **Index, two forms.** (a) The **path index**: entries sorted by normalized path; the path is stored as a hash plus an offset into a string table; the real name is kept in the string table; names are stored already normalized in the engine's sense (task 0050: `\` to `/`, ASCII lower-case) and must be valid UTF-8 with no `.`/`..` components, no leading or trailing slash, and no NUL. (b) The **key index** for tiles and similar data: sorted 64-bit keys (type tag + Morton-coded tile key), no strings, binary-searchable. Both forms point to the same kind of entry record.
5. **Entry record.** Entry type, flags, uncompressed size, content hash, offset of the chunk table, provenance record for derived entries (digest of the source hashes, tool id and version, parameters; archive doc 14.5), residency hints (`pinned`, `prefetch-with` group, in-memory size, coarse-first; unknown hints ignored).
6. **Chunk table.** Per chunk: offset, stored size, uncompressed size, codec id, per-chunk hash (or the tree-hash scheme; see decision S2). Chunks are independent so any chunk can be read, decompressed and verified alone.
7. **Data blocks.** Aligned to 4 KiB (archive doc 6); padding bytes are zero; chunk size range (*proposal*: 64 to 256 KiB, the final default set by the measurement of stage g); the **mip tail layout** for texture entries (archive doc 14.3): tail chunk first and contiguous, one chunk per larger mip, KTX2 level offsets kept valid if the KTX2 specification allows (to be checked from the specification itself: **verify**).
8. **Registry.** A table of: codec ids (*proposal*: 0 = stored, 1 = zstd; others reserved, with a range kept for experiments that must not appear in released packs), entry type tags (*proposal*: generic, texture, mesh, audio, shader, then the reserved tile types: terrain tile, object cluster, impostor atlas, grass coverage), pack flags, entry flags, hint ids; each with an allocation rule (a numbered change to the registry document), a status (stable, draft, reserved) and the text of what a reader does when it meets an **unknown** id (reject the entry, skip the entry, or ignore: stated per table).
9. **Hashing and compression.** BLAKE3 for content hashes (CC0, Apache-2.0 or Apache-2.0 with LLVM exceptions per the project's README, checked 2026-11-01); zstd for the compressed chunks, BSD option of its dual licence (checked 2026-11-01); hash size (*proposal*: 32 bytes); the per-chunk verification scheme (S2); compressed data stored raw when compression gains too little (codec 0). KTX-Software is **not** a dependency of the container (its repository bundles a file that is not open source; archive doc 8): the container stores KTX2 bytes, it does not parse them.
10. **Canonical form.** The single byte-exact output for a given input list: entries sorted by a stated rule (normalized path bytes, then key), chunk boundaries a pure function of the input and the chosen chunk size, fixed zstd level and parameters, no timestamps, no host paths, zero padding, no dependence on thread count or on the order inputs were supplied. The format stores the compressor version but validity depends on **content hashes, not compressed bytes** (archive doc 14.6), because compressed bytes may differ between zstd versions (**verify**).
11. **Limits.** Maximum path length, entry count, chunk size, uncompressed size per entry and per pack, chunk count per entry, string table size, key-index size, nesting depth (none: **no nested packs**), each with a number (*proposals*, to be set in stage f from the realistic sizes of a large Morrowind and New Vegas install, which are **unknown** until measured) and the exact behaviour on exceeding it (reject the pack).
12. **Security rules for readers** (archive doc 16, as normative text): bounds-check every offset and size, check arithmetic for overflow, cap allocation by the stated limits, reject path traversal and non-normalized names, reject overlapping or out-of-file chunk ranges, reject duplicate paths, verify hashes before trusting content, no executable content, no encryption, no DRM.
13. **Versioning.** Major.minor. A minor version adds only things an older reader can ignore (new optional hints, new registry entries marked skippable); a major version may break. How a reader treats a higher minor, a higher major, unknown flags and unknown codec ids. Draft versions (0.x) make no compatibility promise.
14. **Update and derived-pack conventions.** The immutable, content-named files and generation pointer of archive doc 14.8, written as conventions for tools (not as part of the pack bytes).
15. **A comparison table** (informative) against BSA and BA2 as the repository reads them, for people porting tools.
16. **Worked examples.** A tiny valid pack (one stored entry, one zstd entry, one keyed entry) listed byte by byte, and a list of invalid packs each with the required rejection, small enough to be reproduced by hand; task 0052 turns them into the first conformance corpus.

### The collision check (name, extension, magic)

> **Decided 2026-11-01 (toddhow, decision 0010): the extension is `.edenpak`.** The check below now confirms it and records the sources instead of choosing one; if it finds a real clash, stop and ask.

Done from sources and recorded with URLs and the date in the decision record:

- **Extension.** Search the extension databases (file.org, fileinfo.com, file-extensions.org, the Library of Congress digital formats and the UK National Archives PRONOM registry) for the shortlist: `.edenpak`, `.eden`, `.eap`, `.edenarc`. Already known: `.eaf` is ELAN's (rejected). A web search on 2026-11-01 found no use of `.edenpak` or `.eden`, which is not a clearance.
- **Magic bytes.** Search public code (GitHub code search for the candidate byte strings, the `file` command's magic database, `shared-mime-info`'s `freedesktop.org.xml`, Wikipedia's list of file signatures) for each candidate magic. Pick one with no hit.
- **Name.** "Eden Archive Format" and "Eden": a web search for products using the name in the archive/format space; trademark advice is not available, so the naming policy of archive doc 15 (governance) is recorded instead.
- **MIME type.** Check the IANA media types registry for anything near `application/x-eden-archive`; register nothing now.

### Decisions toddhow can change (safe choice first)

| # | Decision | Chosen | Alternative |
| --- | --- | --- | --- |
| S1 | Where the spec lives | `docs/spec/` (new folder), to be copied to a public repository | `docs/design/` beside the archive doc |
| S2 | Per-chunk verification | Store a BLAKE3 hash per chunk in the chunk table; whole-entry hash = hash over the chunk hashes (simple, easy to implement twice) | Use BLAKE3's own tree and outboard encoding (Bao-like; smaller index, harder for second implementers; the README says BLAKE3 is a Merkle tree capable of verified streaming) |
| S3 | Hash size | 32 bytes | Truncated 16 |
| S4 | Index search | Sorted arrays, binary search | Perfect hashing (later, a minor-version addition) |
| S5 | Alignment | 4 KiB for data blocks; index structures 8-byte aligned | Larger (64 KiB) |
| S6 | Footer | Yes, repeating the header essentials | Header only |
| S7 | Extension and magic | Fixed by the collision check of stage b | |
| S8 | Licence of the text | CC BY 4.0 (accepted, archive doc 17 decision 8; its use in a standard is **verify**) | |

## Stages

Each stage commits a reviewed document.

- **a. Skeleton and vocabulary.** The section list above, with every term defined in plain language, the `must`/`should`/`may` convention stated, and the open items listed.
- **b. Collision check and identification.** Run the check above, record sources, choose extension, short name and magic; write decision record (naming).
- **c. Header, footer, encoding rules.** Byte tables with offsets and field types, alignment, reserved-field behaviour, header checksum.
- **d. Index, entry record, chunk table.** Both index forms, sort rules, the keyed index, the provenance record and hints.
- **e. Registry and codecs.** The registry document, allocation rule, unknown-id behaviour, the stored and zstd codecs, per-chunk verification (S2) and the texture mip-tail layout (reading the KTX2 specification to check level order and alignment first).
- **f. Limits, canonical form, security rules, versioning.** Numbers for the limits (a measurement note records the data they come from); the canonical-form algorithm in steps a second implementer can follow; the reader's rejection list.
- **g. Examples and chunk-size measurement.** The worked valid and invalid packs; a measured recommendation for the default chunk size on generated data (no game assets), reported without being a requirement.
- **h. Independent read-through.** A reviewer who has not seen the engine code writes pseudocode for a reader from the document alone; every ambiguity found is fixed. The decision record for the whole format (and the answers of archive doc 17) is written.

## Constraints

- Never invent a format, library or engine fact: cite a source (a specification page, a repository README) or mark **verify**. Licence checks are recorded with the URL and date. No dependency is added by this task.
- No game assets: the examples are hand-made bytes.
- Everything that can be ambiguous is stated as a `must`, a `should` or a `may`; anything left to the implementation is said to be.
- Layouts for tile, cluster and impostor payloads are not frozen (archive doc 16, "freezing too early"); the version stays 0.x until the streaming prototype (S6 of the world-lod doc) has used the format.
- The text is licensed per decision 8 (CC BY 4.0, to be verified before publication).
- Terminology: extension, plugin and record file as in the root `CLAUDE.md`.

## Acceptance criteria

- [ ] The specification covers items 1 to 16 above; a reader can be written from it without looking at any engine code (shown by stage h).
- [ ] Every byte layout has a table with offsets, sizes, types and the required value or range; every length, offset and count has a stated limit and a stated rejection behaviour.
- [ ] The registry lists codec ids, entry types, pack and entry flags and hints, with allocation rule and unknown-id behaviour for each table.
- [ ] The collision check for extension, magic bytes, name and MIME type is recorded with sources, dates and the decision; `.eaf` is documented as rejected with the reason.
- [ ] The canonical form is a step-by-step algorithm with no host-dependent input; two implementations following it would produce identical bytes (confirmed later by task 0052's tests).
- [ ] At least one valid and ten invalid example packs are listed byte by byte, each invalid one with its required rejection.
- [ ] Per-chunk verification, derived-pack fields, keyed index, tile entry type tags, mip tail layout and the update conventions of the archive doc, section 14, are all specified or explicitly deferred with a reason.
- [ ] Facts about zstd, BLAKE3 and KTX2 that are not yet checked are marked **verify**; no unchecked fact is stated as settled.
- [ ] The decision record exists and the archive doc's section 17 points to it; the archive doc and this spec agree.
- [ ] A fresh reviewer read each stage.
Out of scope: code (task 0052), the VFS (0050), typed payload layouts for tiles, clusters, impostors and grass (the LOD stage specs), texture transcoding and the DDS to KTX2 converter (archive A4), glTF and mesh pipelines, shader packs, encryption or DRM, nested archives, a public repository, a standards body, compatibility promises (those wait for version 1.0).
