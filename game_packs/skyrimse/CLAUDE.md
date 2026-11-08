# game_packs/skyrimse

The Skyrim Special Edition game pack skeleton (task 0086, decision 0013). Read the root `CLAUDE.md`, `docs/design/game-packs.md` and `components/extension/CLAUDE.md` first.

It identifies Skyrim SE content and runs none of Skyrim's rules. Its only visible behaviour in a game is that `game=skyrimse` ends the session at start-up with "Skyrim SE rules are not implemented" and refuses saving.

## Layout

- `pack_core.*`: all the logic, with no plug-in glue, so the tests link it directly. Nothing in it throws; failures are `Content::Error` values or a message that starts with the file name.
- `skyrimse_game_pack.cpp`: the glue and the `openeden_extension_entry`. `OPENEDEN_SKYRIMSE_NO_ENTRY` (defined only by the test executable, which has an entry point already) leaves the entry out.

## Rules and pitfalls

- **Recognised, not decoded.** The record type table is for counting and skipping. Registering a type with `RecordService` means the core stores it; this pack decodes none, so it registers none. A record decoder belongs to a later task (needs record descriptors, task 0085 onwards).
- **The null rules are a failing `start()`** because `GameRules` does not exist yet. When it does, replace the throw with a real null implementation and keep the message and the test.
- **Hostile input.** Plugin counts and sizes are checked by `Content::Plugin`; `inflateRecord` caps the inflated size at 512 MiB and checks the declared and the produced size; the BSA header counts are checked against the file size before `Bsa::CompressedBSAFile` reserves from them. Keep that order when adding a reader.
- **Archives are version 105** (Skyrim SE). Other TES4-family versions are refused with the version in the message; the host's `VFS::registerBethesdaArchiveKinds()` opens them for the VFS, this pack does not register factories.
- **No game data in the repository.** Tests generate plugins, archives and NIFs. The install-in-place check (`apps/components_tests/skyrimse/testskyrimserealdata.cpp`) prints aggregates only and never a mod path; it runs only when `OPENEDEN_SKYRIMSE_DATA` names a Data folder.
- GCC `-Werror` traps from the root guide apply to the tests (a gtest macro as an unbraced `if` body failed here once).
