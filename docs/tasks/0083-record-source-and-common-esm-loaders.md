# Task: Record-source abstraction and the common ESM/ESP loaders
Design doc section: `docs/design/game-packs.md` sections 3, 5.1 to 5.3 and stage c of section 10; `docs/research/0043-separating-formats-from-game-rules.md` sections 1.2 and 3 (tangles 1, 2, 9).
Goal: read any plugin (TES3, and the TES4-family files that `components/esm4` reads) through one game-neutral interface that yields the header and raw records, with typed decoding registered per game, and make the game's existing load path use it with no change in what it loads.
Inherited code touched, at commit `671c7676e9`: `components/esm/` (`esmbridge.*`, `records.hpp`, `util.*`, `defs.hpp`, `exteriorcelllocation.hpp`, `esmterrain.*`, `luascripts.*`: the base directory that includes `esm3` and `esm4`), `components/esm3/esmreader.*` and `readerscache.*`, `components/esm4/reader.*` (compressed records, `.STRINGS` localisation, `getStringsSuffix`), `components/esmloader/` (`load.cpp`, `esmdata.*`), `apps/openmw/mwworld/esmloader.cpp` (the dispatch on the file header and `ESM4::Reader`), `apps/openmw/mwworld/esmstore.cpp` (`ESMStore::load`, `loadESM4`), `components/contentselector/model` (reads plugin headers), `components/resource/foreachphysicsobject.cpp` (reads cell references; a layer exception), `components/vfs` and `components/bsa` (archive registration), `apps/esmtool`.
Inputs / outputs: new `components/contentsource/` in `formats` (`plugin.hpp`: `Content::Plugin`, `Content::Header` {game id guess, version, masters, flags}, `Content::RawRecord` {type, id, flags, byte span}; `plugin_reader.cpp`; `decoderregistry.hpp`); a contract header `sdk/include/openeden/contracts/records_contract.hpp` and `content_contract.hpp` (drafted here, finalised in task 0084); archive-kind registration for BSA/BA2 through `VFS::Manager::addArchiveFactory` (task 0050); `ESMStore` fed through the decoder registry for the types it already holds.
Status: draft for toddhow's approval. **Full loop** (untrusted file input, the VFS code that reads packs, `ESMStore`).

## Design
- **Plugin reading is two layers.** Layer 1 (`Content::Plugin`): header, master list, record iteration as `(type, id, flags, bytes)` without decoding; handles TES3 and TES4-family headers; Skyrim SE's header version, ESL/light flag, compressed record flag and localised-string flag are read here (the facts are in `ESM4::Reader`; the exact flag values are cited from that file in the stage log, not from memory). Layer 2: a decoder registry from record type id to decode function, registered by the game's pack. Today's typed `ESM3::` and `ESM4::` loaders become the first registrations, unchanged.
- **Identity** stays `ESM::RefId` (`components/esm/refid.hpp`): it already carries string ids and form ids.
- **`components/esm` is split in two** by moving the files that include `esm3`/`esm4` (`esmbridge`, `records.hpp`, `util`, `defs`) into a bridge directory in `formats` that depends on the base only through the registry. No file leaves `components`; this is a rename inside the formats layer with the includes updated (a mechanical change in a listed set of files).
- **`ESMStore` loads through the registry** for the types it knows. Its `StoreTuple` stays (it moves to the Morrowind pack in task 0085); the new thing is that `ESMStore::load` no longer branches on the header itself.
- **Archive kinds** (TES3 BSA, TES4 BSA with LZ4, BA2) are registered as VFS archive factories by file extension and magic from the content source; `registerArchives` stops naming them (removes the `vfs -> bsa` include exception of task 0081).
- **The launcher's content list** (`components/contentselector`) uses `probe(header)` instead of reading headers itself.

## Constraints
- Determinism: record order is file order; masters resolve in load order; no unordered containers where order affects results.
- Hostile input: sizes and counts bounded before allocation; a truncated or lying record gives a located error (file, offset, record type); none crashes or reads past a span (fuzz test on generated plugins).
- No exceptions across the contract boundary; errors are values on the contract.
- Load time: not slower than the commit before by more than noise on Morrowind plus Tribunal plus Bloodmoon (measured, three runs).
- No game data in tests: generated plugins only; a user-supplied-file mode in `esmtool` writes aggregates only (counts per type, no names).
- Skyrim SE record **semantics** are out of scope: this task reads headers and raw records of Skyrim SE plugins and the existing `esm4` decoders where they exist; it adds no new Skyrim record decoder.

## Acceptance criteria
- [ ] Unit tests: `Content::Plugin` on generated TES3 and TES4-family files (header, masters, flags, records, compressed record, localised-string flag, truncated, oversized count)
- [ ] `esmtool` output on Morrowind, Tribunal and Bloodmoon is **byte-identical** before and after (dump of every record type; the comparison is a script in the stage log, outputs kept in the work folder)
- [ ] `ESMStore` load: the number of records per store, `getRefCount`, and the first-cell load are identical (a test that prints them and the stage log compares); the 32 Lua tests pass
- [ ] Reading the header and the record index of toddhow's Skyrim SE plugins (`Skyrim.esm`, the Dawnguard/Dragonborn/Update masters and a handful of mods) succeeds and prints aggregates (record counts per type, flags); no error for records it has no decoder for (skipped with a count)
- [ ] Archive registration: BSA (TES3 and TES4 versions), BA2 general archives open through the factory list; the existing bsa tests pass; the Morrowind VFS has the same file list and override counts (8,675 overridden paths on the Morrowind data, task 0050) as before
- [ ] Layer rules (0081): the exceptions list loses `esm -> esm3`, `esm -> esm4`, `vfs -> bsa`; no new entry
- [ ] With the feature off (default game, no Skyrim content): nothing visible changes; load time within noise; no new setting is introduced
- [ ] Linux `-Werror` build, `components-tests`, `openmw-tests`, `eden-studio-tests`, sanitizers
Out of scope: moving `ESMStore`'s tuple, `Ptr`, any record's rules; the record descriptors for Lua and the editor (they come with task 0085); Papyrus; writing plugins in Skyrim form.

## Open questions (recommendation first)
1. Does layer 1 expose Skyrim's `GRUP` structure or flatten it? Recommend expose it (the cell hierarchy is in the groups) and test with generated groups.
2. Does the common pack decode the TES3 record layouts (file layout) or only raw bytes? Recommend: layouts stay as typed structs in `formats` (a layout is a format), rules are separate; Morrowind's typed structs therefore remain in `components/esm3` for now.
3. Who owns the save records in `components/esm3` (`savedgame`, `*state`)? Recommend: not touched here; task 0085 moves them with the managers that write them.
