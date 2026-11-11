# Eden Archive Format: container specification, version 0.1 (draft)

Status: **draft 2 of version 0.1**: written 2026-11-01 (task 0051, archive milestone A2) and revised the same day after an independent review that read only this text (task 0052, step 0; changelog at the end). Version 0.x makes **no compatibility promise** (section 16). The reference library and the conformance corpus are task 0052.

Licence of this text: CC BY 4.0 (decision 8 of `docs/design/eden-archive-format.md` section 17; that this licence suits a standard text is **verify** before publication).

Design background (not needed to implement a reader): `docs/design/eden-archive-format.md` and `docs/design/world-lod-and-streaming.md`. Everything a reader or writer must do is in this document. Where this text says "the engine", it is only an example of a user of the format.

How to read the status marks in this document: **proposal** is a value chosen for 0.1 that may change before 1.0; **verify** is a fact about something outside this document that has not been confirmed from the project's own text (each carries the reason); a section or table marked *informative* has no requirements in it.

Words: in this document "extension" means only a **file-name suffix** (such as `.edenpak`). Nothing here refers to OpenEden's native-code extensions.

## Contents

1. Scope and conventions
2. Identification
3. Encoding rules
4. File layout
5. Header
6. Footer
7. Metadata region: string table, entry table, indexes, chunk tables, provenance, extension blocks
8. Data blocks and chunking
9. Codecs
10. Entry types and typed layouts
11. Registries
12. Limits
13. Hashing and integrity
14. Reader requirements
15. Security considerations
16. Versioning and extension
17. Canonical form and determinism
18. Update and derived-pack conventions (informative)
19. Comparison with BSA and BA2 (informative)
- Annex A: Identifier collision check
- Annex B: A worked valid pack
- Annex C: Invalid packs
- Annex D: Conformance corpus requirements
- Annex E: Decisions, deviations and open points

## 1. Scope and conventions

The Eden Archive Format (EAF in prose, `.edenpak` on disk; section 2) is a container for many files ("entries"). It defines how entries are named or keyed, split into independently readable and verifiable chunks, optionally compressed, hashed and indexed. It defines nothing about what the bytes mean, except the small typed-layout annexes of section 10. It excludes executable content, nested packs, encryption and DRM.

