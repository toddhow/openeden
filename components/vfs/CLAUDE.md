# Virtual file system (components/vfs)

Task 0050 (archive milestone A1). `VFS::Manager` is the one place where a path becomes a file. Loose folders, Bethesda
archives (BSA, BA2) and, later, Eden packs are all `VFS::Archive` objects registered in an order; the last one wins.

## Rules

- **Priority.** `registerArchives` adds the `fallback-archive=` list first (last entry wins), then the data folders
  (last wins). So every loose folder beats every archive. A pack named in `fallback-archive=` is like a BSA; one named on a `data=` line stands at that line (below).
- **Names.** `Path::Normalized` is the only key type: `\` becomes `/`, ASCII `A-Z` becomes lower case, doubled and
  leading separators go. Nothing else: no Unicode normalization, `.` and `..` are not resolved (a lookup with them
  misses), trailing dots and spaces and Windows device names are kept. Do not hand-write another version of this
  rule; use `VFS::Path`.
- **Names inside packs are strict** and checked by `Path::checkPackEntryName` (`entryname.hpp`). A tool that writes
  an entry to disk also calls `Path::checkExtractionSafe`.
- **Thread safety.** Written in the class comment of `manager.hpp`: set up on one thread, then immutable.
- **Overrides.** `buildIndex` lets each archive list into its own map and merges them in order; paths provided by more
  than one archive are remembered in `mProviders` (conflicts only, so it stays small). The merge finds the archive of
  the file being overridden with `Archive::contains`, so an archive's `contains` must agree with its `listResources`.
- **Range reads.** `File::size` and `File::readRange` are positional and synchronous. Loose files and entries of the
  uncompressed (Morrowind) BSA use `Platform::File::readAt` on a handle opened for the call. Compressed BSA and BA2
  entries use the default (open the entry, which decompresses it, then slice). A file over 4 GiB is not supported on
  Windows (`Platform::File::size`).
- **Packs in `data=` (task 0064).** A `data=` line may name a file ending in `.edenpak`; `registerArchives` registers it at the position of its line (`makeEdenArchive`), so it wins and loses like a folder there. `filterOutNonExistingPaths` keeps such a line; `Collections` and `MultiDirCollection` skip regular files. A file with that name that is not a pack stops start-up with a located error. Plugins in a pack are not readable (the readers take disk paths): `findContentFilesInArchive` lists them and `registerArchives` warns. Only the `fallback-archive=` route still puts a pack below all folders.
- **Archive factory list thread safety.** A mutex guards the list; `makeArchive` copies it under the lock and runs the factories outside it. Registration is still meant for start-up.
- **Archive kinds** are added with `addArchiveFactory` (or `addArchiveFactoryOnce(key, ...)`, which ignores a key it has) during start-up, before `registerArchives`. A factory
  recognises its file by magic bytes and returns `nullptr` otherwise. VFS itself knows only the Eden pack: the Bethesda kinds (TES3 BSA, TES4-family BSA, BA2) are `components/archivekinds/`, and every program that opens archives calls `VFS::registerBethesdaArchiveKinds()` before `registerArchives` (openmw, Eden Studio's `Data`, navmeshtool, physicsobjecttool; tests through `registerAll`). Without it a BSA in `fallback-archive=` stops start-up with "Unknown archive type" (task 0083 stage 3).

## Pitfalls

- `getRecursiveDirectoryIterator("/")` normalizes to an empty string and then increments `back()` of it, which is
  undefined behaviour (inherited, pinned by a disabled test). Not changed because no caller passes it.
- `VFSTestData` in `components/testing/util.hpp` copies its map over the index in `listResources` (`out = mFiles`). That
  works because the manager gives each archive an empty map of its own.
- Hand-written rewrites of resource paths that exist outside `VFS::Path` (survey, task 0050 stage b):
  `components/extension/vfs_service.cpp` (`normalizeVirtualPath`, its own physical mount table, now refuses `..`),
  `apps/openmw/mwrender/npc.cpp` (lower-cases a model name before a folder lookup), `components/vsgadapters/nif/nif.cpp`
  (lower-cases a model name only to test its folder and suffix), `apps/opencs/view/doc/view.cpp` and `meshsubview.cpp` (QString
  lower-case to find `/meshes/`, editor only). None of them is a lookup key that skips the Manager's normalization, so they are left alone.
