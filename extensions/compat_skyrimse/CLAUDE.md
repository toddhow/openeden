# extensions/compat_skyrimse

The Skyrim Special Edition compatibility pack (task 0086), built on the pattern of `extensions/compat_morrowind`. It provides `openeden.game` (1.1) and the NIF reading options of the Skyrim SE version set. Read the root `CLAUDE.md` and `extensions/compat_morrowind/CLAUDE.md` first.

- **`SkyrimGameContract`** answers the same conventions as Morrowind's contract. Values come from recall unless noted: the humanoid skeleton paths and the four bone names were checked against the installed game's archives (`apps/components_tests/skyrimse/testskyrimserealdata.cpp`); `getUnitsPerMeter()` is Morrowind's value (`Constants::UnitsPerMeter`), not measured for Skyrim; Skyrim has no body part records, so the first-person suffix is empty.
- **`scene_options.*`**: `skyrimReadOptions()` / `skyrimSceneOptions()`. The scene reader already defaults to the Skyrim SE version set and no Morrowind root rule; the pack states them so that a tool asks the pack, not the reader's default.
- A new convention is added to the contract by task 0085's rule (a method at the end, additive); implement it here the same day.
- Dependencies: the SDK, `components/nif` and `components/nifscene` (formats layer), nothing else. No renderer.
- The pack is not enabled by default and is not in the shipped `openmw.cfg`.