**Requirement words.** MUST, MUST NOT, SHOULD, SHOULD NOT and MAY are used as in RFC 2119 and RFC 8174 (https://www.rfc-editor.org/rfc/rfc2119, https://www.rfc-editor.org/rfc/rfc8174): MUST/MUST NOT are absolute, SHOULD/SHOULD NOT may be departed from for a stated reason, MAY is optional. Text in lower case is explanation.

**Parties.** A *writer* produces a pack. A *reader* opens and reads one. A *validator* is a reader that also checks the canonical form (section 17). A pack is **untrusted input** to every reader (section 15).

**Terms.**

| Term | Meaning |
| --- | --- |
| pack | one file in this format |
| entry | one stored file: addressed by a path, a key, or both |
| path | the entry's normalized name (section 7.3.1) |
| key | a 64-bit number addressing an entry without a string (section 7.5) |
| chunk | an independently stored, compressed and verified piece of an entry's content |
| content | the entry's bytes before compression (the "uncompressed" bytes) |
| stored bytes | the bytes of a chunk as they lie in the pack |
| region | a contiguous byte range of the pack with one purpose (header, data, metadata and its sub-regions, footer) |
| BLAKE3-256 | the BLAKE3 hash function with its default 256-bit (32-byte) output (section 13) |

## 2. Identification

| Item | Value | Status |
| --- | --- | --- |
| Format name | Eden Archive Format | decided (toddhow, 2026-11-01) |
| Public name | **Eden Archive** (decided by toddhow, 2026-11-01: "Eden Archive will be the public text"); the file extension `.edenpak` and the magic bytes below are unchanged and are not user-facing text; "EdenPak" is not used as a name | decided |
| File extension | `.edenpak` (lower case; the dot is not part of it) | decided (toddhow, 2026-11-01), collision check in Annex A |
| Rejected extension | `.eaf`: it is ELAN Annotation Format's extension (https://mpi.nl/corpus/html/elan/ch01s02s05.html), so it is not used | decided |
| Magic (file start) | the 8 bytes `45 44 45 4E 50 41 4B 01` (ASCII `EDENPAK`, then the byte `01`) | proposal; collision check in Annex A |
| Footer magic (last 128 bytes, section 6) | the 8 bytes `45 44 45 4E 45 4E 44 01` (ASCII `EDENEND`, then `01`) | proposal |
| MIME type | `application/vnd.openeden.edenpak` | **proposal only. Nothing is registered with IANA and this document does not ask for registration.** |
| Uniform type / other registrations | none | |
| Specification version | 0.1 (major 0, minor 1) | |

The eighth magic byte is the **container generation** (`01` for every pack of this layout family). It is not the format version, which is in the header (section 5). A reader that finds the seven bytes `EDENPAK` but another generation byte MUST report "unsupported generation" and not "not a pack". An archive-factory style probe (one that decides by magic whether a file is a pack) SHOULD match on the seven ASCII bytes only, so that a pack of a later generation is reported as unsupported rather than ignored.

A tool that recognises packs SHOULD recognise them by magic and MAY use the extension as a hint. A reader MUST NOT trust the extension.

## 3. Encoding rules

1. **Byte order.** Every multi-byte integer is **little-endian**. No structure uses big-endian values.
2. **Integer types.** `u8`, `u16`, `u32`, `u64` are unsigned; `i32` is a two's-complement signed 32-bit value. All offsets and sizes in structures are `u64` unless a table says otherwise (packs will pass 4 GiB).
3. **Offsets** are absolute: counted in bytes from the first byte of the file, which is offset 0.
4. **Alignment.** Every structure of the metadata region starts at a multiple of **8**. Each entry's data block starts at an absolute file offset that is a multiple of **4096** (section 8). A reader MUST check these alignments as stated in section 14.
5. **Fixed or length-prefixed.** Every structure is either fixed-size with named fields at fixed offsets (tables in sections 5 to 7) or carries its own length.
6. **Padding and reserved fields.** Padding bytes and reserved fields are written as zero. A reader MUST ignore the value of a reserved field or padding byte, except where this document says "MUST be zero". (The header checksum and the metadata digest cover them, so changes are still detected.) A validator in canonical mode (section 17) MUST report non-zero padding and reserved bytes.
7. **Text.** All names are UTF-8 (RFC 3629) and MUST be valid: no overlong forms, no surrogates, nothing above U+10FFFF. Text fields are not NUL-terminated; their length is stored next to them.
8. **Overflow.** Every offset-plus-size test MUST be written so that it cannot overflow, for example `size <= regionEnd - offset` after `offset <= regionEnd` has been checked, in 64-bit unsigned arithmetic. A sum that overflows is an error (`E_RANGE`).
9. **Counts times record sizes** (for example `entryCount * entryRecordSize`) MUST be computed with overflow checking, or after the count has been bounded by the limits of section 12.

## 4. File layout

A pack is five regions, in this order, with nothing else in the file:

```text
offset 0         +------------------------------+
                 | Header           (512 bytes) |  section 5
                 | zero padding to dataOffset   |
dataOffset       +------------------------------+  (4096 in canonical form)
                 | Data blocks                  |  section 8: one block per non-empty entry,
                 |   each starting at a multiple|  each block = that entry's chunks, stored bytes
                 |   of 4096                    |
                 | zero padding to metadataOff. |
metadataOffset   +------------------------------+  (a multiple of 4096 in canonical form)
                 | Metadata region              |  section 7
                 |   string table               |
                 |   entry table                |
                 |   path index                 |
                 |   key index                  |
                 |   chunk tables               |
                 |   provenance records         |
                 |   extension blocks           |
fileSize - 128   +------------------------------+
                 | Footer           (128 bytes) |  section 6
fileSize         +------------------------------+
```

The metadata (the "index") is at the end so that a writer can stream the data first and then write the index it learned; the footer is fixed-size so that a reader finds the index from the end of the file in one read, and the header repeats the essentials so that a reader that starts at the front finds it too. The two MUST agree (section 14).

Memory mapping is permitted (all arrays are 8-byte aligned) but not required, and the first OpenEden reader does not use it (design doc 14.8). Every read in this format is a positional range read.

## 5. Header

The header is **512 bytes** in this version (`headerSize` = 512). It starts at offset 0.

| Offset | Size | Type | Field | Rule |
| --- | --- | --- | --- | --- |
| 0 | 8 | bytes | `magic` | MUST equal `45 44 45 4E 50 41 4B 01` |
| 8 | 2 | u16 | `versionMajor` | 0 for this specification |
| 10 | 2 | u16 | `versionMinor` | 1 for this specification |
| 12 | 4 | u32 | `headerSize` | MUST be 512 in version 0.1 (`E_VERSION` otherwise). From version 1.0 a later minor version may write a larger multiple of 8 (at most 4096) and a reader of that major version ignores the extra bytes (section 16.1); `headerChecksum` is then at `headerSize - 32`. |
| 16 | 8 | u64 | `packFlags` | section 11.4. Bits 0 to 31 are *critical*: a reader MUST reject the pack (`E_UNSUPPORTED`) if it sees a set bit it does not know. Bits 32 to 63 are *ignorable*. |
| 24 | 8 | u64 | `fileSize` | MUST equal the real size of the file |
| 32 | 4 | u32 | `chunkSize` | nominal chunk size in bytes: a power of two from 4096 to 8388608 (section 8.2) |
| 36 | 4 | u32 | `entryRecordSize` | size in bytes of one entry record (section 7.4); MUST be a multiple of 8, at least 112 and at most 512. Readers use it as the stride |
| 40 | 4 | u32 | `chunkRecordSize` | size of one chunk record (section 7.7); a multiple of 8, at least 56, at most 256 |
| 44 | 4 | u32 | `compressorVersion` | informative: the version number of the zstd library that wrote codec-1 chunks, in zstd's `ZSTD_VERSION_NUMBER` form (`major*10000 + minor*100 + release`, `zstd.h` in https://github.com/facebook/zstd, read 2026-11-01); 0 if none was used |
| 48 | 4 | i32 | `compressorLevel` | informative: the zstd compression level used; 0 if none |
| 52 | 4 | u32 | reserved | |
| 56 | 8 | u64 | `entryCount` | number of entry records |
| 64 | 8 | u64 | `pathEntryCount` | number of entries with a path (flag `HAS_PATH`) |
| 72 | 8 | u64 | `keyEntryCount` | number of entries with a key (flag `HAS_KEY`) |
| 80 | 8 | u64 | `dataOffset` | start of the data region; MUST be 4096 in version 0.1 |
| 88 | 8 | u64 | `dataSize` | length of the data region in bytes: 0 if no entry has a chunk, otherwise the end offset of the last chunk's stored bytes minus `dataOffset` (no trailing padding) |
| 96 | 8 | u64 | `metadataOffset` | start of the metadata region; MUST be the smallest multiple of 4096 that is at least `dataOffset + dataSize` |
| 104 | 8 | u64 | `metadataSize` | length of the metadata region; MUST equal the sum of the seven sub-region sizes (section 7.1), is a multiple of 8 and at most 2^32 |
| 112 | 8 | u64 | `stringTableOffset` | start of the string table |
| 120 | 8 | u64 | `stringTableSize` | its length, a multiple of 8 (section 7.2) |
| 128 | 8 | u64 | `entryTableOffset` | start of the entry table (`entryCount * entryRecordSize` bytes; there is no size field) |
| 136 | 8 | u64 | `pathIndexOffset` | start of the path index (`pathEntryCount * 16` bytes) |
| 144 | 8 | u64 | `keyIndexOffset` | start of the key index (`keyEntryCount * 16` bytes) |
| 152 | 8 | u64 | `chunkTablesOffset` | start of the chunk tables region |
| 160 | 8 | u64 | `chunkTablesSize` | its length: MUST equal `chunkRecordSize` times the total number of chunk records (section 7.7) |
| 168 | 8 | u64 | `provenanceOffset` | start of the provenance region |
| 176 | 8 | u64 | `provenanceSize` | its length: the sum of the provenance record sizes (section 7.8), 0 if there are none |
| 184 | 8 | u64 | `extensionOffset` | start of the extension blocks (section 16.3) |
| 192 | 8 | u64 | `extensionSize` | its length, a multiple of 8, at most 16 MiB (0 if none) |
| 200 | 8 | u64 | `generation` | for a derived pack (flag `DERIVED`), the generation number it belongs to (section 18); otherwise 0 |
| 208 | 32 | bytes | `sourceDigest` | for a derived pack, the 32-byte digest of the packs and inputs it was built from (section 18); otherwise 32 zero bytes |
| 240 | 32 | bytes | `manifestDigest` | the logical digest of section 13.5 |
| 272 | 32 | bytes | `metadataDigest` | BLAKE3-256 of the bytes `[metadataOffset, metadataOffset + metadataSize)` |
| 304 | 176 | bytes | reserved | zero; room for fields of later minor versions |
| 480 | 32 | bytes | `headerChecksum` | BLAKE3-256 of the header bytes `[0, headerSize - 32)` (all of the header except this field). For `headerSize` 512 these are bytes 0 to 479 |

The path index and the key index have no size field: their sizes are `pathEntryCount * 16` and `keyEntryCount * 16`; the entry table's size is `entryCount * entryRecordSize`. "Empty" means size 0.

**The metadata region is exactly tiled.** The seven sub-regions follow each other in the order of section 7.1, each starting exactly where the previous one ends (the string table at `metadataOffset`), whether or not a sub-region is empty: the offset of an empty sub-region is therefore the end of the previous one. Every sub-region size is a multiple of 8. The last sub-region ends at `metadataOffset + metadataSize`, which is `fileSize - 128`. A pack that does not satisfy this is rejected (`E_RANGE`). (Draft 1 allowed free order and gaps; the review found that bytes in gaps are covered by no digest, so draft 2 forbids them.)

**Fixed outer layout in 0.1.** `dataOffset` is 4096, `metadataOffset` follows from the end of the data, and `fileSize = metadataOffset + metadataSize + 128`. The bytes `[headerSize, dataOffset)` and `[dataOffset + dataSize, metadataOffset)` MUST be zero (a reader checks them; they are at most 4095 bytes each). Gaps between the data blocks inside the data region (section 8.1) are not covered by any digest either: they MUST be zero, a validator checks it, a reader MAY skip it.

## 6. Footer

The footer is the last **128 bytes** of the file, at `fileSize - 128`. Its size is fixed for every version of generation `01`; growth goes in the header and in extension blocks.

| Offset | Size | Type | Field | Rule |
| --- | --- | --- | --- | --- |
| 0 | 8 | bytes | `footerMagic` | MUST equal `45 44 45 4E 45 4E 44 01` |
| 8 | 2 | u16 | `versionMajor` | MUST equal the header's |
| 10 | 2 | u16 | `versionMinor` | MUST equal the header's |
| 12 | 4 | u32 | `footerSize` | MUST be 128 |
| 16 | 8 | u64 | `fileSize` | MUST equal the real file size and the header's |
| 24 | 8 | u64 | `metadataOffset` | MUST equal the header's |
| 32 | 8 | u64 | `metadataSize` | MUST equal the header's |
| 40 | 32 | bytes | `metadataDigest` | MUST equal the header's |
| 72 | 32 | bytes | `headerChecksum` | MUST equal the header's |
| 104 | 24 | bytes | reserved | zero |

A reader that starts from the end of the file reads the last 128 bytes, checks `footerMagic`, reads the header from offset 0, and compares. Any disagreement is an error (`E_CHECKSUM`; a footer that is absent or not at the end is `E_TRUNCATED`).

## 7. Metadata region

### 7.1 Canonical order of the sub-regions

The sub-regions MUST follow each other in this order, with nothing between them (section 5, "exactly tiled"): **string table, entry table, path index, key index, chunk tables, provenance records, extension blocks.** Padding to a multiple of 8 is part of the sub-region it follows (the string table, each provenance record). The metadata region ends exactly at `fileSize - 128`.

### 7.2 String table

A byte array of the entries' paths, concatenated, with no separators and no terminators. Each path-bearing entry points into it with an offset and a length. Paths are stored in entry-table order, each stored once and back to back: the `pathOffset` of the first path entry is 0 and each next one equals the previous `pathOffset + pathLength` (`E_RANGE`); the sum of all path lengths rounded up to a multiple of 8 MUST equal `stringTableSize`, and the padding bytes are zero. So no two paths overlap or share bytes. A reader MUST check that each (offset, length) pair lies inside `stringTableSize`.

### 7.3 Paths

#### 7.3.1 Rules for a stored path (normalized path)

A stored path MUST satisfy all of the following (the rules of `VFS::Path::checkPackEntryName` in `components/vfs/entryname.cpp`, read 2026-11-01, with the length limit of section 12, plus the per-component limit of rule 1):

1. It is not empty and at most 1024 bytes (section 12), and each `/`-separated component is at most 255 bytes.
2. It is valid UTF-8 (section 3.7).
3. It contains no byte below 0x20 (no control character, NUL included).
4. It is **normalized**: it contains no `\` and no ASCII upper-case letter `A` to `Z`, and no two `/` follow each other. (Normalization replaces `\` with `/` and maps ASCII `A-Z` to `a-z` and does nothing else: no Unicode normalization, no case folding of non-ASCII letters. `components/vfs/pathutil.hpp`, `isNormalized`.)
5. It does not start or end with `/`.
6. It has no empty component and no component that is exactly `.` or `..`.

A reader MUST reject a pack with a stored path that breaks a rule: `E_LIMIT` when it breaks the length limits of rule 1 (path longer than 1024 bytes, component longer than 255 bytes, empty path), `E_NAME` for every other rule. The check is on the stored bytes; a reader MUST NOT "repair" a name.

Further rules apply when a *tool writes entries to disk*; they are in section 15.3.

#### 7.3.2 Ordering and uniqueness

Entries with a path are sorted by their path bytes compared **bytewise as unsigned values, shortest first on a common prefix** (the order of `memcmp` on the common length, then the shorter name first). Paths MUST be unique: after sorting two equal paths would be adjacent. With `cmp(a, b)`: `m = min(len a, len b); c = memcmp(a, b, m); if c != 0 return sign(c); return sign(len a - len b)`, each path MUST satisfy `cmp(previous, current) < 0`. A reader that finds `cmp == 0` rejects the pack with `E_DUPLICATE`, and `cmp > 0` with `E_ORDER`. This order does not treat `/` specially: `a-x` sorts before `a/b`.

**File and directory with one name.** A pack MUST NOT contain a path that is also a proper directory prefix of another path (`a` and `a/b`): a validator in canonical mode rejects it (`E_NAME`), a reader MAY accept it, and a tool that writes entries to disk MUST refuse it (section 15.4).

#### 7.3.3 Path hash

The *path hash* of a path is the first 8 bytes of the BLAKE3-256 digest of the path's bytes, read as a little-endian `u64`. (The same hash function as everywhere else, so no second hash is needed in an implementation.)

### 7.4 Entry table and entry record

The entry table has `entryCount` records of `entryRecordSize` bytes, in the **entry order**:

1. First, every entry that has a path, in path order (7.3.2). An entry that has both a path and a key is in this group and is ordered by its path only; its key takes no part in the ordering of group 2, but all keys of both groups together MUST be unique (`E_DUPLICATE`, found by the index check, section 14.2 item 12).
2. Then, every entry that has a key and no path, in ascending key order (unsigned numeric, 7.5).

An entry with neither a path nor a key is invalid. The order is part of the canonical form, and a reader MUST check it (`E_ORDER`). Data blocks are laid out in entry order (section 8.1), so keyed entries are stored in key order (for tile keys: in Morton order, section 7.5).

Entry record (112 bytes in this version; a later minor version MAY append fields, so a reader uses `entryRecordSize` as the stride and ignores the bytes it does not know):

| Offset | Size | Type | Field | Rule |
| --- | --- | --- | --- | --- |
| 0 | 8 | u64 | `key` | the entry's key if `HAS_KEY` is set (never 0); otherwise 0 |
| 8 | 8 | u64 | `pathOffset` | offset of the path inside the string table; 0 if no `HAS_PATH` |
| 16 | 4 | u32 | `pathLength` | length of the path in bytes; 0 if no `HAS_PATH` |
| 20 | 2 | u16 | `entryType` | section 11.2. Unknown values are read as generic |
| 22 | 2 | u16 | reserved | |
| 24 | 4 | u32 | `entryFlags` | section 11.5. Bits 0 to 15 critical, bits 16 to 31 ignorable |
| 28 | 4 | u32 | `chunkCount` | number of chunk records; 0 exactly when `uncompressedSize` is 0; at most 1,048,576 (section 12) |
| 32 | 8 | u64 | `uncompressedSize` | size of the content in bytes; at most 4 GiB (section 12) |
| 40 | 8 | u64 | `chunkTableOffset` | absolute offset of this entry's chunk records (section 7.7: fully determined by the chunk counts of the earlier entries); 0 if `chunkCount` is 0 |
| 48 | 32 | bytes | `contentHash` | BLAKE3-256 of the entry's whole content (section 13.1). For an empty entry the hash of zero bytes |
| 80 | 8 | u64 | `provenanceOffset` | absolute offset of this entry's provenance record (section 7.8), inside the provenance region, a multiple of 8; 0 unless `HAS_PROVENANCE` is set |
| 88 | 4 | u32 | `hintMemorySize` | hint: size of the decoded in-memory form in bytes, valid if `HINT_MEMSIZE` is set; 0 otherwise |
| 92 | 4 | u32 | `hintGroup` | hint: the prefetch-with group number, valid if `HINT_GROUP` is set; 0 otherwise |
| 96 | 4 | u32 | `layoutParamA` | a parameter of the entry type's typed layout (section 10); 0 for generic entries |
| 100 | 4 | u32 | `layoutParamB` | likewise |
| 104 | 8 | u64 | reserved | |

### 7.5 Key index and keys

A **key** is a 64-bit number that addresses an entry without a string. Bits 63 to 56 are the **key tag**; bits 55 to 0 are the payload, opaque to the container, meaning defined by the tag (section 11.3). The container only requires keys to be unique and the key index to be sorted by key as an **unsigned** number. The tile key's layout (worldspace, level and Morton-coded x and y inside the 56 bits) belongs to the tile annex (section 10.3) and is not frozen here. Key 0 is not valid.

The key index has `keyEntryCount` records of 16 bytes, one for each entry with `HAS_KEY`, **sorted by key ascending**:

| Offset | Size | Type | Field | Rule |
| --- | --- | --- | --- | --- |
| 0 | 8 | u64 | `key` | strictly greater than the previous record's |
| 8 | 8 | u64 | `entryIndex` | index into the entry table; that entry MUST have `HAS_KEY` and the same `key` |

A lookup by key is a binary search of this array. Perfect hashing may be added later as a minor-version feature (extension block, section 16.3).

### 7.6 Path index

The path index has `pathEntryCount` records of 16 bytes, one for each entry with `HAS_PATH`, **sorted by (`pathHash`, `entryIndex`) ascending**, both unsigned:

| Offset | Size | Type | Field | Rule |
| --- | --- | --- | --- | --- |
| 0 | 8 | u64 | `pathHash` | the path hash (7.3.3) of the entry's path; MUST be correct |
| 8 | 8 | u64 | `entryIndex` | index into the entry table; that entry MUST have `HAS_PATH` |

A lookup by normalized path computes the path hash, binary-searches the run of records with that hash, and compares the path bytes of each candidate in the string table (hashes can collide). Listing all entries is a walk over the entry table, which is already in path order. Because `pathEntryCount` equals the number of entries with `HAS_PATH` and every record is valid and distinct, every path entry appears exactly once.

### 7.7 Chunk tables and chunk record

The chunk tables region holds one table per non-empty entry, in entry order, back to back; each table is `chunkCount` records of `chunkRecordSize` bytes (56 in this version; a reader uses `chunkRecordSize` as the stride). The table of an entry starts at `chunkTablesOffset + chunkRecordSize * (the sum of the chunkCount of the earlier entries)`; the tables fill the region exactly (`chunkTablesSize` = `chunkRecordSize` times the total number of chunks, at most 2^25). So tables cannot overlap or be shared (`E_RANGE`). The chunk records of one entry are in content order: chunk 0 holds the first bytes of the content.

| Offset | Size | Type | Field | Rule |
| --- | --- | --- | --- | --- |
| 0 | 8 | u64 | `offset` | absolute offset of the chunk's stored bytes, inside the data region |
| 8 | 4 | u32 | `storedSize` | length of the stored bytes; 1 to 2^24, for every codec including unknown ones |
| 12 | 4 | u32 | `uncompressedSize` | length of the chunk's content; 1 to 8,388,608 |
| 16 | 2 | u16 | `codec` | section 11.1 |
| 18 | 2 | u16 | `chunkFlags` | section 11.6; `HASH_PRESENT` MUST be set in version 0.1 |
| 20 | 4 | u32 | reserved | |
| 24 | 32 | bytes | `chunkHash` | BLAKE3-256 of this chunk's **uncompressed** bytes (section 13.2) |

The chunk's content bytes lie at the position of the sum of the `uncompressedSize` of the earlier chunks of the entry. The sum of an entry's chunk `uncompressedSize` values MUST equal the entry's `uncompressedSize`. A chunk table is read, decoded and verified **alone**: nothing in any chunk depends on another chunk.

An entry whose content fits in one chunk has `chunkCount` 1, and its `chunkHash` equals its `contentHash` (a reader MUST check this at open). A reader MUST size its buffers from the chunk record's `uncompressedSize` (at most 8 MiB), never from the header's `chunkSize`.

### 7.8 Provenance record

Present for an entry with `HAS_PROVENANCE`. Used for **derived** entries: those a tool produced from other data. It lets a loader decide whether an entry is still valid without opening the sources (design doc 14.5). Each record starts at a multiple of 8 and is padded with zeros to a multiple of 8.

| Offset | Size | Type | Field | Rule |
| --- | --- | --- | --- | --- |
| 0 | 4 | u32 | `recordSize` | total size of this record including padding; a multiple of 8; at most 65,536 (section 12) |
| 4 | 2 | u16 | `toolIdLength` | length of the tool identifier in bytes, 1 to 255 |
| 6 | 2 | u16 | reserved | |
| 8 | 32 | bytes | `sourceDigest` | the digest of the source hashes the entry was built from. The container only stores it; the tool defines how it is formed (recommended: BLAKE3-256 over the sorted list of the sources' content hashes) |
| 40 | 4 | u32 | `parametersLength` | length of the parameter bytes, at most 60,000 |
| 44 | 4 | u32 | reserved | |
| 48 | `toolIdLength` | UTF-8 | `toolId` | tool name and version, for example `tilebaker/0.3.1` (no NUL) |
| next | `parametersLength` | bytes | `parameters` | the tool's canonical parameter encoding; opaque to the container |

`recordSize` MUST equal `48 + toolIdLength + parametersLength` rounded up to a multiple of 8 (so it never exceeds 60,304; the limit of 65,536 is a defensive bound). A reader MUST check that 48 bytes lie inside the region before it reads `recordSize` or the lengths. `toolId` MUST be valid UTF-8 without NUL. The records fill the provenance region exactly: the record of the first entry that has one starts at `provenanceOffset`, each next one where the previous ended, in entry order; `provenanceSize` is the sum of the `recordSize` values. Records are never shared between entries. `HAS_PROVENANCE` with `provenanceSize` 0 is `E_RANGE`. Padding bytes of a record MUST be zero (the manifest digest covers them).

### 7.9 Residency hints

The hints are fields of the entry record (`hintMemorySize`, `hintGroup`) and flags (`PINNED`, `COARSE_FIRST`). They are **advice**: a reader MAY ignore any of them and MUST ignore a hint it does not know. They are `PINNED` (never evict), `HINT_GROUP` (entries with the same non-zero group number are co-requested), `HINT_MEMSIZE` (the in-memory size, for budgeting) and `COARSE_FIRST` (load before finer entries).

## 8. Data blocks and chunking

### 8.1 Data blocks

An entry with at least one chunk has one **data block**: its chunks' stored bytes. The first chunk of the block MUST start at an **absolute file offset that is a multiple of 4096** (not an offset relative to `dataOffset`), inside the data region (this lets a reader use sector-aligned reads). In canonical form the chunks of one entry follow each other with **no gap** (chunk *k+1* starts where chunk *k* ends) and the next entry's block starts at the smallest multiple of 4096 that is at least the end of the previous entry's last chunk (a block that ends on a multiple of 4096 is followed directly by the next); the gap is zero bytes. Blocks follow the entry order (section 7.4). An empty entry (content of 0 bytes) has no chunks and no block.

A reader uses each chunk's `offset` and does not assume that chunks are adjacent. Taken in the order (entry order, chunk order), the stored ranges of all chunks of the pack MUST be strictly ascending and disjoint: `offset[k+1] >= offset[k] + storedSize[k]` (`E_OVERLAP`). This is checked in one pass with no sorting. Gaps between chunks are allowed in a non-canonical pack (they MUST be zero bytes, see section 5); chunks may not be shared between entries. The first chunk starts at or after `dataOffset`, and the last chunk ends exactly at `dataOffset + dataSize`.

### 8.2 Chunking

`chunkSize` in the header is the **nominal** uncompressed chunk size: a power of two from 4096 (2^12) to 8,388,608 (2^23) bytes. **Proposal for the default: 131,072 bytes (128 KiB).** Reasons: it lies in the 64 to 256 KiB range chosen in the design document, and it equals the largest block size the zstd format allows (the zstd format's `Block_Maximum_Size` is the smaller of the window size and 128 KB, https://github.com/facebook/zstd/blob/dev/doc/zstd_compression_format.md, read 2026-11-01), so a chunk of that size is not split into more blocks than a smaller one would be. The final default is set by the measurement of task 0051 stage g and then by the streaming prototype; a pack carries its own `chunkSize`, so a changed default breaks nothing.

**Default layout.** In version 0.1 the canonical writer cuts every entry, whatever its type, by the default layout; typed boundaries (section 10) are non-canonical until their annex is frozen. Entries are cut at multiples of `chunkSize`: every chunk but the last has exactly `chunkSize` uncompressed bytes, and the last has 1 to `chunkSize` bytes. A **typed layout** (section 10) may define other boundaries; the hard limits of section 12 still apply to every chunk. A reader MUST NOT require the default layout (it checks sizes only against the limits and the sum), while a validator in canonical mode MUST check it for every entry (every chunk but the last has exactly `chunkSize` bytes and `chunkCount` is `ceil(uncompressedSize / chunkSize)`).

## 9. Codecs

The codec of each chunk is in its chunk record (so a codec can be added without changing the format).

### 9.1 Codec 0: store

The stored bytes are the content bytes. `storedSize` MUST equal `uncompressedSize`.

### 9.2 Codec 1: zstd

The stored bytes are **exactly one zstd frame** (Zstandard, RFC 8878, https://datatracker.ietf.org/doc/html/rfc8878) and nothing else. A reader checks, before it decodes, using the zstd library's frame-header parser (`ZSTD_getFrameHeader`) and `ZSTD_findFrameCompressedSize`:

1. the frame is a normal frame with the magic `0xFD2FB528` (bytes `28 B5 2F FD`), not a skippable frame;
2. the `Frame_Content_Size` field is present and equals the chunk's `uncompressedSize`;
3. no dictionary: `Dictionary_ID` is absent or 0;
4. the frame's window size is at most 8 MiB (for a single-segment frame the window equals the content size). The reader compares the parsed window size itself and also sets the library's maximum window to 2^23;
5. the frame's compressed size equals `storedSize` exactly: no byte follows the frame (a second frame, a skippable frame and trailing bytes all fail);
6. the frame decodes to exactly `uncompressedSize` bytes, into an output buffer of that size.

Any failure is `E_CODEC` (at read; the pack still opens).

Content checksum: **the zstd content checksum SHOULD be absent** (the BLAKE3 chunk hash covers the content). A reader MUST accept a frame with the checksum flag set and verify it according to the zstd library's behaviour.

`storedSize` MUST NOT exceed (`E_LIMIT` at open; this bound is deliberately strict, a writer that exceeds it is not canonical) `U + (U >> 8) + margin`, where `U` is `uncompressedSize` and `margin` is `((131072 - U) >> 11)` if `U < 131072`, otherwise 0. This is zstd's own `ZSTD_COMPRESSBOUND` macro in `lib/zstd.h` (read 2026-11-01), which that header describes as the maximum compressed size in the worst case of a single pass.

**Decoder safety.** A reader MUST decode into a buffer of exactly `uncompressedSize` bytes, MUST fail if the frame would produce more or fewer, and MUST decode no chunk before the chunk record has passed the checks of section 14.

**Choosing the codec (writer).** A chunk is stored with codec 1 only when that is smaller by at least 1/16: `storedSize <= U - (U >> 4)`. Otherwise it is stored with codec 0. A writer MAY be told to store an entry uncompressed (for content that is already compressed, such as a KTX2 file with zstd supercompression, or audio codecs). Both choices are valid in a pack; the canonical form (section 17) fixes the rule.

## 10. Entry types and typed layouts

The `entryType` tells a consumer how to interpret the content and which typed layout (if any) governs the chunk boundaries. Everything else is a plain chunked blob. A reader that does not know a type reads the entry as generic. Registry: section 11.2.

### 10.1 Generic, mesh, audio, shader

Content is opaque bytes in the default layout. (A mesh is a glTF `.glb` or engine form, audio a codec stream, a shader a SPIR-V module; the container does not look at them.)

### 10.2 Texture (annex stub; **draft**, not frozen)

The content of a texture entry is a **KTX2 file** (Khronos, https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html, read 2026-11-01 through a text extraction of the page; the points below were taken from sections 3.9.7 and 3.13 and MUST be re-checked against the specification text itself before this annex is frozen: **verify**). The container stores the bytes and does not parse KTX2. KTX-Software is not a dependency of this format.

What the annex must say, and the design that follows from the specification:

- A KTX2 file stores its **mip level data from the smallest level to the largest** (section 3.13 of the KTX2 specification), while the Level Index is ordered with the base level (largest) at index 0 (section 3.9.7). So the always-resident **tail** (all small mips) is *already* at the front of the level data in a standard file, and the file does not need reordering.
- Chunk boundaries of a texture entry are placed on level boundaries: **chunk 0** holds everything from byte 0 of the file to the end of the last tail level (the headers, the descriptors, the Level Index and the small levels). Each larger level then has one chunk, or several consecutive chunks of at most 8 MiB if the level is bigger; a level never shares a chunk with another level. When the supercompression scheme is 0, KTX2 pads between levels so that each level starts aligned to the least common multiple of the texel block size and 4 bytes; the padding stays in the chunk it follows, so the `byteOffset` values of the Level Index stay valid inside the reassembled content.
- `layoutParamA` = the number of mip levels in the file; `layoutParamB` = the number of levels held in chunk 0 (the tail). **proposal**. The size limit of the tail (for example, "all levels up to 64 x 64") is a writer's choice, to be set by the texture stage (archive milestone A4).
- The pack's 4096-byte alignment is of each entry's first chunk in the pack; it is unrelated to the alignment of levels inside the KTX2 content.
- Content that is already supercompressed (KTX2 supercompression scheme other than 0) SHOULD use codec 0 for its chunks.

### 10.3 Tile, cluster, impostor and grass entries (annex stub; **later**, not frozen)

The entry types `0x0010` to `0x0013` (terrain tile, object cluster, impostor atlas, grass coverage map) and the key tags `0x10` to `0x13` are **reserved** now so that packs built by the LOD baker can use keyed entries. **Their payload layouts and the tile key's bit layout are not defined in version 0.1** and will be defined by the LOD stage specs after the streaming prototype (design doc 14.2 and 16, "freezing too early"); the version stays 0.x until then. Until then, a tile entry is a generic chunked blob addressed by key. The rules that already hold: the key's high 8 bits are the tag; for these types the tag MUST equal the low byte of `entryType`; the entries of one tile tree are stored in key order, so that a Morton-coded payload puts spatial neighbours next to each other in the file; and the limits of section 12 apply.

## 11. Registries

Each table has an **id**, a **status**, and the **behaviour on an unknown id**. Statuses: **stable** (will not change meaning within the major version), **draft** (in use, may change before 1.0), **reserved** (kept for a stated purpose, not usable), **experimental** (for tests; MUST NOT appear in a pack that is published or shipped).

**Allocation process (proposal).** An id is allocated by a numbered registry change: a short text giving the table, the id, the name, the status, what a reader does with the entry when the id is unknown, and a pointer to a written description of the id's meaning and, for a codec, a reference implementation licensed compatibly with the reference library. A change is reviewed, accepted by the maintainers in a numbered decision record, and added to this section with its number. An id is never reused or given another meaning. While the repository is not public, the changes are decision records in `docs/decisions/`; the process becomes the public registry when the specification is published.

### 11.1 Codec ids (`u16`)

| Id | Name | Status | Section |
| --- | --- | --- | --- |
| 0 | store | stable | 9.1 |
| 1 | zstd | stable | 9.2 |
| 2 to 255 | reserved for later standard codecs | reserved | |
| 256 to 65279 | unallocated (same behaviour as 2 to 255) | reserved | |
| 65280 to 65535 | experimental | experimental | |

Unknown codec: the chunk cannot be decoded. A reader MAY open the pack (the other entries stay usable) and MUST fail the read of the entry that uses it with `E_UNSUPPORTED`, never return the stored bytes as content.

### 11.2 Entry types (`u16`)

| Id | Name | Status | Section |
| --- | --- | --- | --- |
| 0x0000 | generic | stable | 10.1 |
| 0x0001 | texture (KTX2) | draft | 10.2 |
| 0x0002 | mesh | draft | 10.1 |
| 0x0003 | audio | draft | 10.1 |
| 0x0004 | shader | draft | 10.1 |
| 0x0005 to 0x000F | reserved for later core types | reserved | |
| 0x0010 | terrain tile | reserved (layout not defined) | 10.3 |
| 0x0011 | object cluster | reserved (layout not defined) | 10.3 |
| 0x0012 | impostor atlas | reserved (layout not defined) | 10.3 |
| 0x0013 | grass coverage map | reserved (layout not defined) | 10.3 |
| 0x0014 to 0x00FF | reserved for the tile family | reserved | |
| 0x0100 | face curves (`opas.facecurves` packed binary "OPFC") | draft (task 0071; the payload is that format's own, the entry's path is the voice line's path with `.facecurves`) | |
| 0x0101 to 0xEFFF | unallocated | reserved | |
| 0xF000 to 0xFFFF | experimental | experimental | |

Unknown entry type: **treat the entry as generic** (the content is readable; the type is advice about layout). A reader that gets a type it knows but a layout it cannot parse reports an error for the use, not for the open.

### 11.3 Key tags (top 8 bits of a key)

| Tag | Meaning | Status |
| --- | --- | --- |
| 0x00 | application-defined: the 56-bit payload is opaque | stable |
| 0x01 to 0x0F | reserved | reserved |
| 0x10 | terrain tile key | reserved (layout not defined) |
| 0x11 | object cluster key | reserved (layout not defined) |
| 0x12 | impostor atlas key | reserved (layout not defined) |
| 0x13 | grass coverage key | reserved (layout not defined) |
| 0x14 to 0x7F | reserved | reserved |
| 0x80 to 0xFF | experimental | experimental |

Unknown tag: the key is an opaque 64-bit number (ordering and uniqueness still apply).

### 11.4 Pack flags (`u64`)

| Bit | Name | Class | Status | Meaning |
| --- | --- | --- | --- | --- |
| 0 | `DERIVED` | critical | stable | the pack was built by a tool from other data; `generation` and `sourceDigest` in the header are meaningful (section 18) |
| 1 to 31 | | critical | reserved | a set bit that a reader does not know: reject the pack (`E_UNSUPPORTED`) |
| 32 to 63 | | ignorable | reserved | ignored if unknown |

### 11.5 Entry flags (`u32`)

| Bit | Name | Class | Status | Meaning |
| --- | --- | --- | --- | --- |
| 0 | `HAS_PATH` | critical | stable | the entry has a path (`pathOffset`, `pathLength`) |
| 1 | `HAS_KEY` | critical | stable | the entry has a key |
| 2 | `HAS_PROVENANCE` | critical | stable | the entry has a provenance record |
| 3 to 15 | | critical | reserved | unknown set bit: reject the pack (`E_UNSUPPORTED`) |
| 16 | `PINNED` | ignorable | draft | hint: never evict |
| 17 | `COARSE_FIRST` | ignorable | draft | hint: load before finer entries |
| 18 | `HINT_GROUP` | ignorable | draft | `hintGroup` is valid |
| 19 | `HINT_MEMSIZE` | ignorable | draft | `hintMemorySize` is valid |
| 20 to 31 | | ignorable | reserved | ignored if unknown |

### 11.6 Chunk flags (`u16`)

| Bit | Name | Class | Status | Meaning |
| --- | --- | --- | --- | --- |
| 0 | `HASH_PRESENT` | critical | stable | `chunkHash` holds the chunk's hash. In version 0.1 it MUST be set on every chunk (`E_RANGE` at open otherwise), so that ranged reads are always verified |
| 1 to 7 | | critical | reserved | unknown set bit: the entry cannot be read (`E_UNSUPPORTED` on read) |
| 8 to 15 | | ignorable | reserved | ignored if unknown |

### 11.7 Extension block ids (`u32`)

| Id | Name | Status | Meaning |
| --- | --- | --- | --- |
| 0x00000001 | writer info | draft | UTF-8 text, at most 1024 bytes: the writing tool and its version; informative |
| 0x00000002 to 0x7FFFFFFF | | reserved | |
| 0x80000000 to 0xFFFFFFFF | | experimental | |

Unknown block: skipped if its `MUST_UNDERSTAND` flag is clear; the pack is rejected (`E_UNSUPPORTED`) if it is set.

## 12. Limits

Every limit below is a **proposal**: the realistic sizes of a large Morrowind and a large New Vegas installation are **unknown** until measured (task 0051 stage f was to take them from real installs; that measurement has not been done because this task used no game data). The numbers are therefore chosen to be (a) far above any known need, (b) small enough that every product of two limits fits in 64 bits, and (c) small enough that a reader's memory use is bounded. A pack that exceeds a limit is **rejected** with `E_LIMIT` (the whole pack, at open, for the structural limits; the read, for a size that is only learned while reading). A reader MAY be configured with lower limits; it MUST NOT accept higher ones for this version.

| Limit | Value | Why this number |
| --- | --- | --- |
| Path length | 1024 bytes | equals `VFS::Path::defaultMaxEntryNameLength` in `components/vfs/entryname.hpp`, which names this specification as the place that fixes it; Windows' extended path limit is 32,767 characters, far above any asset path |
| Path component length | 255 bytes | the usual per-component limit of common file systems (**verify** per platform) |
| Metadata region | 4,294,967,296 (2^32) bytes | bounds the open-time work: the metadata digest is computed over it |
| Entry record size / chunk record size | at most 512 / at most 256 bytes | so the entry table is at most 2 GiB and every table product fits in 64 bits |
| Chunks per pack | 33,554,432 (2^25) | bounds the chunk tables to 8 GiB at 256 bytes (and the checks of 14.2 to one pass) |
| Stored size of a chunk | 16,777,216 (2^24), any codec | so no buffer is sized from an unknown codec's `storedSize` beyond that |
| Entries per pack | 4,194,304 (2^22) | at most 470 MB of entry table at 112 bytes each; hundreds of thousands of files are known to occur in game archives (**verify**, unmeasured), and a 100 x 100 cell tile tree has about 13,000 tiles (world-lod doc 12.3) |
| Chunks per entry | 1,048,576 (2^20) | 4 GiB divided by the smallest nominal chunk (4096) |
| Chunk uncompressed size | 8,388,608 (2^23) | RFC 8878's recommended decoder window of 8 MB; keeps every zstd window within it and bounds one decode buffer |
| Nominal chunk size (header) | 4096 to 8,388,608, a power of two | |
| Entry uncompressed size | 4,294,967,296 (2^32) | a safe cap for one asset; the field is 64-bit so that a later minor version can raise it |
| Total uncompressed size of a pack | 1,099,511,627,776 (2^40) | a hostile pack can declare far more decoded bytes than it stores (a zstd run-length chunk is ten bytes for 8 MiB); readers also take a caller budget (section 15.3) |
| Pack file size | 17,592,186,044,416 (2^44) | |
| String table size | 4,294,967,296 (2^32) | 2^22 entries of 1024 bytes |
| Provenance record | 65,536 bytes (tool id at most 255, parameters at most 60,000) | |
| Extension blocks | at most 1024 blocks; `extensionSize` at most 16 MiB (block headers and padding included) | |
| Nesting depth | **none**: a pack MUST NOT contain a pack as an entry that a reader opens as a pack | no nested archives (design doc 6) |
| Header and footer | `headerSize` exactly 512 in 0.1 (at most 4096 from 1.0); footer exactly 128 | |

Because a reader reads entry and chunk records using `entryRecordSize` and `chunkRecordSize` (at most 512 and 256), the entry table is at most 2^22 times 512 bytes and the chunk tables at most 2^25 times 256 bytes, which fit in 64 bits. The worst case of opening a hostile pack that stays inside the limits is: hash up to 4 GiB of metadata (streaming, no need to hold it), validate up to 4 GiB of path bytes, and one pass over at most 2^25 chunk records.

## 13. Hashing and integrity

### 13.1 Hash function

All hashes are **BLAKE3-256**: the BLAKE3 function with its default output size of 256 bits. BLAKE3's project page (https://github.com/BLAKE3-team/BLAKE3, README read 2026-11-01) states that the default output size is 256 bits, that it is a Merkle tree on the inside, and that it is released under CC0 1.0, Apache 2.0, or Apache 2.0 with LLVM exceptions. The digest of the empty input is `af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262` (first test vector of the project's `test_vectors/test_vectors.json`, read 2026-11-01). The container uses only the plain hash mode: no key and no derived key.

A hash is stored as 32 bytes, in the order the function outputs them (the order of the usual hexadecimal text).

### 13.2 What is hashed

| Hash | Over | Stored in |
| --- | --- | --- |
| content hash | the whole entry's content (uncompressed bytes) | entry record `contentHash` |
| chunk hash | one chunk's uncompressed bytes | chunk record `chunkHash` |
| path hash | the path bytes (first 8 bytes of the digest) | path index |
| header checksum | the header bytes except the checksum field | header, footer |
| metadata digest | the whole metadata region | header, footer |
| manifest digest | the logical description of the entries (13.5) | header |

The entry's content hash is the plain BLAKE3-256 of the content, **independent of chunking and of the compressor**. This is deliberate: the engine's `VFS::File::getContentHash()` (`components/vfs/file.hpp`, task 0050) is defined as the BLAKE3-256 of the content, and the engine compares hashes of the same path in different packs and of loose files to decide whether two copies are identical. A hash built from chunk hashes would differ for the same file packed with another chunk size. The chunk hash is an *additional* per-chunk check.

The chunk hash is over the **uncompressed** bytes so that it does not change with the compressor version (section 17). The consequence for security is in section 15: a reader cannot check a chunk before decoding it, so decoding must be bounded.

### 13.3 Which checks a reader makes, and when

At **open**: header checksum, footer agreement, regions and structure (section 14.2), and the metadata digest (a reader MAY skip the metadata digest only when the caller explicitly asks for a faster open, for example for a cache pack the same program just wrote; every other check stays). At **read** of a chunk: the chunk hash (always present in 0.1). At read of a whole entry, or when asked to verify: the content hash over all the decoded bytes. Hashes are checked **before the content is trusted**: a chunk whose hash fails MUST NOT be returned to the caller as content.

### 13.4 What the hashes do not do

They detect damage and give identity. They do not authenticate the writer: whoever writes a pack also writes its hashes. There are no signatures in this version.

### 13.5 Manifest digest

The *manifest digest* identifies the **logical content** of a pack, independent of the compressor and of where bytes lie. It is BLAKE3-256 of the following byte string:

1. the 18 ASCII bytes `EDENPAK-MANIFEST-1`;
2. `packFlags` (`u64`), `generation` (`u64`), the 32 bytes of `sourceDigest`, `chunkSize` (`u32`), `entryCount` (`u64`);
3. for each entry, in entry order: `entryFlags` (`u32`), `entryType` (`u16`), 2 zero bytes, `key` (`u64`), `pathLength` (`u32`), the path bytes, `uncompressedSize` (`u64`), the 32 bytes of `contentHash`, `hintMemorySize`, `hintGroup`, `layoutParamA`, `layoutParamB` (each `u32`), `chunkCount` (`u32`), then for each chunk its `uncompressedSize` (`u32`) and its 32 `chunkHash` bytes, then the provenance record's `recordSize` (`u32`; 0 if the entry has none) and, if it has one, the `recordSize` bytes of the record (padding included).

A keyed entry contributes `pathLength` 0 and no path bytes; an entry with no content contributes `chunkCount` 0 and no chunk list. The digest includes `packFlags` and `entryFlags` and the hint fields as stored, ignorable bits and values included (it describes the pack as written). All integers little-endian. A validator MUST recompute it and compare (`E_CHECKSUM`); a reader MAY. The digest does **not** contain codec ids, stored sizes, offsets, `compressorVersion`, `compressorLevel` or extension blocks, which are what differ between two correct writers. It is used to name derived packs and to compare two packs built from the same input.

## 14. Reader requirements

### 14.1 Error classes

A reader that rejects a pack or an entry SHOULD report one of these classes, together with the **byte offset** of the offending field (an absolute file offset; 0 when there is none) and the **field name** as spelled in the tables of this document (for example `chunk[2].offset`). A conformance test compares the class and, where the corpus says so, the offset, never message text. The reference tools exit with 0 when the pack is valid, 1 when it is invalid or a read fails, and 2 for a usage or input/output error (the class is for diagnostics; the **rejection** is what is required):

| Class | Meaning |
| --- | --- |
| `E_MAGIC` | not a pack (wrong first 7 bytes) |
| `E_VERSION` | a version or generation this reader does not support |
| `E_TRUNCATED` | the file is shorter or longer than it says, or the footer is missing or not at the end |
| `E_CHECKSUM` | header checksum, metadata digest or header/footer disagreement |
| `E_RANGE` | an offset, size or count outside its region, misaligned, or an arithmetic overflow, or a field that contradicts another |
| `E_OVERLAP` | two chunks overlap or are out of order (sub-regions of the metadata region that do not tile are `E_RANGE`) |
| `E_ORDER` | entries, path index or key index out of order |
| `E_DUPLICATE` | a path or a key appears twice |
| `E_NAME` | a stored path breaks section 7.3.1 |
| `E_LIMIT` | a limit of section 12 is exceeded |
| `E_INDEX` | the path index or key index does not match the entry table |
| `E_UNSUPPORTED` | an unknown critical flag, an unknown codec or a must-understand extension block |
| `E_CODEC` | a chunk's stored bytes do not decode as the codec requires (bad frame, wrong content size, dictionary, wrong decoded length) |
| `E_HASH` | a chunk hash or content hash does not match the decoded bytes |

### 14.2 Checks at open (a reader MUST reject the pack if any fails)

**Order and class.** The checks are made in the order below. The reported class is that of the first check that fails (items in order, bullets in order, entries in index order, chunks in order). In every check a limit comparison comes before arithmetic on the compared value. All arithmetic is 64-bit unsigned and overflow-checked (a sum that would overflow fails with `E_RANGE`, unless the value was already refused by its limit). A reader MAY run its checks in another order only if it reports the same class.

1. **Identification.** A file of fewer than 7 bytes, or whose first 7 bytes are not `EDENPAK`: `E_MAGIC`. The eighth byte is not `01`: `E_VERSION`. The file is shorter than 640 bytes, or its last 128 bytes do not begin with the footer magic: `E_TRUNCATED`.
2. **Version and sizes.** `versionMajor.versionMinor` is exactly 0.1, and `headerSize` is 512 (`E_VERSION`); `footerSize` is 128 (`E_RANGE`).
3. **Checksums.** The header checksum is correct, and the footer agrees with the header on version, `fileSize`, `metadataOffset`, `metadataSize`, `metadataDigest` and `headerChecksum` (`E_CHECKSUM`).
4. **File size.** The real size is at most 2^44 (`E_LIMIT`) and equals `fileSize` (`E_TRUNCATED`).
5. **Counts and strides.** `packFlags` has no unknown critical bit (`E_UNSUPPORTED`). `chunkSize` is a power of two from 4096 to 2^23, `entryRecordSize` is a multiple of 8 from 112 to 512, `chunkRecordSize` is a multiple of 8 from 56 to 256 (`E_RANGE`). `entryCount` is at most 2^22 (`E_LIMIT`). `pathEntryCount` and `keyEntryCount` are each at most `entryCount`, and `entryCount` is at most their sum (`E_RANGE`).
6. **Regions.** `metadataSize` is at most 2^32 (`E_LIMIT`). `dataOffset` is 4096; `dataSize` is at most `fileSize - 4096`; `metadataOffset` is the smallest multiple of 4096 at least `dataOffset + dataSize`; `metadataOffset + metadataSize + 128` equals `fileSize`. The sub-regions tile the metadata region as section 5 states, with sizes `stringTableSize` (a multiple of 8), `entryCount * entryRecordSize`, `pathEntryCount * 16`, `keyEntryCount * 16`, `chunkTablesSize` (a multiple of 8), `provenanceSize` (a multiple of 8) and `extensionSize` (a multiple of 8, at most 16 MiB, that is `E_LIMIT` first), and `metadataSize` is their sum (all `E_RANGE`). The bytes in `[headerSize, dataOffset)` and in `[dataOffset + dataSize, metadataOffset)` are zero (`E_RANGE`).
7. **Metadata digest.** BLAKE3-256 of the metadata region equals `metadataDigest` (`E_CHECKSUM`), computed in streaming fashion. A reader MAY skip it only when the caller explicitly asks for a faster open (13.3); the conformance corpus is run with it on.
8. **Extension blocks.** Walk the extension region: at least 16 bytes remain for a block header, else `E_RANGE`; `payloadSize` is at most the remaining bytes minus 16 (checked before it is rounded up to a multiple of 8) and the padded block fits, else `E_RANGE`; at most 1024 blocks (`E_LIMIT`); a payload size of 0 is legal; `blockFlags` bits other than bit 0 are ignored; a block with `MUST_UNDERSTAND` whose id the reader does not know: `E_UNSUPPORTED`; the known block 1 has a payload of at most 1024 bytes (`E_LIMIT`). The walk ends exactly at `extensionSize`.
9. **Entries.** For every entry record, in order, with `i` its index:
   - **Flags.** No unknown critical `entryFlags` bit (`E_UNSUPPORTED`). At least one of `HAS_PATH` and `HAS_KEY` is set. `key` is non-zero exactly when `HAS_KEY` is set. If `HAS_PATH` is clear, `pathOffset` and `pathLength` are zero. If `HAS_PROVENANCE` is clear, `provenanceOffset` is zero. If `entryType` is 0x0010 to 0x0013, `HAS_KEY` is set and the key's high 8 bits equal the low byte of `entryType` (all `E_RANGE`).
   - **Sizes.** `uncompressedSize` is at most 2^32 and the running sum over all entries is at most 2^40 (`E_LIMIT`); `chunkCount` is at most 2^20 and the running total of chunk counts is at most 2^25 (`E_LIMIT`); `chunkCount` is 0 exactly when `uncompressedSize` is 0 (`E_RANGE`); if it is 0, `chunkTableOffset` is 0 and `contentHash` is the BLAKE3-256 of zero bytes (`E_RANGE`).
   - **Path** (if `HAS_PATH`). `pathLength` is 1 to 1024 (`E_LIMIT`); the path lies inside the string table (`E_RANGE`); it satisfies 7.3.1 (`E_NAME`); `cmp(previous path, this path) < 0` (`E_DUPLICATE` if equal, `E_ORDER` if greater), and no path entry follows a key-only entry (`E_ORDER`); `pathOffset` equals the end of the previous path (0 for the first) (`E_RANGE`). After the last entry, the sum of the path lengths rounded up to 8 equals `stringTableSize`, with zero padding (`E_RANGE`).
   - **Key** (if `HAS_KEY` and not `HAS_PATH`). The key is strictly greater than the previous key-only entry's (`E_ORDER`, or `E_DUPLICATE` if equal).
   - **Provenance** (if `HAS_PROVENANCE`). The record is at the position section 7.8 fixes, its 48 fixed bytes lie in the region, `recordSize` is a multiple of 8 and at most 65,536, `toolIdLength` is 1 to 255, `parametersLength` is at most 60,000 (`E_LIMIT`), `recordSize` equals the rounded sum (`E_RANGE`), `toolId` is valid UTF-8 without NUL (`E_NAME`). After the last entry the record sizes add up to `provenanceSize` (`E_RANGE`).
   - **Chunk table position.** `chunkTableOffset` equals `chunkTablesOffset + chunkRecordSize * (chunks of earlier entries)` (`E_RANGE`). After the last entry, `chunkTablesSize` equals `chunkRecordSize` times the total (`E_RANGE`).
10. **Chunks.** For every chunk record of every entry, in (entry, chunk) order: `uncompressedSize` is 1 to 2^23 and `storedSize` is 1 to 2^24 (`E_LIMIT`); for codec 0 `storedSize` equals `uncompressedSize` (`E_RANGE`); for codec 1 `storedSize` is at most the bound of 9.2 (`E_LIMIT`); `HASH_PRESENT` is set (`E_RANGE`); `offset` is at least `dataOffset`, at most `dataOffset + dataSize`, and `storedSize` is at most `dataOffset + dataSize - offset` (`E_RANGE`); the first chunk of an entry starts at a multiple of 4096 (`E_RANGE`); `offset` is at least the end of the previous chunk of the pack (`E_OVERLAP`); the running sum of the entry's chunk `uncompressedSize` stays at most the entry's `uncompressedSize` and ends equal to it (`E_RANGE`); for a one-chunk entry `chunkHash` equals `contentHash` (`E_RANGE`). After the last chunk, its end equals `dataOffset + dataSize` (and `dataSize` is 0 if there is no chunk) (`E_RANGE`).
11. **Path index.** There are `pathEntryCount` records. Each `entryIndex` is below `entryCount` and names an entry with `HAS_PATH` (`E_INDEX`); the `pathHash` is correct, recomputed by the reader (`E_INDEX`); records are strictly ascending by (`pathHash`, `entryIndex`) (`E_ORDER`); because the counts are equal and the pairs distinct, each path entry appears once.
12. **Key index.** There are `keyEntryCount` records. Each `entryIndex` is below `entryCount` and names an entry with `HAS_KEY` and the same key (`E_INDEX`); keys strictly ascending (`E_ORDER`, or `E_DUPLICATE` if equal).

A reader MAY perform checks 9 to 12 lazily only if it still guarantees that an entry or index record is checked before it is used, and it MUST NOT return data from a pack whose checks 1 to 8 failed. Open-time work and memory are proportional to the metadata size (at most 2^32 bytes); a reader SHOULD let the caller set lower caps.

### 14.3 Checks at read

1. The chunk's codec is known; if not, `E_UNSUPPORTED` for that read.
2. For codec 1: the stored bytes are a single frame that satisfies section 9.2, decoded into a buffer of exactly `uncompressedSize` bytes.
3. If `HASH_PRESENT`, the BLAKE3-256 of the decoded bytes equals `chunkHash`; else `E_HASH`.
4. When the whole entry is read or verified: the BLAKE3-256 of all content equals `contentHash`.
5. (Draft 2) A reader that returns a chunk's bytes has verified `chunkHash`; there is no unverified path in version 0.1, because every chunk has a hash. The entry's `contentHash` is metadata the pack states about itself: a consumer that uses it to decide identity (for example the engine's conflict report) trusts the pack's author until the whole entry has been read and verified, or the pack validated.
6. A read of a range `[offset, offset + size)` of the content MUST satisfy `offset <= uncompressedSize` and `size <= uncompressedSize - offset`; otherwise it fails. The reader decodes only the chunks that overlap the range.

### 14.4 Mapping to a virtual file system (informative)

The engine's `VFS::File::size()` is the entry's `uncompressedSize`; `readRange(offset, size, destination)` is 14.3 item 5; `getContentHash()` is the entry's `contentHash`. A file that is not a valid pack is not registered (the archive factory returns no archive).

### 14.5 Lookup (informative pseudocode)

```text
open(path):  h = first8(blake3(path)) as u64 LE
             i = lower_bound(pathIndex, key = (h, 0))
             while pathIndex[i].hash == h:
                 e = entry[pathIndex[i].entryIndex]
                 if stringTable[e.pathOffset .. +e.pathLength] == path: return e
                 i += 1
             return not found
read(e, off, n): for each chunk c overlapping [off, off+n): decode c (9), verify c.chunkHash,
                 copy the overlap
```

## 15. Security considerations

A pack is hostile until proven otherwise (design doc 3 and 16). In addition to the rules above:

1. **Bounds.** Every offset, size and count is bounds-checked against its region before it is used, with overflow-safe arithmetic (section 3.8). Table sizes are bounded by the limits before they are multiplied.
2. **Allocation.** A reader's allocation is bounded by the limits: no buffer larger than one chunk's `uncompressedSize` (at most 8 MiB) is allocated from a chunk record, and no table larger than the limits allows. It never allocates according to an unchecked field.
3. **Decompression bombs.** A zstd frame can expand by a very large factor. The defences are: the declared `uncompressedSize` is limited (8 MiB per chunk, 4 GiB per entry, 2^40 per pack); the decoder is given an output buffer of exactly the declared size and a frame that would write more fails; the frame's own content-size field must agree with the chunk record; the window is bounded; and a reader that serves untrusted packs SHOULD also keep a time and memory budget per request. A reader or validator MUST accept a caller-supplied budget of decoded bytes and fail with `E_LIMIT` when it is exceeded; the tools default to 2^36 bytes unless told otherwise. Because the chunk hash is over decoded bytes, **the decoder runs before the hash can be checked**; this is why the decoder limits above are required, and why a reader SHOULD keep the zstd library up to date.
4. **Path traversal and name tricks.** The stored-name rules (7.3.1) forbid `..`, leading `/`, `\` and control characters. A tool that writes entries to a disk folder MUST additionally apply the stricter extraction rules of `VFS::Path::checkExtractionSafe` (`components/vfs/entryname.cpp`, read 2026-11-01): reject a component that contains any of `: < > " | ? * \`; reject a component that ends with `.` or a space; reject a component whose part before the first `.` (ignoring trailing spaces) is a Windows reserved device name (`con`, `prn`, `aux`, `nul`, `conin$`, `conout$`, `clock$`, `com1` to `com9`, `lpt1` to `lpt9`, and `com`/`lpt` followed by a superscript 1, 2 or 3), whatever the case; and write only inside the chosen target folder; refuse a path that is also a directory prefix of another path of the pack (`a` and `a/b`); and never follow a symbolic link or junction found in the output folder. Names that differ only by Unicode normalization or by non-ASCII case are different names to the container; an extractor on a case-insensitive file system SHOULD detect two entries that map to one file and refuse.
5. **Duplicates and shadowing.** Duplicate paths and keys are rejected, so a lookup has one answer.
6. **No code, no secrets.** The format has no executable content, no scripting and no encryption or DRM. A consumer must not execute an entry because of its type or name.
7. **Resource exhaustion.** A reader SHOULD verify the metadata digest at open and SHOULD be fuzzed (task 0052).
8. **Integrity is not authenticity.** Section 13.4.
9. **Time-of-check to time-of-use.** A pack can change while it is open. The first reader does not memory-map (design doc 14.8), reads with positional reads and treats a short read as an error; a reader that maps the file must handle the file shrinking.
10. **Hash collisions in the path index** are expected (64-bit) and handled by comparing the path bytes (14.5).

## 16. Versioning and extension

### 16.1 Version numbers and how a reader treats them

The version is `versionMajor.versionMinor` in the header and footer.

- **Version 0.x is a draft**: no compatibility promise. A reader of this specification supports **exactly 0.1**. It MUST reject (`E_VERSION`) any other 0.y, because a draft minor may change anything.
- **From 1.0 on** (rules for the future, stated now so that the first stable readers follow them): a *minor* version adds only things that an older reader can ignore (new ignorable flags, new hints, new skippable extension blocks, new fields appended to records or in reserved space, new codec and type ids that only affect the entries using them). A *major* version may break anything. A reader MUST accept any minor of a major it supports, ignoring what it does not know, and MUST reject (`E_VERSION`) a higher major. A pack whose version is lower than the reader's is read by the older rules.

### 16.2 Unknown things, summarised

| Unknown | Reader behaviour |
| --- | --- |
| higher major version | reject, `E_VERSION` |
| higher minor of a supported major (from 1.0) | accept; ignore unknown ignorable flags, hints, extension blocks, appended record fields, header bytes after 512 |
| unknown critical pack flag, entry flag | reject the pack, `E_UNSUPPORTED` |
| unknown ignorable flag | ignore |
| unknown chunk flag in bits 1 to 7 | the entry is unreadable, `E_UNSUPPORTED` at read |
| unknown codec id | the entry is unreadable, `E_UNSUPPORTED` at read; the pack stays usable |
| unknown entry type | read as generic |
| unknown key tag | opaque key |
| unknown extension block | skip, unless `MUST_UNDERSTAND`, then reject the pack |
| non-zero reserved field | ignore (canonical validators report it) |

### 16.3 Extension mechanism

Reserved space: 176 bytes in the header, the reserved fields of the records, the stride fields `entryRecordSize` and `chunkRecordSize` (so records can grow), and the extension blocks.

The **extension blocks** (sub-region at `extensionOffset`) are a sequence of blocks, each starting at a multiple of 8:

| Offset | Size | Type | Field | Rule |
| --- | --- | --- | --- | --- |
| 0 | 4 | u32 | `blockId` | section 11.7 |
| 4 | 4 | u32 | `blockFlags` | bit 0 = `MUST_UNDERSTAND`; the other bits reserved |
| 8 | 8 | u64 | `payloadSize` | bytes of payload |
| 16 | `payloadSize` | bytes | payload | then zero padding to a multiple of 8 |

A reader walks the blocks until `extensionSize` is used up, with the checks of section 14.2 item 8; a block that does not fit is `E_RANGE`. A canonical pack (section 17) has **no** extension blocks, because a writer-info block would make two writers' packs differ.

## 17. Canonical form and determinism

A **canonical pack** is the one pack that this specification defines for a given *input*. Canonical form is what makes hashes of packs meaningful and builds reproducible (design doc 15 and 18).

### 17.1 The input

A canonical writer is a function of:

1. the **list of entries**, each with: a path and/or a key, an entry type, the content bytes, the hints (flags and values), the typed-layout parameters, an optional provenance record, and a **compression policy** (`auto` or `store`, 9.2);
2. the **pack flags**, `generation` and `sourceDigest`;
3. the nominal **chunk size** (default 131,072);
4. the **zstd level** and the zstd library version and parameters (17.4).

It does **not** depend on: the order in which entries were supplied, the number of threads, host paths, time, user names, or any random value.

### 17.2 The algorithm

1. **Validate.** Check every path against 7.3.1 and the limits of section 12. Reject a duplicate path or key.
2. **Order.** Sort entries with a path by path (7.3.2); then entries with only a key by key. This is the entry order.
3. **Chunk.** Cut each entry's content by the default layout (8.2) or its typed layout.
4. **Hash.** Compute each chunk hash and each entry's content hash.
5. **Choose codecs and compress.** For each chunk of an entry whose policy is `auto`, compress it with zstd (17.4) and use codec 1 if `storedSize <= U - (U >> 4)` (9.2), else store it with codec 0. A `store` entry uses codec 0 for every chunk. Chunks are independent, so they may be compressed on any number of threads; the result does not depend on that.
6. **Lay out the data region.** `dataOffset` = 4096. For each entry in entry order that has chunks: start at the smallest multiple of 4096 that is at least the end of the previous block, write the chunks' stored bytes back to back; fill every gap with zero bytes. `dataSize` = end of the last stored byte minus `dataOffset` (0 if there is none).
7. **Lay out the metadata region.** `metadataOffset` = the smallest multiple of 4096 at or after `dataOffset + dataSize` (zero fill between). Write, in the order of 7.1: the string table (paths in entry order, padded to a multiple of 8); the entry table; the path index; the key index; the chunk tables (in entry order); the provenance records (in entry order, each padded to a multiple of 8); no extension blocks. `entryRecordSize` = 112 and `chunkRecordSize` = 56. Every chunk record has `HASH_PRESENT` set. The offset of every empty sub-region = the end of the previous sub-region (the first = `metadataOffset`).
8. **Compute the digests.** `manifestDigest` (13.5), `metadataDigest`.
9. **Header and footer.** `fileSize` = `metadataOffset + metadataSize + 128`. Write the header with `compressorVersion` and `compressorLevel` as in 17.4 (both 0 when no chunk uses codec 1), reserved bytes zero, then `headerChecksum`; write the footer.
10. **No other bytes**: no trailing data, no timestamps, no host paths, no writer name.

### 17.3 What "the same pack" means: decision

The compressed bytes of a zstd chunk are **not** a function of the content alone. Neither the zstd format document nor RFC 8878 requires a compressor to produce a unique encoding; the library's own header says that standard builds take part in determinism guarantees only "with other builds of the same version" (the comment of `ZSTD_isDeterministicBuild()` in `lib/zstd.h`, read 2026-11-01), and that the row-based match finder is chosen at run time from SIMD support by default (the comment of `ZSTD_c_useRowMatchFinder`, same file), so even the same version may choose differently on two CPUs unless the parameter is set. The decision (**proposal**, toddhow may change it, Annex E D6):

1. The canonical form is **byte-exact for everything except the stored bytes of codec-1 chunks and what depends on their sizes** (offsets after them, `storedSize`, the codec choice at the margin, `dataSize`, `metadataOffset`, `fileSize`, and `metadataDigest`, `headerChecksum` and the footer's copies, which cover those fields; the manifest digest does not).
2. Those bytes are reproducible **only when the writer uses the same (codec id, level, parameters, zstd library version)**. The header records the version and the level (`compressorVersion`, `compressorLevel`) so that a tool can tell whether it can expect identical bytes.
3. **Validity and identity do not depend on the compressed bytes.** What identifies a pack logically is its `manifestDigest`, which excludes everything that differs between correct writers. A *canonical-form validator* checks the layout rules of 17.2 and the logical content (and the file/directory name rule of 7.3.2, the zero-ness of padding, reserved bytes and gaps, `headerSize` 512, strides 112 and 56, no extension blocks, zero hints/`layoutParam`s whose flag is clear or whose type is generic, zero `generation` and `sourceDigest` without `DERIVED`, the manifest digest, that a codec-1 chunk satisfies the 1/16 rule of 9.2 (a codec-0 chunk of an `auto` entry cannot be judged), and that identical content in two entries is stored twice, not shared); it does **not** require the compressed bytes to equal a particular compressor's output.
4. A pack whose chunks are **all codec 0** is byte-for-byte reproducible by every conforming writer; the conformance suite (Annex D) uses such packs for byte-exact golden files, and uses decoded-content and `manifestDigest` comparison for packs with codec-1 chunks.
5. The reference writer pins the zstd library to one tagged version and sets its parameters explicitly (17.4), so builds of OpenEden's own packs are reproducible; that is a property of the reference writer, not a requirement on other writers.

### 17.4 zstd parameters for canonical output (writer)

A canonical writer SHOULD compress each chunk as one independent frame with the single-call interface (no worker threads inside zstd: `ZSTD_c_nbWorkers` 0), no dictionary, `ZSTD_c_contentSizeFlag` on (it is on by default, `zstd.h`), `ZSTD_c_checksumFlag` off, the match finder parameter `ZSTD_c_useRowMatchFinder` set explicitly rather than left at `auto`, and a recorded level. **Default level: 3** (`ZSTD_CLEVEL_DEFAULT` is 3 in `zstd.h`, read 2026-11-01), the speed-oriented choice for derived and cache packs; tools that build a pack for distribution MAY use a higher level and record it. Which parameters other than these affect the output is **verify**: the pinned reference writer records the full parameter set in the task 0052 notes.

## 18. Update and derived-pack conventions (informative)

These are conventions for tools, not part of the bytes of a pack (design doc 14.7 and 14.8; world-lod doc 4.6 and 4.7).

1. **Packs are immutable and content-named.** A tool never edits a pack in use. It writes a new file with a new name (for example the hexadecimal `manifestDigest`, with the extension `.edenpak`) and, to switch to it, replaces a small *generation pointer* file by atomic rename; the pointer is read when the application starts.
2. **Derived packs.** A pack with flag `DERIVED` (11.4) was built by a tool from other data and lives in a cache folder, never inside an author's pack. Its `generation` names the generation it belongs to and its `sourceDigest` is the digest of the packs and inputs it was built from. A loader MUST ignore a derived pack whose `sourceDigest` does not match the digest it computes from the current inputs: stale data is never shown. How the digest is formed is the tool's rule and is stored in the pack only as the 32 bytes.
3. **Entries of a derived pack** carry a provenance record (7.8) so a loader can judge each entry without opening the sources.
4. **Removing obsolete packs** (including while a program runs, with an `in-use` marker that expires if its process died) is left to the application; the format needs only that readers do not memory-map, so files can be replaced (design doc 14.8; the marker approach is **verify**).

## 19. Comparison with BSA and BA2 (informative)

For people porting tools. Facts here are from OpenEden's own readers (`components/bsa/*`, read 2026-11-01) unless marked **verify**.

| | Morrowind BSA | Compressed BSA (Oblivion/Skyrim family) | BA2 (Fallout 4 family) | Eden pack |
| --- | --- | --- | --- | --- |
| Identification | first `u32` is `0x100` (`bsafile.cpp`) | reader accepts versions 103, 104, 105 (`compressedbsafile.cpp`; magic **verify**) | magic `BTDX`, type `GNRL` or `DX10` (`ba2file.cpp`, `ba2dx10file.cpp`) | `EDENPAK` + `01` |
| Name lookup | a name hash plus a table of names | name hashes (**verify**) | **verify** | BLAKE3 path hash and a sorted string table; exact path bytes compared |
| Content hash | none (the "hash" is of the name) | none | none | BLAKE3-256 per entry and per chunk |
| Compression | none | whole-file (**verify**) | whole-file or per texture chunk (**verify**) | per chunk, zstd |
| Streaming | no | no | texture chunks, **verify** | chunked and aligned by design |
| Case | case-insensitive name hashing | same | same | names stored lower-case ASCII; normalized by the writer |
| Largest archive | 32-bit offsets in classic BSA (**verify**) | 32-bit (**verify**) | 64-bit offsets (**verify**) | 64-bit |

## Annex A. Identifier collision check

Done on **2026-11-01** for task 0051. Aim: show what was searched and what came back for the extension `.edenpak`, the magic bytes, the name and the MIME type. **No search can prove that a name or extension is unused**; this annex records only "no hit found in X, Y, Z on this date", and lists what could not be checked. Nothing here is legal advice.

### A.1 Extension `.edenpak`

| Source | URL / method | Result |
| --- | --- | --- |
| Web search engine | queries `".edenpak" file extension` and `edenpak` | no page describing a `.edenpak` format; the second query found only an unrelated trademark record (A.3) and bag brands |
| file.org | https://file.org/extension/edenpak | a page exists but states that no program is verified for the extension: a generated placeholder, **neither a hit nor a clearance** |
| fileinfo.com | https://fileinfo.com/extension/edenpak | HTTP 404, no entry |
| file-extensions.org | https://www.file-extensions.org/edenpak-file-extension | HTTP 404, no entry (its search form cannot be driven by a plain page fetch) |
| Library of Congress | https://www.loc.gov/search/?q=edenpak&fo=json ; Digital Formats browse list https://www.loc.gov/preservation/digital/formats/fdd/browse_list.shtml | the search returned 0 results; no format name or extension containing "eden" in the browse list |
| UK National Archives PRONOM | https://pronom.nationalarchives.gov.uk/ | **not checked**: the site refused the fetch (HTTP 403) |
| GitHub, repositories and issues | API `api.github.com/search/repositories?q=edenpak` and `search/issues?q=edenpak` | 0 repositories and 0 issues for `edenpak`; `edenpak in:name,description,readme` found 1 repository, a 2018 test repository of a user account named `EdenPak` (https://github.com/EdenPak, no description, not a format) |
| GitHub, code search | https://github.com/search?q=edenpak&type=code ; API `search/code` | **not checked**: code search requires a signed-in session |
| Other `.eden` use (not our extension) | web search `Eden archive format`; https://mrob.com/pub/vidgames/eden-file-format.html | `.eden` is the save-game suffix of the iOS game Eden World Builder; `.edz`/`.edn` are other formats. None uses `.edenpak` |

Finding: **no hit found for `.edenpak` in the sources above.** Not checked: PRONOM, GitHub code search, and every source not listed. The stop condition of the task (a real clash with `.edenpak`) did not occur.

### A.2 Magic bytes `45 44 45 4E 50 41 4B 01` (and footer magic `45 44 45 4E 45 4E 44 01`)

Method: each database was downloaded (or its page fetched) and searched, case-insensitively, for the text `eden` (which covers both strings, `EDENPAK` and `EDENEND`, and the bytes' ASCII form).

| Source | Version read | Result |
| --- | --- | --- |
| `file` (libmagic) magic database, `magic/Magdir/*` of https://github.com/file/file | `master` at commit `584caf71c7f9b5a581d888636bdbf80c11bfd087` (committed 2026-10-30) | 2 lines contain `eden`, neither a signature: an author's name in a comment (`archive`, "van Eeden") and a German word in a comment (`images`, "Verschiedene"). No `EDEN` signature |
| shared-mime-info, `data/freedesktop.org.xml.in`, https://gitlab.freedesktop.org/xdg/shared-mime-info | `master`, newest commit `93706a8b` (2026-10-21) | 0 occurrences of `edenpak`; no glob `*.eden*` |
| Wikipedia, List of file signatures, https://en.wikipedia.org/wiki/List_of_file_signatures | raw wikitext fetched 2026-11-01 | 0 occurrences of `eden` or of the hex sequence `45 44 45 4E` |
| Gary Kessler's File Signatures Table, https://www.garykessler.net/library/file_sigs.html (redirects to https://filesig.search.org/) | the page dated 26 April 2025 (`file_sigs_GCK_latest.html`) | 0 occurrences of `eden` |

Finding: **no hit found for the chosen magic in the four databases above, on 2026-11-01.** Not checked: other signature collections (for example TrID, DROID/PRONOM signatures, per-vendor lists). The byte `01` and the leading seven ASCII letters give a signature that is long and textual, but that is a property of the chosen bytes, not a proof of absence. Similar-looking strings exist elsewhere (BSA starts with `00 01 00 00`, BA2 with `BTDX`), none equal.

### A.3 Names: "Eden Archive Format", "EdenPak", "Eden"

- A web search for `"Eden Archive Format" OR "Eden Archive" file format` found **no file format of that name**; it found the Eden World Builder `.eden` save format, the HP-calculator "Eden" documentation archive, EPLAN's `.edz` and the `.edn` data notation, all unrelated.
- **"Eden" is a shared word**: among results for a search on Eden emulator, there is an open-source Nintendo Switch emulator named Eden (site eden-emu.dev; mirrors on GitHub). It was not found to use `.edenpak` or this magic (the search results mention no such extension; the project's own sources were not read).
- **"EdenPak"/"EDENPAK" as a name in other fields:** a search snippet from a trademark listing site (https://trademark.justia.com/owners/edenpak-llc-4209707, which redirects to https://trademarks.justia.com/owners/edenpak-llc-4209707) reports a US trademark EDENPAK, owner EDENPAK LLC, filed 2019-09-04, serial 88603986, goods paper and plastic tableware. **The page itself could not be opened (HTTP 403); this is the search snippet only and is not verified.** It is in a different field from software file formats; whether it matters for using "EdenPak" as the *short name* is a question for toddhow (Annex E, open point O1). A GitHub account named `EdenPak` exists (A.1).
- The extension and the magic remain unambiguous as file identifiers because none of the "Eden" names above uses `.edenpak` or the magic; the *name* "Eden" is shared and the format's full name is therefore always written "Eden Archive Format" (and the product is OpenEden).
- Governance (design doc 15): a naming and trademark policy is to be written when the specification is published so that the name cannot be attached to incompatible formats; trademark advice is not available to this project, so none is given.

### A.4 MIME type

Checked https://www.iana.org/assignments/media-types/application.csv on 2026-11-01: no registered type whose name contains `eden`, and no `application/vnd.openeden*`. The proposed `application/vnd.openeden.edenpak` is a **proposal**; nothing is registered and no registration is requested. (The vendor tree `vnd.` is for types of a vendor or organisation; whether the project qualifies and the registration procedure are **verify**, RFC 6838, not read for this task.)

## Annex B. A worked valid pack

A pack of 3 entries and 17,088 (0x42C0) bytes, built by a scratch program written for this task (not part of the repository) with the python packages `blake3` 1.0.11 and `zstandard` 0.25.0 (libzstd 1.5.7); **all hashes below were computed by the library**, not by hand. The hexadecimal text of this annex was then parsed back into bytes and given to a second program (a reader written for this task) that implements most of the checks of section 14 (not implemented in it: the zstd window limit, bytes after a frame, the `ZSTD_COMPRESSBOUND` ceiling on `storedSize` of codec-1 chunks, the chunk-flag bits, the contents of provenance records and the extension-block walk, which the Annex B pack does not use; it also tests `storedSize == uncompressedSize` of codec-0 chunks at read, not at open as 14.2 item 10 requires); it accepted the pack, decoded all three entries and compared their hashes.

The three entries (in entry order):

| # | Address | Type | Content | Chunk | Codec |
| --- | --- | --- | --- | --- | --- |
| 0 | path `data/blob.bin` | generic (0) | 64 bytes, each `41` (`A`) | 1 chunk, 10 stored bytes | 1 (zstd) |
| 1 | path `readme.txt` | generic (0) | the 13 bytes `Hello, Eden!` + `0A` | 1 chunk, 13 stored bytes | 0 (store) |
| 2 | key `0x1000000000000001` (tag 0x10), no path | terrain tile (`0x0010`), flag `PINNED` | the 4 bytes `TILE` | 1 chunk, 4 stored bytes | 0 (store) |

Parameters: `chunkSize` 131,072 (`0x20000`), `compressorVersion` 10507 (zstd 1.5.7), `compressorLevel` 3. Entry 0's zstd frame is **hand-built, not produced by a compressor**: `28 B5 2F FD` (magic), `20` (frame header descriptor: single-segment, one-byte content size, no checksum, no dictionary), `40` (content size 64), `03 02 00` (block header: last block, type RLE, size 64), `41` (the repeated byte). libzstd decodes it to 64 bytes of `41`. A real compressor would write different bytes for this chunk, which is why validity does not depend on compressed bytes (section 17.3).

Layout (offsets in hexadecimal): header `0000 to 01FF`; zero padding to `1000`; entry 0 block at `1000` (10 bytes); entry 1 block at `2000` (13 bytes); entry 2 block at `3000` (4 bytes); data ends at `3004`, so `dataSize` = `2004`; metadata starts at `4000`: string table `4000` (23 bytes, padded to 24), entry table `4018` (3 x 112 = 336 bytes, to `4168`), path index `4168` (2 x 16, to `4188`), key index `4188` (16, to `4198`), chunk tables `4198` (3 x 56 = 168, to `4240`); no provenance, no extension blocks; footer `4240` to `42BF`.

### B.1 Hexadecimal dump

Rows of 16 bytes: offset, the 16 bytes, and their ASCII. A line with `*` means that all rows from the previous printed row up to (not including) the next printed row are identical to the previous printed row.

```text
00000000  45 44 45 4e 50 41 4b 01  00 00 01 00 00 02 00 00  |EDENPAK.........|
00000010  00 00 00 00 00 00 00 00  c0 42 00 00 00 00 00 00  |.........B......|
00000020  00 00 02 00 70 00 00 00  38 00 00 00 0b 29 00 00  |....p...8....)..|
00000030  03 00 00 00 00 00 00 00  03 00 00 00 00 00 00 00  |................|
00000040  02 00 00 00 00 00 00 00  01 00 00 00 00 00 00 00  |................|
00000050  00 10 00 00 00 00 00 00  04 20 00 00 00 00 00 00  |......... ......|
00000060  00 40 00 00 00 00 00 00  40 02 00 00 00 00 00 00  |.@......@.......|
00000070  00 40 00 00 00 00 00 00  18 00 00 00 00 00 00 00  |.@..............|
00000080  18 40 00 00 00 00 00 00  68 41 00 00 00 00 00 00  |.@......hA......|
00000090  88 41 00 00 00 00 00 00  98 41 00 00 00 00 00 00  |.A.......A......|
000000a0  a8 00 00 00 00 00 00 00  40 42 00 00 00 00 00 00  |........@B......|
000000b0  00 00 00 00 00 00 00 00  40 42 00 00 00 00 00 00  |........@B......|
000000c0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
000000f0  62 50 f4 96 c3 63 9c 22  e6 a6 a9 a9 e4 b2 39 6c  |bP...c."......9l|
00000100  ab 02 cf 56 51 3f 70 22  49 5d 5e d0 bc 73 03 8c  |...VQ?p"I]^..s..|
00000110  a5 fd 5a cb 7a 03 5f 38  79 23 5b 0b 8f dd d2 05  |..Z.z._8y#[.....|
00000120  e2 1b c1 44 09 a7 c9 6a  98 83 3c 0e 5b 6b 97 29  |...D...j..<.[k.)|
00000130  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
000001e0  e4 70 c0 2b 69 7d 9a 5c  11 54 28 e3 27 a6 5a 4c  |.p.+i}.\.T(.'.ZL|
000001f0  c8 7d 6e 4a 8e 40 e9 66  42 a7 6d 0b 11 08 a7 aa  |.}nJ.@.fB.m.....|
00000200  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00001000  28 b5 2f fd 20 40 03 02  00 41 00 00 00 00 00 00  |(./. @...A......|
00001010  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00002000  48 65 6c 6c 6f 2c 20 45  64 65 6e 21 0a 00 00 00  |Hello, Eden!....|
00002010  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00003000  54 49 4c 45 00 00 00 00  00 00 00 00 00 00 00 00  |TILE............|
00003010  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00004000  64 61 74 61 2f 62 6c 6f  62 2e 62 69 6e 72 65 61  |data/blob.binrea|
00004010  64 6d 65 2e 74 78 74 00  00 00 00 00 00 00 00 00  |dme.txt.........|
00004020  00 00 00 00 00 00 00 00  0d 00 00 00 00 00 00 00  |................|
00004030  01 00 00 00 01 00 00 00  40 00 00 00 00 00 00 00  |........@.......|
00004040  98 41 00 00 00 00 00 00  e0 28 42 4e 46 20 5e 56  |.A.......(BNF ^V|
00004050  b2 ed 1c e1 bf 70 87 05  40 72 e6 c4 e4 1f 84 3b  |.....p..@r.....;|
00004060  ed 1e 74 9d b6 35 79 2c  00 00 00 00 00 00 00 00  |..t..5y,........|
00004070  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00004090  0d 00 00 00 00 00 00 00  0a 00 00 00 00 00 00 00  |................|
000040a0  01 00 00 00 01 00 00 00  0d 00 00 00 00 00 00 00  |................|
000040b0  d0 41 00 00 00 00 00 00  e7 61 99 c0 df 40 59 c1  |.A.......a...@Y.|
000040c0  c8 32 a5 cf f8 ce b7 dc  e5 bb 05 52 79 b0 df 69  |.2.........Ry..i|
000040d0  04 b1 9a 44 a3 0d 2a cd  00 00 00 00 00 00 00 00  |...D..*.........|
000040e0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
000040f0  00 00 00 00 00 00 00 00  01 00 00 00 00 00 00 10  |................|
00004100  00 00 00 00 00 00 00 00  00 00 00 00 10 00 00 00  |................|
00004110  02 00 01 00 01 00 00 00  04 00 00 00 00 00 00 00  |................|
00004120  08 42 00 00 00 00 00 00  ce 30 1e 4f 3c 6f 79 65  |.B.......0.O<oye|
00004130  e5 52 74 99 1c 0e 43 25  7e dc d6 1b 51 3d 60 a2  |.Rt...C%~...Q=`.|
00004140  b3 01 bb d0 25 7d 41 ac  00 00 00 00 00 00 00 00  |....%}A.........|
00004150  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
00004160  00 00 00 00 00 00 00 00  28 6e 99 97 e3 a1 1b 68  |........(n.....h|
00004170  00 00 00 00 00 00 00 00  8a 95 6b ea db 69 99 b9  |..........k..i..|
00004180  01 00 00 00 00 00 00 00  01 00 00 00 00 00 00 10  |................|
00004190  02 00 00 00 00 00 00 00  00 10 00 00 00 00 00 00  |................|
000041a0  0a 00 00 00 40 00 00 00  01 00 01 00 00 00 00 00  |....@...........|
000041b0  e0 28 42 4e 46 20 5e 56  b2 ed 1c e1 bf 70 87 05  |.(BNF ^V.....p..|
000041c0  40 72 e6 c4 e4 1f 84 3b  ed 1e 74 9d b6 35 79 2c  |@r.....;..t..5y,|
000041d0  00 20 00 00 00 00 00 00  0d 00 00 00 0d 00 00 00  |. ..............|
000041e0  00 00 01 00 00 00 00 00  e7 61 99 c0 df 40 59 c1  |.........a...@Y.|
000041f0  c8 32 a5 cf f8 ce b7 dc  e5 bb 05 52 79 b0 df 69  |.2.........Ry..i|
00004200  04 b1 9a 44 a3 0d 2a cd  00 30 00 00 00 00 00 00  |...D..*..0......|
00004210  04 00 00 00 04 00 00 00  00 00 01 00 00 00 00 00  |................|
00004220  ce 30 1e 4f 3c 6f 79 65  e5 52 74 99 1c 0e 43 25  |.0.O<oye.Rt...C%|
00004230  7e dc d6 1b 51 3d 60 a2  b3 01 bb d0 25 7d 41 ac  |~...Q=`.....%}A.|
00004240  45 44 45 4e 45 4e 44 01  00 00 01 00 80 00 00 00  |EDENEND.........|
00004250  c0 42 00 00 00 00 00 00  00 40 00 00 00 00 00 00  |.B.......@......|
00004260  40 02 00 00 00 00 00 00  a5 fd 5a cb 7a 03 5f 38  |@.........Z.z._8|
00004270  79 23 5b 0b 8f dd d2 05  e2 1b c1 44 09 a7 c9 6a  |y#[........D...j|
00004280  98 83 3c 0e 5b 6b 97 29  e4 70 c0 2b 69 7d 9a 5c  |..<.[k.).p.+i}.\|
00004290  11 54 28 e3 27 a6 5a 4c  c8 7d 6e 4a 8e 40 e9 66  |.T(.'.ZL.}nJ.@.f|
000042a0  42 a7 6d 0b 11 08 a7 aa  00 00 00 00 00 00 00 00  |B.m.............|
000042b0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
```

### B.2 Annotation

Header (offset: field = value):

- `0000` magic = `45 44 45 4E 50 41 4B 01`; `0008` versionMajor = 0; `000A` versionMinor = 1; `000C` headerSize = `00000200` (512)
- `0010` packFlags = 0; `0018` fileSize = `42C0`
- `0020` chunkSize = `00020000`; `0024` entryRecordSize = `70` (112); `0028` chunkRecordSize = `38` (56); `002C` compressorVersion = `290B` (10507); `0030` compressorLevel = 3
- `0038` entryCount = 3; `0040` pathEntryCount = 2; `0048` keyEntryCount = 1
- `0050` dataOffset = `1000`; `0058` dataSize = `2004`; `0060` metadataOffset = `4000`; `0068` metadataSize = `0240`
- `0070` stringTableOffset = `4000`; `0078` stringTableSize = `18`; `0080` entryTableOffset = `4018`; `0088` pathIndexOffset = `4168`; `0090` keyIndexOffset = `4188`
- `0098` chunkTablesOffset = `4198`; `00A0` chunkTablesSize = `A8`; `00A8` provenanceOffset = `4240`, `00B0` provenanceSize = 0; `00B8` extensionOffset = `4240`, `00C0` extensionSize = 0
- `00C8` generation = 0; `00D0` sourceDigest = 32 zero bytes (rows `00D0` to `00EF`, shown by the `*` line); `00F0` manifestDigest = `6250f496c3639c22e6a6a9a9e4b2396cab02cf56513f7022495d5ed0bc73038c`; `0110` metadataDigest = `a5fd5acb7a035f3879235b0b8fddd205e21bc14409a7c96a98833c0e5b6b9729`; `0130` to `01DF` reserved (zero); `01E0` headerChecksum = `e470c02b697d9a5c115428e327a65a4cc87d6e4a8e40e96642a76d0b1108a7aa` (BLAKE3-256 of bytes `0000` to `01DF`)

Entry 0 (`4018`, `data/blob.bin`): key 0; pathOffset 0 (`4018`..); pathLength `0D` (13) at `4028`; entryType 0 and reserved 0 at `402C`; entryFlags `00000001` (`HAS_PATH`) at `4030`; chunkCount 1 at `4034`; uncompressedSize `40` at `4038`; chunkTableOffset `4198` at `4040`; contentHash `e028424e46205e56b2ed1ce1bf7087054072e6c4e41f843bed1e749db635792c` at `4048` (BLAKE3-256 of 64 bytes of `41`); the remaining fields zero.

Entry 1 (`4088`, `readme.txt`): pathOffset `0D` (at `4090`); pathLength `0A` (10) at `4098`; flags 1; chunkCount 1; uncompressedSize `0D` (13); chunkTableOffset `41D0`; contentHash `e76199c0df4059c1c832a5cff8ceb7dce5bb055279b0df6904b19a44a30d2acd`.

Entry 2 (`40F8`, key only): key `1000000000000001` (bytes `01 00 00 00 00 00 00 10` at `40F8`); pathOffset 0 and pathLength 0; entryType `0010` at `410C`; entryFlags `00010002` at `4110` (`HAS_KEY`, bit 1, and `PINNED`, bit 16); chunkCount 1 at `4114`; uncompressedSize 4 at `4118`; chunkTableOffset `4208` at `4120`; contentHash `ce301e4f3c6f7965e55274991c0e43257edcd61b513d60a2b301bbd0257d41ac` at `4128`.

(The entry's fields are at the offsets of section 7.4 from the start of its record.)

Path index (`4168`): record 0 = pathHash `681ba1e397996e28` (first 8 bytes of the BLAKE3 digest of `data/blob.bin`, which is `286e9997e3a11b68...`) with entryIndex 0; record 1 = pathHash `b99969dbea6b958a` (digest of `readme.txt` starts `8a956beadb6999b9...`) with entryIndex 1. Sorted by hash: `0x681b...` before `0xb999...`. Key index (`4188`): key `1000000000000001`, entryIndex 2.

Chunk tables (`4198`): chunk of entry 0: offset `1000`, storedSize `0A`, uncompressedSize `40`, codec 1, chunkFlags 1 (`HASH_PRESENT`), chunkHash = the entry's hash. Chunk of entry 1 (`41D0`): offset `2000`, storedSize `0D`, uncompressedSize `0D`, codec 0, flags 1. Chunk of entry 2 (`4208`): offset `3000`, 4, 4, codec 0, flags 1.

Footer (`4240`): footerMagic `EDENEND` + `01`; version 0.1; footerSize `80` (128); fileSize `42C0`; metadataOffset `4000`; metadataSize `0240`; metadataDigest and headerChecksum repeated from the header; 24 reserved zero bytes.

## Annex C. Invalid packs

Each invalid pack below is the valid pack of Annex B with **one** change. Where the change is in the metadata region or the header, the writer of the test also **recomputes** what the change would otherwise break by accident (the metadata digest, the header checksum, the footer copies, and the path index when a name changes), unless the case is about exactly that, so that the pack is rejected **for the stated reason**. A reader that rejects for another reason first still conforms (the class is a diagnostic; the rejection is required), but the corpus (Annex D) records the intended class. Offsets are those of Annex B. "At open" means the reader MUST reject when opening; "at read" means the pack opens and the read of the entry MUST fail.

All 20 cases were generated and checked in draft 1 with the scratch program of Annex B. That program did not implement every rule of section 14.2 (see the first paragraph of Annex B), so this says nothing about the other rules; task 0052 regenerates the cases with an independent writer.

| # | Change | Required result | Class | When |
| --- | --- | --- | --- | --- |
| I01 | byte `0000` changed from `45` to `58` | reject | `E_MAGIC` | open |
| I02 | file cut by 16 bytes (footer incomplete) | reject | `E_TRUNCATED` | open |
| I03 | byte `0190` (reserved header byte) changed from `00` to `01`, checksum not recomputed | reject | `E_CHECKSUM` | open |
| I04 | `fileSize` increased by 8 in both the header (at `0018`) and the footer (at `4250`); header checksum and footer copy recomputed, so the only failing check is that `fileSize` equals the real size | reject | `E_TRUNCATED` | open |
| I05 | 8 zero bytes appended after the footer | reject | `E_TRUNCATED` | open |
| I06 | chunk 0's `offset` (at `4198`) := `FFFFFFFFFFFFF000` | reject (offset far outside the data region) | `E_RANGE` | open |
| I07 | chunk 2's `offset` (at `4208`) := `2000` (inside entry 1's block) | reject (overlapping chunks) | `E_OVERLAP` | open |
| I08 | entry 0's name := `data/../b.bin` (same length; path index rebuilt) | reject (path traversal) | `E_NAME` | open |
| I09 | entry 1's name := the same string-table bytes as entry 0 (`pathOffset` 0, `pathLength` 13; path index rebuilt) | reject (duplicate path; the order check comes before the string-table position check) | `E_DUPLICATE` | open |
| I10 | entry 0's `uncompressedSize` (at `4038`) := `FFFFFFFFFFFFFFFF` | reject (size beyond the limit, which is checked before any sum) | `E_LIMIT` | open |
| I11 | chunk 0's `uncompressedSize` (at `41A4`) := `01000000` (16 MiB) | reject (chunk beyond 8 MiB) | `E_LIMIT` | open |
| I12 | byte `1005` (the zstd frame's content size) changed from `40` to `FF`; chunk table unchanged | the pack opens; reading entry 0 fails (frame says 255 bytes, record says 64) | `E_CODEC` | read |
| I13 | byte `2000` (first byte of `readme.txt`) flipped | the pack opens; reading entry 1 fails | `E_HASH` | read |
| I14 | header `packFlags` := `0000000000000020` (unknown critical bit 5); checksum recomputed | reject | `E_UNSUPPORTED` | open |
| I15 | chunk 1's `codec` (at `41E0`) := 7 | the pack opens; reading entry 1 fails; entries 0 and 2 still read | `E_UNSUPPORTED` | read |
| I16 | entry 1's name := `aaadme.txt` (sorts before entry 0; path index rebuilt) | reject (entries not in order) | `E_ORDER` | open |
| I17 | string-table padding byte `4017` changed from `00` to `01`, metadata digest not recomputed | reject (corpus run with the digest check on, 14.2 item 7) | `E_CHECKSUM` | open |
| I18 | first path-index hash (at `4168`) := 0 | reject (hash does not match the path) | `E_INDEX` | open |
| I19 | entry 0's `chunkTableOffset` := end of the chunk tables region plus 8 | reject (outside the region) | `E_RANGE` | open |
| I20 | entry 2's `entryFlags` := `00010022` (adds unknown critical bit 5); digests recomputed | reject | `E_UNSUPPORTED` | open |

Further cases the corpus MUST add (Annex D) but this annex does not give byte by byte (`E_VERSION` for version 0.2 or a higher generation byte; header and footer that disagree; `pathEntryCount` above `entryCount`; two entries whose chunk tables overlap; a non-zero `key` without `HAS_KEY`; a path that is also a directory prefix (validator only); extension-block walk errors; `headerSize` other than 512; a nonzero byte in the padding after the header): zero entries; a key of 0; keys out of order or equal; a path longer than 1024 bytes; a component longer than 255 bytes; invalid UTF-8 in a path; a `\` or upper-case letter in a path; a leading or trailing `/`; overlapping metadata sub-regions; a block not 4096-aligned; chunk sizes that do not add up to the entry size; a one-chunk entry whose chunk hash differs from the entry hash; a zstd frame with a dictionary; a frame with trailing bytes; a zstd frame that declares a window above 8 MiB; header and footer that disagree; an extension block with `MUST_UNDERSTAND` and an unknown id; a provenance record whose `recordSize` is wrong; an entry count that makes a table overflow 64 bits; and a hostile file made of random bytes behind a valid magic.

## Annex D. Conformance corpus requirements

For task 0052 (the corpus is written from this document alone, by someone who has not seen the engine's reader).

1. **Valid packs**, each with a description file giving the expected entries (path or key, type, size, content hash, content): the Annex B pack; an empty pack (zero entries, `dataOffset` 4096, `metadataOffset` 4096); a pack with one empty entry; an entry of exactly `chunkSize` bytes, of `chunkSize + 1` bytes, and of 3 chunks; entries of all registered types; a path of 1024 bytes; a path with non-ASCII UTF-8; a keyed-only pack; an entry with both a path and a key; an entry with provenance; a derived pack (flag, generation, source digest); a pack with an ignorable unknown flag, hint and reserved bytes set (a reader MUST accept it, and a canonical validator MUST report it); a pack with an unknown entry type (read as generic); a pack with a non-must-understand extension block (skipped); a texture entry in the layout of 10.2 once the annex is frozen.
2. **Invalid packs**: the cases of Annex C and its further cases list, each with the intended class and whether it is rejected at open or at read. A reader passes if it rejects every invalid pack, never crashes, and (when it reports classes) reports the intended one for at least the cases marked in the corpus as class-exact.
3. **Hostile files**: truncations of a valid pack at every length from 0 to the header size and at each structure boundary; random bytes behind a valid magic; bit flips in every field; a zstd bomb (a frame that expands far beyond its declared size, and a frame that declares 8 MiB but is tiny); a pack of 2^22 + 1 entries described by header only; sizes that overflow when multiplied. The reader MUST bound time and memory on all of them.
4. **Determinism checks** (writer side): the same input list in two orders gives identical packs; the same input packed with 1 and with 4 threads gives identical packs when the compressor library, version and parameters are the same; an all-codec-0 input gives a byte-exact golden pack that every conforming writer must reproduce; for codec-1 inputs the check is equal `manifestDigest` and equal decoded content.
5. **Golden data** changes only in a pull request that says why, reviewed by toddhow (root `CLAUDE.md`).
6. Corpus files contain no game assets; they are generated or hand-made.

## Annex E. Decisions, deviations and open points

Decisions that this draft takes where the task text left a choice (D) and deviations from earlier text (V). toddhow can change any of them; the safe choice is first.

| # | Item | Chosen | Alternative |
| --- | --- | --- | --- |
| D1 | File name of this specification | `docs/spec/eden-archive-format-v0.1.md` (as asked in the task brief) | the task spec's `docs/spec/eden-archive-format-0.1.md` |
| D2 | Registry | a section of this document (section 11) | a separate `registry.md` |
| V1 | **Whole-entry hash (task spec S2).** S2 proposed "whole-entry hash = hash over the chunk hashes". | The entry's content hash is the plain BLAKE3-256 of the content, and the per-chunk hashes are an additional field. Reason: `VFS::File::getContentHash()` (task 0050, `components/vfs/file.hpp`) is defined as the BLAKE3-256 of the content, and identical-content detection compares it across packs and with loose files, which can only be hashed plainly; a hash over chunk hashes would also change with the chunk size | S2 as written |
| D3 | Chunk hash over uncompressed bytes | uncompressed, so the hash does not depend on the compressor version | over the stored bytes (could be checked before decoding, but changes with the compressor) |
| D4 | Chunk size ceiling | 8 MiB, from RFC 8878's recommended decoder window | 64 MiB or more (needs larger zstd windows) |
| D5 | Strict alignment | a reader rejects an entry block that is not 4096-aligned | accept unaligned blocks |
| D6 | Determinism across zstd versions | byte-exact except codec-1 stored bytes; logical identity by `manifestDigest`; golden bytes only for store-only packs (17.3) | make the compressed bytes part of validity (would pin one zstd build for ever) |
| D7 | Header 512 bytes, footer 128 bytes | room for 176 reserved bytes; fixed footer | smaller header |
| D8 | Path hash | first 8 bytes of BLAKE3-256 of the path | xxHash or another 64-bit hash (an extra algorithm to specify; not chosen) |
| D9 | Mip tail | no reordering: KTX2 already stores the smallest level first; chunk 0 = the front of the file through the tail | reorder the payload (would break standard KTX2) |
| D10 | Limits | section 12, unmeasured | set after measuring real installs |
| D11 | Chunk default | 128 KiB | 64 or 256 KiB; to be set by the measurement of stage g |
| D12 | Path index uses sorted arrays with binary search (S4) | as in S4 | perfect hashing later |
| D13 | Footer (S6) | yes | header only |

Open points for toddhow:

- **O1.** The short name "EdenPak" is also the name of an unrelated US tableware trademark (EDENPAK LLC, search snippet only) and of a dormant GitHub account (Annex A.3). The format's full name and the extension are not affected. Keep "EdenPak" as the short name? **Resolved 2026-11-01:** toddhow decided that "Eden Archive" is the public name, so "EdenPak" is not used as a name.
- **O2.** The `application/vnd.openeden.edenpak` MIME type is a proposal; registration with IANA is not requested.
- **O3.** Limits (section 12) and the chunk size default need measurement on real Morrowind and New Vegas data (task 0051 stage f/g, which were not run, as this task used no game files).
- **O4.** Not checked in the collision search: PRONOM and GitHub code search (blocked), every other registry.
- **O5.** The KTX2 facts of 10.2 were read through a text extraction of the specification and must be re-read from the specification before the annex is frozen. The tile annex is not defined.
- **O6.** That CC BY 4.0 suits a standards text is still **verify**.
- **O7.** The independent read-through (stage h: a reviewer who has not seen the engine writes reader pseudocode from this document alone) and the fresh-reviewer reads have **not** been done.
- **O8.** The key tag/bit layout of tile keys, the `layoutParam` meaning for tiles, and the mip-tail size are left to later stage specs. The typed layouts are subsections 10.2 and 10.3 (marked draft and later) and not separate annex files as the task text suggested.
- **O9 (resolved in draft 2).** `metadataSize` is capped at 2^32 and the record strides at 512 and 256 (section 12). Earlier text: `metadataSize` has no cap of its own. A hostile pack can declare up to 2^22 entries of `entryRecordSize` 4096 (16 GiB of metadata) inside the 2^44 file limit, and the open-time metadata digest reads all of it. Either state a cap (for example on `metadataSize`, or lower `entryRecordSize`/`chunkRecordSize` maxima) or leave it to the fuzzing harness of task 0052 to decide.

## Changelog

| Date | Draft | Change |
| --- | --- | --- |
| 2026-11-01 | 1 | First draft (task 0051), checked by its author only |
| 2026-11-01 | 2 | Independent review (task 0052, step 0; a reviewer who read only this text and the task). Annex B decoded byte by byte with no mismatch. Changes: `headerSize` fixed at 512, `dataOffset` fixed at 4096, `metadataOffset` derived, metadata sub-regions exactly tiled (no gaps, empty offsets defined), zero-ness of the padding after the header and before the metadata; caps added: `metadataSize` 2^32, strides 512 and 256, chunks per pack 2^25, `storedSize` 2^24 for every codec, pack total 2^40; counts of path and key entries bounded before multiplication; string table, chunk tables and provenance records laid out back to back so they cannot alias; chunk stored ranges strictly ascending in (entry, chunk) order (one pass, no sort); `HASH_PRESENT` mandatory; checks of 14.2 renumbered, ordered and given exact classes (precedence rule); zero/consistency rules for unused fields; entry type 0x10 to 0x13 requires a key with the matching tag; zstd frame checks spelled out; extension-block walk made an open-time check; decoded-byte budget; path compare pseudocode and the file/directory name rule; manifest digest assigned to the validator; canonical writer uses the default layout for all types, version and level 0 when no codec 1; Annex C wording (I06, I09, I10, I17) and further cases |
| 2026-11-01 | 2 (clarified) | After the independent test writer worked from draft 2 (task 0052): 15.3 says 2^40 like section 12; 14.1 gives E_OVERLAP to chunks only; the file-and-directory name rule is checked by a validator in canonical mode; a reported error carries the offset of the first field the failed check names |
