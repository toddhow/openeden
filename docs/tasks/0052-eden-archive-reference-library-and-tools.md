# Task: Eden Archive reference library and tools (archive milestone A3)
Design doc section: `docs/design/eden-archive-format.md`, sections 7 (compression and hashing), 12 (virtual file system), 14 (what streaming needs), 15 ("Tools and bindings"), 16 (security), 18 (testing) and 19 (milestone A3; it was called E3 before the rename); the specification written by `docs/tasks/0051-eden-archive-format-spec-v0-1.md`; the VFS interfaces of `docs/tasks/0050-vfs-unified-archive-and-loose-files.md`; `docs/design/world-lod-and-streaming.md`, sections 5.3 and 11; decision record `docs/decisions/0010-rendering-and-studio-answers-2026-11-01.md`, items 24 to 26.
Goal: implement the Eden Archive Format as a small, permissively licensed reference library (reader, writer, validator) with a deterministic packer and unpacker command-line tool, a conformance corpus and a fuzzing harness, and plug the reader into the engine's VFS, so the container exists and is trustworthy before any world-LOD tile is written into it.
Inherited code touched, at commit `522b1a7951`: only the seam added by task 0050: `components/vfs/archive.hpp` (`Archive`), `components/vfs/file.hpp` (`File::size`, `File::readRange` once 0050 adds them), `components/vfs/registerarchives.cpp` (`registerArchives`) and the archive detection of `components/vfs/bsaarchive.hpp` (`makeBsaArchive`); build files `components/CMakeLists.txt` and `extern/CMakeLists.txt` (fetching and pinning zstd and BLAKE3, **verify** the existing zstd/lz4 situation: `components/bsa/compressedbsafile.cpp` already includes `lz4frame.h`, and whether zstd is already in the dependency set is not checked); `docs/dependencies.md`; the CI check scripts under `CI/` and `.github/workflows/` (task 0022) for the new tests and the fuzz job.
Inputs / outputs: new top-level folder for the reference library (proposed `edenarchive/`, see decision L1) with its own `LICENSE`, public headers, reader, writer, validator, the CLI tools `edenpack` (pack), `edenunpack` (unpack and list) and `edenvalidate`, a conformance corpus folder of generated packs with an expected-results file, a fuzz harness; an engine-side adapter (`components/vfs/edenarchive.*`, GPL) exposing a pack as a `VFS::Archive`; unit tests under `apps/components_tests/`; entries in `docs/dependencies.md`; no new setting.

Status: draft, written 2026-11-01. **Full loop**: the reader parses untrusted bytes, so the independent test-writer writes the conformance corpus and tests from the specification of task 0051 **alone**, without seeing the implementation; the implementer may not edit them; a fresh reviewer follows, with extra attention to bounds, integer overflow, allocation limits and threading. Depends on 0051 (the specification) and 0050 (the VFS seam). It precedes the world-LOD stages S1 onward. The launcher's built-in mod organizer (world-lod doc, section 13; its M1 is this tool) comes after this task. Nothing is built.

## Design, in plain language

Three programs come out of this task, all built from one small library:

