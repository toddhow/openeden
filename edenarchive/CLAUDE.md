# Eden Archive reference library (`edenarchive/`)

Task 0052 (archive milestone A3). Reader, writer and validator of the Eden Archive Format, specified in
`docs/spec/eden-archive-format-v0.1.md` (draft 2). The public name is "Eden Archive"; the extension `.edenpak` and the
magic `EDENPAK` + `01` are file identifiers only.

## Rules

- **Licence line.** This folder is Apache-2.0 (`LICENSE`) and must never include `components/` or `apps/`. The GPL
  engine depends on it (`components/vfs/edenarchive.*`, `apps/edenarchive/`), never the other way round. File names
  here are lower case letters and digits only (`CI/check_file_names.sh`).
- **The reader trusts nothing.** `src/archive.cpp` follows section 14.2 item by item, in that order, so the reported
  error class is the first failing check. Limits are compared before any arithmetic on the value; every sum is either
  bounded by a limit first or overflow-checked. The whole metadata region is read into memory once (cap
  `OpenOptions::mMaxMetadataSize`, default 1 GiB, the format allows 2^32) and every later read of the index is a
  bounds-safe view into it.
- **Chunk reads** verify the chunk hash before a byte is returned. They use thread-local scratch buffers (stored
  bytes, decoded bytes, one `ZSTD_DCtx`); `Source::read` must not call back into the same `Archive`.
- **The writer is canonical.** Output depends only on the set of entries and the options: entries are sorted, chunks
  are compressed on any number of threads and consumed in order, zstd parameters are set explicitly (level, no
  checksum, content size on, window log 23, row match finder off, no worker threads). Byte-for-byte equality is
  promised for the same zstd version only (spec 17.3); store-only packs are identical everywhere.
- **Pinned dependencies.** zstd 1.5.7 and BLAKE3 1.8.2 are fetched by `extern/CMakeLists.txt` and built here with no
  assembler. `static_assert(ZSTD_VERSION_NUMBER == 10507)` in `archive.cpp` and a unit test guard against a different
  `zstd.h` (OpenMW's Windows vcpkg set has one) being picked up.
- **No exceptions across the public API.** Every public function is `noexcept` and reports an `Error`
  (`class`, absolute `offset`, `field`, `message`). Tests compare class and offset, never message text.

## Layout

- `include/edenarchive/` public headers; `src/` implementation (`format.hpp` constants, `archiveimpl.hpp` open state).
- `tests/` gtest executable `edenarchive-tests`; `testsupport/` the INDEPENDENT test-side pack writer (written from the
  specification alone, using only BLAKE3 and zstd) and the corpus case list; `corpus/` generated packs.
- `fuzz/fuzzreader.cpp` libFuzzer entry (`-DEDENARCHIVE_BUILD_FUZZER=ON`, Clang only); `src/fuzz.cpp` holds the one
  input function that the libFuzzer target and the deterministic mutation unit test (`tests/fuzz.cpp`) share.
- `bench/benchread.cpp` the measurements of stage h.

## Corpus

`corpus/*.edenpak` and `corpus/expected.txt` are generated, never edited. Regenerate with
`edenarchive-corpusgen edenarchive/corpus` (a target of `BUILD_COMPONENTS_TESTS`); the test `corpusfiles.cpp`
regenerates into a temporary folder and compares with the committed files. They contain no game data. Changing a golden
file needs a pull request that says why (root `CLAUDE.md`).

## Pitfalls

- The test-writer found that the spec's wording on which field an error points at was not fixed; the implementation
  reports the first field the failed check names (for example `pathLength` for "path outside the string table").
- `Archive::open(path)` holds one file handle for the life of the archive; on Windows it opens with
  `FILE_SHARE_DELETE` so a tool can replace the pack by rename.
- Small files cost up to 4095 bytes of padding each (blocks start at multiples of 4096): 500,000 tiny files made a
  2.1 GB pack. This is in the format (spec 8.1), not a bug.
