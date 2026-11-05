# Task: Skyrim SE, the first pack skeleton
Design doc section: `docs/design/game-packs.md` sections 1, 3, 5 and stage f of section 10; `docs/research/0043-separating-formats-from-game-rules.md`; `docs/research/0036-extended-skeletons-measured.md`; `docs/design/advancedbodies-in-openeden.md`.
Goal: a `compat_skyrimse` compatibility pack and a `game_skyrimse` game pack skeleton that identify Skyrim SE installs, register its archives and plugin header reading, and give Skyrim meshes and skeletons to Eden Studio and the AdvancedBodies tools, running none of Skyrim's rules.
Inherited code touched, at commit `671c7676e9`: none expected in the core; the skeleton is built against the SDK of task 0084 and the common pack of tasks 0082 and 0083. Read for reference: `extensions/compat_morrowind/` (the pattern: `extension.json`, `morrowind_compat_extension.cpp`, the `GameContract` implementation), `components/esm4/reader.*`, `components/bsa/compressedbsafile.cpp` (TES4 BSA with LZ4), `apps/edenskelstat`, `components/bodynif/skeletonread.*`.
Inputs / outputs: `extensions/compat_skyrimse/` (an extension: `openeden.game` implementation with id `skyrimse`, units per meter, default humanoid skeleton path convention, the pack's NIF `SceneOptions`) and `game_packs/skyrimse/` (a `kind: "game"` pack that registers the Skyrim SE content probe, archive kinds, record type ids it recognises and a *null* `GameRules`); an Eden Studio mesh/skeleton viewer entry for Skyrim data using `Nif::Scene`.
Status: draft for toddhow's approval; depends on tasks 0082 to 0084. **Full loop** for the game pack (loader, contracts, untrusted input); the compatibility pack may use the lighter loop only if toddhow marks it.

## Why Skyrim SE is the second game (toddhow, 2026-11-04: decided)
- It differs most from Morrowind (Gamebryo 20.2.0.7 user version 12 stream 100; `BSTriShape`; form ids and master lists; localised strings; LZ4 BSAs; Havok and Papyrus), so it shows where the boundaries are wrong.
- toddhow owns the content and the measurements: CBBE 3BBB, XPMSSE, HIMBO and The New Gentleman, read with the engine's reader in research note 0036; a seamless body, which AdvancedBodies uses without a merge step (design doc H13 to H20 of `advancedbodies-in-openeden.md`).
- Oblivion and Fallout 3/New Vegas are a later, cheaper step: `components/esm4` already covers parts of them. They are not an alternative to this task.

## Scope of the skeleton
- **Identify:** probe a Skyrim SE install (the Data folder, `Skyrim.esm`, `Update.esm`, the DLC masters) from plugin headers (task 0083); a profile `game=skyrimse` selects the pack; the launcher/probe never starts a game.
- **Archives:** open the install's BSA files through the registered archive factory (TES4 BSA version with LZ4; whether Skyrim SE is version 105: **verify** from the install, the reader's own version check is cited in the stage log) and list the meshes and skeleton paths used by toddhow's set.
- **Meshes and skeletons:** read XPMSSE skeletons and the body meshes as `Nif::Scene` (task 0082), apply the Skyrim `SceneOptions`, and provide them to Eden Studio's mesh viewer (task 0043's viewer) and to `edenskelstat`/the skeleton-map tools with unchanged results (the aggregates of note 0036).
- **Not drawn in the game:** `BSTriShape` rendering in the game is a separate task (research note 0036 section 9 item 8).
- **`GameRules`:** a null implementation that refuses to start a game with the message "Skyrim SE rules are not implemented" and lets the editor and tools run.
- **Records:** type ids recognised and skipped with counts; no Skyrim record decoder beyond what `components/esm4` has. Rules, Havok `.hkx` animation, Papyrus (`.pex`), the Creation Engine's scripts, perks and quests are out of scope and are named as the next design questions (design doc section 9, question 6).

## Constraints
- No Bethesda assets in the repository: tests use generated plugins, generated NIFs (task 0082's builder) and an install-in-place mode that writes aggregates only (the pattern of note 0036). Paths of toddhow's mod folders are never written into files in the repository.
- The pack reads the user's own installed data; it ships none.
- Off by default and absent unless installed: with `game=morrowind` nothing about Skyrim loads (no extra files read, no startup cost beyond a manifest scan; measured).
- The Morrowind path is unchanged: the same gates as task 0085.
- Determinism, hostile input and allocation rules as in tasks 0082 to 0084.

## Acceptance criteria
- [ ] `compat_skyrimse` builds, installs and loads as an extension (task 0084's loader), and `game_skyrimse` loads as a game pack; with both disabled nothing changes
- [ ] On generated Skyrim-version plugins and archives: header, masters, flags, a compressed record, a localised-string record, an LZ4 archive entry are all read; truncated variants fail with located errors
- [ ] On toddhow's installed Skyrim SE set, in place: the aggregates of research note 0036 are reproduced through the pack (43 files read; skeleton and body counts equal; zero exceptions), and the plugin headers and archive lists of `Skyrim.esm` and the DLC masters print counts per record type with no error
- [ ] Eden Studio opens a Skyrim body and skeleton in the mesh viewer (frame viewed and the steps written in the stage log); this uses `Nif::Scene` only, no `BSTriShape` path in the game renderer
- [ ] `game=skyrimse` starts the game host to the main menu, shows the "rules not implemented" message and does not crash; `game=morrowind` plays as before (the 32 Lua tests, the three extension game checks)
- [ ] Layer rules (0081) and the extension SDK check (`CI/check_extension_sdk.sh`) pass; the dependency licences are unchanged (no new dependency)
- [ ] Linux `-Werror` and sanitizer runs
Out of scope: any Skyrim gameplay rule, Papyrus, Havok animation, Skyrim terrain/LOD (`.btr`/`.bto`), `BSTriShape` in the renderer, AdvancedBodies integration (a later task: the seamless-body path of the design doc), other games.

## Open questions (recommendation first)
1. Does the first game-pack skeleton need `game_skyrimse` at all, or only `compat_skyrimse`? Recommend both: the null `GameRules` proves the tier's loading, probing and failure behaviour with a game that has no rules yet.
2. Which toddhow-owned content is the acceptance set (his names and counts are in note 0036)? Recommend that set exactly, in place, aggregates only.
3. Havok `.hkx` and Papyrus: separate research notes before any task? Recommend yes, both after this skeleton.
4. Does the editor show Skyrim records? Recommend no in this task (needs record descriptors, task 0085 onwards).