- a **packer** that turns a folder into a pack, the same bytes every time (so a pack's hash means something and builds can be reproduced);
- an **unpacker and lister** that turns a pack back into a folder or lists it;
- a **validator** that reads every byte of a pack under the specification's rules and says whether it is valid and, if not, exactly where it is not (file, offset, field).

The **reader** is the dangerous part: it will be handed files from the internet. It must never trust a number in the file: every offset and size is checked against the file size and the limits of the specification, every multiplication or addition is checked for overflow, nothing is allocated from a size read from the file before it is checked against a limit, and hashes are verified before content is used. The task therefore ships with two ways to attack its own reader before anyone else does: a **conformance corpus** (valid packs, invalid packs, and hostile packs such as truncated files, absurd sizes, overlapping chunks, path traversal names, duplicate paths, an index pointing outside the file) with the required result for each, and a **fuzzing harness** that feeds the reader random and mutated packs under the address and undefined-behaviour sanitizers.

The packer has the two modes the accepted answers require (archive doc 17, decision 2): **store** mode, which keeps each file's bytes as given, only chunked and hashed (the default for repacking existing mods), and **optimise** mode, which may recompress and reorder and is meant for derived packs written by the baker. Original files are never changed or deleted. Texture conversion (DDS to KTX2) is **not** in this task: textures are stored as the author supplied them until archive milestone A4.

The engine adapter makes a pack look like any other `VFS::Archive`: `listResources` lists the path index, entries open as streams (for the legacy code paths) and answer `readRange` directly from chunk reads (for the streaming core). The adapter is registered through `fallback-archive=` as decided in task 0050. A pack is opened once, read-only, by positional reads; no memory mapping (archive doc 14.8).

### Where it lives and under what licence

Decided in the archive doc (section 17, decision 8): **Apache-2.0** for the reference library, and CC BY 4.0 for the specification text. The engine is GPL-3.0-or-later; the FSF's licence list states that Apache-2.0 is compatible with GPLv3 (one-way: Apache code can be used in a GPLv3 program), which is **verify**: read it on gnu.org when this task starts and record the URL in `docs/dependencies.md`. Because the library must be publishable on its own, it lives in its own folder with its own `LICENSE` and no dependency on engine code (`components/` and `apps/` must not be included by it); only the thin adapter in `components/vfs/` is GPL and depends on both. Third-party code it uses: **zstd** (BSD option of its dual BSD or GPLv2 licence, per its README, checked 2026-11-01) and **BLAKE3** (CC0 1.0, Apache-2.0 or Apache-2.0 with LLVM exceptions, per its README, checked 2026-11-01), both fetched at a pinned version and hash, recorded in `docs/dependencies.md` **before** they are added (root `CLAUDE.md`, Guardrails). meshoptimizer (MIT per its LICENSE.md, checked 2026-11-01) is not used by this task. KTX-Software is not used (its repository bundles a non-open-source file, so a dependency on it needs a file-by-file check first; archive doc 8).

### Decisions toddhow can change (safe choice first)

| # | Decision | Chosen | Alternative |
| --- | --- | --- | --- |
| L1 | Location | A top-level folder (`edenarchive/`) with its own licence and CMake project, added by `add_subdirectory` | `components/edenarchive/` (blurs the licence line) |
| L2 | Language and interface | C++20 core with a small C header for tools and other languages; no C++ types in the C header; the stable C ABI promise waits for the format's 1.0 (the engine's extension C ABI is a separate, later item, P6) | Pure C |
| L3 | Compression and hash | zstd (BSD option) and BLAKE3 (Apache-2.0 option), both pinned | Others (not allowed by the guardrail: no invented codec or hash) |
| L4 | Fuzzing | libFuzzer-style harness built with Clang and sanitizers on Linux in CI; a seed corpus from the conformance corpus; short smoke run per pull request, longer run on demand. Windows runs the corpus and a deterministic mutation test instead (**verify** MSVC's fuzzer support before promising more) | A third-party fuzzing service (not decided) |
| L5 | Corpus content | Generated by a small program from literal data in the repository; no game assets, no downloaded files; each file is a few KiB at most; the expected result is in one text file listing pack, required outcome and error location | Packs from real mods (forbidden) |
| L6 | Threading | Reader is thread-safe after open (positional reads, no shared mutable state); writer packs on several threads but its output is independent of thread count | Single-thread writer |
| L7 | Packing order | By the canonical sort of the specification, never by directory enumeration order | |
| L8 | Verification when reading | Verify the header, index and each chunk's hash before use; whole-pack validation only in the validator | Verify only on demand |

## Stages

Each stage commits working, with a fresh reviewer; the test-writer's tests for a stage exist before its implementation.

- **a. Dependencies and project.** Licence checks recorded, zstd and BLAKE3 pinned by tag and hash in `docs/dependencies.md` and `extern/CMakeLists.txt`, the folder and CMake project, the Apache-2.0 `LICENSE`, `CI/check_no_osg.sh` and the format check passing on the new files, an empty library building on Windows (MSVC) and Linux (GCC, Clang).
- **b. Reader core.** Header, footer and index parsing with every check of the specification; entry lookup by path and by key; the chunk read with decompression and hash verification; errors that carry file, offset and field; the allocation limits; no exceptions across the C boundary.
- **c. Writer and canonical form.** Entry collection, canonical sort, chunking, zstd, hashing, header and footer, zero padding; store and optimise modes; a thread pool whose output does not depend on thread count; a byte-for-byte determinism test (1 and 4 threads, inputs supplied in different orders).
- **d. Validator and tools.** `edenvalidate`, `edenpack`, `edenunpack`; round trip folder to pack to folder identical in contents; exit codes and messages; refusal to write outside the output folder when unpacking (path traversal), refusal to overwrite without a flag.
- **e. Conformance corpus.** Built from the examples of task 0051 and extended: valid packs (empty, one entry, many entries, stored and zstd chunks, keyed entries, derived flag, unknown ignorable hints and flags, a skippable extension block; a higher minor version is an invalid case, `E_VERSION`, because a 0.x reader supports exactly 0.1) and invalid and hostile packs (every rejection rule of the specification at least once). The expected-results file is checked by a test that runs the reader and the validator over the whole corpus.
- **f. Fuzz harness.** Mutation of the corpus plus random bytes, run under AddressSanitizer and UndefinedBehaviorSanitizer; any crash, hang, out-of-memory or sanitizer report is a failing test and its input is added to the corpus; a time and memory cap per input.
- **g. VFS adapter.** `components/vfs/edenarchive.*`: the pack as a `VFS::Archive`, opening through `fallback-archive=` by magic, `File::open`, `File::size` and `File::readRange` over chunks, conflict-report hash support (a pack entry has its content hash), tests with generated packs through `registerArchives`; the launcher is untouched.
- **h. Performance and notes.** Pack and read throughput on generated data, memory use of an opened pack with a large index (a generated index of the size of a big Morrowind and New Vegas install, **unknown** until measured), recorded with the machine named; module guide `edenarchive/CLAUDE.md`; changelog row text for the root guide handed to toddhow.

## Constraints

- No game assets in the repository, the corpus or the tests; sample content is generated or permissively licensed with the licence recorded.
- Determinism: no wall-clock time, no unseeded randomness, no unordered-container iteration where order changes output; the same inputs always produce the same pack bytes on the same machine; byte equality across machines and zstd versions is not promised, validity rests on content hashes (archive doc 14.6).
- Untrusted input: no allocation sized from file data before the limit check; no recursion on file data; no `new`/`delete` in new code; sizes held as 64-bit with checked arithmetic.
- Hot paths: reading a chunk allocates nothing per call beyond caller-provided or pooled buffers (the streaming core relies on this).
- Threading: say in the pull request which thread each public function may be called from; the reader is safe for concurrent reads of one open pack.
- The library compiles without the engine; the adapter is the only engine-aware code; the engine starts with no pack and behaves exactly as before.
- Builds with warnings as errors on Linux (`-Werror`), no new MSVC warnings (`/W4`); clang-format 14 on changed lines.
- Atomic file replacement and never editing a file in place: the packer writes to a temporary name and renames (archive doc 14.8).
- The packer never modifies or deletes input files.

## Acceptance criteria

- [ ] `edenpack` on a generated folder, then `edenunpack`, gives identical contents; `edenpack` run twice, with 1 and 4 threads and with inputs listed in different orders, gives identical bytes.
- [ ] `edenvalidate` accepts every valid pack of the corpus and rejects every invalid one with the specification's required outcome and a located message (file, offset, field).
- [ ] The conformance corpus covers every rejection rule of the specification at least once and contains hostile cases: truncated, oversized counts, overlapping and out-of-range chunks, non-normalized and traversal names, duplicate paths, a corrupted chunk hash, a wrong header checksum, an unknown major version, an unknown codec id (the entry is unreadable, the pack stays usable; the specification has no skippable codecs, only skippable extension blocks) and an unknown must-understand extension block.
- [ ] The fuzz harness runs without a crash, hang or sanitizer report for the agreed time budget (stated in the pull request) on the corpus seeds; every defect it found is in the corpus as a regression.
- [ ] The reader verifies each chunk's hash before returning bytes and a damaged chunk fails only its own entry's reads.
- [ ] The engine adapter lists, opens and range-reads pack entries through the VFS; a pack in `fallback-archive=` takes part in priority and in the conflict report like a BSA (task 0050); with no pack configured, behaviour is unchanged (the 32 Lua tests and the characterisation tests of 0050).
- [ ] Licences and pinned versions of zstd and BLAKE3 are in `docs/dependencies.md` with URLs; the Apache-2.0 to GPL-3.0 compatibility note is recorded with its source; nothing is added before its record exists.
- [ ] The library builds and its tests pass on Windows (MSVC) and Linux (GCC and Clang with `-Werror`); the sanitizer jobs pass.
- [ ] Unit tests, the 32 Lua tests, the Linux gate, clang-format 14 and the CI check scripts pass; the tests were written from the specification alone; a fresh reviewer read each stage.
Out of scope: the specification itself (0051), the VFS changes other than the adapter (0050), DDS to KTX2 conversion and texture layouts beyond storing bytes (archive A4), the derived-data cache and the baker (LOD stages S2 and S8), the streaming core, asynchronous IO and memory mapping, the launcher's pack filter, mod organizer pages and repack workflow (the organizer follows this task), encryption or DRM, nested archives, a public repository or release of the library, a third-party fuzzing service, the stable C ABI promise.
