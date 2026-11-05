# advancedbodies_editor

AdvancedBodies' editor module (task 0069, stage f): the body package panel. A separate library that only Eden Studio
loads, from its fixed core list (`CSMModule::coreModules()`, decision 0007). The design and its verify items are the
task's "Stage f, design" section.

- **What it links:** the Editor Module API (`sdk/include/openeden/editor_module`), `eden_body` (`components/body`) and
  nlohmann/json. No Qt, no Studio type, no engine type. The one engine fact is the unit factor `kGameUnitsPerMetre`
  (`Constants::UnitsPerMeter`, `components/misc/constants.hpp`): a merged cache stores the game's units and does not
  say so; packages and the API are metres, and Studio multiplies by the same factor in `LineSink::convert`.
- **Read only.** Nothing here writes a file: `save` refuses, `isDirty` is false, `makeSetCommand` refuses every row.
- **Files:** `bodyview.*` is the model (pure, tested in `eden-studio-tests`, `apps/opencs_tests/model/module/
  advancedbodies/`); `body_document.cpp` the asset document, its rows, summary and two gizmos;
  `advancedbodies_editor_module.cpp` what is registered; `advancedbodies_editor_export.cpp` the entry point, only in the
  shared library (the tests link the other sources and already have another module's entry point).
- **Sources:** exactly `body.json` (its folder is the package), `*.edenpak` (a packed package when it holds
  `body.json`, a merged cache when it holds `manifest.txt`, told apart from the archive's index before anything is
  decoded, so an LOD or texture pack in an asset root is refused without reading it), `current` (the merged cache of
  its folder, through `Body::loadCurrentCache`). `body.json` and `current` are registered as file name endings (the
  registry's rule), so the Asset Browser also lists `somebody.json` or `recurrent`; the open refuses them. A package's
  files are dropped once its body is worked out. A merged cache is found by the Asset Browser only when its folder is
  an asset root (`OPENEDEN_CS_ASSETDIR` or a data folder): API 1.0 gives a module no user data folder.
- **The validator** runs with exactly `edenbodyvalidate`'s file reader plus `PayloadMeshSource`, which summarises the
  package's own `bodymesh` payloads (the tool has none and warns BODY-MESH-001 instead). A merged entry is validated as
  the package `mergedEntryPackage` derives from what the cache knows; it has no skeleton binding, so BODY-BIND-001 and
  BODY-BIND-014 are always listed (the panel says they are expected).
- **Drawing:** only `bodymesh` payloads, bind pose, never skinned. The regions are filled (API 1.1: one blended
  triangle per triangle of the drawn mesh in its region's colour, alpha `kFillAlpha`, at most `kMaxFillTriangles`, depth
  tested) and outlined (each distinct edge in the region's colour; palette in `bodyview.cpp`, the design's); weld groups
  are one point each. `Bodies > Cycle Region Display` (Ctrl+Alt+R) steps between fill and edges, fill alone and edges
  alone (`setRegionDisplay`; not an undo step, never dirty); the default shows both, the existing tests' edge lists
  still hold. Caps `kMaxRegionLines` and `kMaxWeldPoints` keep it inside a view's overlay (32,768 lines). Studio draws
  the active asset's gizmos in the cell views and in a Mesh view opened with no model
  (`Extensions > Show Module Asset in Mesh Viewer`).
- **Studio builds a form's rows once** (`PropertyForm::build`): every body of a merged cache has its own property
  source, kept for the document's life, so the panel builds a new form when the shown body changes (a new address).
  The inspector's summary is one fixed source whose values follow the shown body.
- **Choosing a body is not an undo step.** `Bodies > Next Body` and `Previous Body` (`selectBody`, then
  `requestRedraw`). A first version used an editable Choice row and an undo command: Studio pushes module commands on
  the content document's undo stack, whose clean flag is the document's modified state, so viewing another body marked
  the ESM document modified and the editor's close waited on a save question.
- **Hostile input:** packs and folders through `components/body`'s capped readers; region maps coloured with a
  "next free triangle" union-find (near-linear in triangles and ranges); the stage a report capped at 64 MiB, nesting
  32 and 1,000,000 values (counted before nlohmann parses it), every field typed, text fields at most 4,096 bytes. The
  lazy work of a merged entry (`BodyViewAccess::body`, reached from Studio's callbacks) catches what it throws and shows
  it as that body's message.
- **The stage a report is read from beside the cache** (`<user data>/analysis/bodies.json`); nothing ties it to the
  bake, and it is parsed again for each body shown.
- **Check in the real editor:** `OPENEDEN_CS_BODYCHECK=<prefix>;<source>[|<race> <sex> <view>];...` with
  `OPENEDEN_CS_UI_CHECK` (see `apps/opencs/CLAUDE.md`). Bakes are derived from the user's game data: keep them and any
  report under `I:\Projects\OpenEden-work`, never in the repository.
