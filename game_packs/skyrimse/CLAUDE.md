# game_packs/skyrimse

The Skyrim Special Edition game pack skeleton (task 0086, decision 0013). Read the root `CLAUDE.md`, `docs/design/game-packs.md` and `components/extension/CLAUDE.md` first.

It identifies Skyrim SE content and runs none of Skyrim's rules. Its only visible behaviour in a game is that `game=skyrimse` ends the session at start-up with "Skyrim SE rules are not implemented" and refuses saving.

## Layout

- `pack_core.*`: all the logic, with no plug-in glue, so the tests link it directly. Nothing in it throws; failures are `Content::Error` values or a message that starts with the file name.
- `records.*`: the record model (task 0086 record loading): `LoadOrder`, `FormIdResolver`, `StringTable`, and `parseRecords*` building plain structs (`ParsedPlugin`) for WRLD, CELL (+ group structure), REFR, ACHR, STAT, NPC_, ARMO, WEAP, MISC, LVLI, LVLN. Not wired into the engine. A subrecord of an unexpected size is counted (`mOddBy`), never guessed at; each layout detail is marked VERIFIED or RECALLED-VERIFY in `records.cpp`. `tools/skyrimse_records_check/check.sh` is the real-data check.
- `world_view.*`: `SkyrimWorldView`, the world store behind the SDK's `WorldView` contract (`sdk/include/openeden/game/world_view.hpp`): an index of cells, worlds and base forms with no placed reference held, cells read lazily from their file range and merged over the load order. Pitfalls: a cell record's children group (type 6) must follow it (the index keeps its offset and size); the index pass uses `ParseLimits::mSkipReferences` and `loadCell` parses with it off; a world's persistent placements belong to the grid square they stand in; ids are opaque to consumers; the pack registers its view with `Host::getWorldViewService()` but nothing fills it in a game yet. `game_skyrimse_world` (a static library of `pack_core`, `records` and `world_view`) is what Eden Studio links.
- `skyrimse_game_pack.cpp`: the glue and the `openeden_extension_entry`. `OPENEDEN_SKYRIMSE_NO_ENTRY` (defined only by the test executable, which has an entry point already) leaves the entry out.

## Rules and pitfalls

- **Recognised, not decoded.** The record type table is for counting and skipping. Registering a type with `RecordService` means the core stores it; this pack decodes none, so it registers none. A record decoder belongs to a later task (needs record descriptors, task 0085 onwards).
- **The null rules are a failing `start()`** because `GameRules` does not exist yet. When it does, replace the throw with a real null implementation and keep the message and the test.
- **Hostile input.** Plugin counts and sizes are checked by `Content::Plugin`; `inflateRecord` caps the inflated size at 512 MiB and checks the declared and the produced size; the BSA header counts are checked against the file size before `Bsa::CompressedBSAFile` reserves from them. Keep that order when adding a reader.
- **Archives are version 105** (Skyrim SE). Other TES4-family versions are refused with the version in the message; the host's `VFS::registerBethesdaArchiveKinds()` opens them for the VFS, this pack does not register factories.
- **No game data in the repository.** Tests generate plugins, archives and NIFs. The install-in-place check (`apps/components_tests/skyrimse/testskyrimserealdata.cpp`) prints aggregates only and never a mod path; it runs only when `OPENEDEN_SKYRIMSE_DATA` names a Data folder.
- GCC `-Werror` traps from the root guide apply to the tests (a gtest macro as an unbraced `if` body failed here once).
- **Owner, lock and teleport (XOWN/XRNK, XLOC, XTEL).** Sizes 4/4/20/32 are verified on the installed plugins; any other size is an odd subrecord and decodes to nothing. The XLOC padding bytes (1 to 3 and 9 to 11) hold garbage and must not be read; the flag byte is at offset 8. The components reach the neutral adapter through `ComponentSource` (a `SkyrimWorldView` answers for the cell the last `loadCell` returned), not through `PlacedObject`; `world_view.hpp` stays 1.0. `destinationCell` comes from the teleport-door index filled in the index pass.
