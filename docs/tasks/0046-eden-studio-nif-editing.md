# Task: Eden Studio NIF editing (NifSkope-style edits in the mesh viewer)
Design doc section: `docs/design/eden-studio-plan.md`, sections 2.2, 3.8 (undo, transactions and dirty state) and 9; continues `docs/tasks/0043-eden-studio-mesh-viewer.md` (the read-only viewer and its "What the later edit NIF spec needs") and `docs/tasks/0031-eden-studio-editor-module-api.md` (`Command`, `PropertySource`, `EditorContext::pushCommand`); `docs/decisions/0010-rendering-and-studio-answers-2026-11-01.md`, answer 16 ("write the spec").
Goal: toddhow can change a loaded `.nif` in the mesh viewer (rename a node, move it, change a flag, point a material at another texture, edit a text key), see the change at once, undo and redo it, and save the result as a new file in a data folder, with the guarantee that a file saved without any edit is byte-for-byte the file that was read, and that a file whose edits cannot be written exactly says so instead of writing something wrong.
Inherited code touched (to be confirmed at the commit that starts each stage): read first, edited only where the stage says. `components/nif/niffile.hpp` and `niffile.cpp` (`Nif::Reader::parse`: the header, the record loop, `mRoots`), `components/nif/nifstream.hpp` and `nifstream.cpp` (`NIFStream`: reading only), `components/nif/record.hpp` (`Record::mRecordIndex`, `mRecordName`), `components/nif/node.cpp`, `extra.cpp`, `property.cpp`, `controller.cpp` (the `read` functions that decide what is kept), `components/vsgutil/id.hpp` (`setID`, which stores `mRecordIndex` on scene nodes), `components/vsgadapters/nif/nif.cpp` (line 1430: `vsgUtil::setID(*node, nifNode.mRecordIndex)`), `apps/opencs/view/render/meshscene.*` and `apps/opencs/model/mesh/*` (the viewer, task 0043), `apps/opencs/model/doc/document.hpp` (`getUndoStack`, a `QUndoStack`).
Inputs / outputs: input is a mesh opened in the viewer (a VFS path or a file); output is a new `.nif` file in a chosen data folder, plus an edit report. New pure model `CSMMesh::NifDocument` (tested without a GPU), new library code `components/nif/write/` (the writer; see "Where the code goes"), no change to the NIF reader's results, to any save game, to the Lua API, to extension contracts or to any file format other than the NIF this task writes.

Status: draft, written 2026-11-01 at toddhow's request. Nothing is built. Full loop: it adds a writer to `components/nif`, which the whole engine reads through, and it changes the editor's undo behaviour.

## What it is, in plain language

NifSkope is the tool Morrowind modders use to open a model and change what is inside it: names, positions, which texture a surface uses, flags. The mesh viewer of task 0043 can already open a model and show its parts, but it looks at the model after the engine has turned it into a scene graph, and that graph has forgotten most of the file. To edit a file we have to work on the file's own records, and to save we have to write those records back in the NIF format.

The risky part is saving. A NIF has hundreds of kinds of record, and the engine's reader does not keep every byte (it skips some fields it does not need; see "What the reader keeps"). If we wrote a file by "reading everything into objects, then writing all objects out", every field the reader forgot would be lost or invented, and an innocent save could break a model in a way that only shows up in the game. So this task does **not** write the whole file from objects. It does this instead:

1. **Keep the original bytes.** When a file is opened for editing we remember its exact bytes, and for every record we remember where in those bytes it starts and ends.
2. **Edit as small changes ("patches") to named fields.** An edit says "record 12, field `name`, new value `Barrel_new`". It does not touch anything else.
3. **Save by copying.** The saved file is the original bytes, with only the records that have patches written again, and everything else (header, other records, footer) copied byte for byte. With no patches the saved file equals the original; that is a property of the method, and a test checks it.
4. **Write a changed record only if we know every byte of it.** For each record type we support editing, the writer is written from the format description and tested by writing the record that was read and comparing bytes. A record type whose layout we cannot fully reproduce is shown read-only in the viewer ("not editable: unsupported field"), never rewritten approximately.

This is slower to build than a general writer, but it is the only design where a mistake costs one unsupported edit, not a broken model.

### Decisions toddhow can change (my recommendation first)

| # | Decision | Recommended | Alternative |
| --- | --- | --- | --- |
| D1 | How to save | Patch the original bytes: re-write only edited records, copy the rest (above) | A full NIF object model and writer for every record type (NifSkope's way): years of work for the 100-odd record types, and any gap corrupts files |
| D2 | First edits | Node names, node transforms (translation, rotation, scale), node flags, texture file names in `NiSourceTexture`, material colours and alpha in `NiMaterialProperty`, text keys in `NiTextKeyExtraData`, "Extra data" strings (see "Which edits first") | Also vertex edits first |
| D3 | Vertex colours, normals, UVs | **Second batch**, behind its own stage and review: edited by tools that assign whole arrays (recalculate normals, set a colour), not by moving single vertices | Do not support geometry edits at all |
| D4 | Collision data | Read-only in the first build: Morrowind's `RootCollisionNode` is plain geometry (so it is editable once geometry edits exist); Havok `bhk*` records belong to later games and are not written | Edit collision now |
| D5 | Where saves go | Always a new file in a data folder the user picks (default: `<data-local>/meshes/<same relative path>`), never the original; overwriting an existing file needs an explicit confirmation naming the path | Write over the file in place |
| D6 | Undo | Every edit is an Editor Module API `Command` pushed through `EditorContext::pushCommand`, so it lives in the editor's one undo stack (plan 3.8) and merges consecutive edits of one field by `mergeId` | A private undo stack for the mesh view |
| D7 | Versions | Morrowind `4.0.0.2` (`VER_MW`) and `4.0.0.0` first; New Vegas `20.2.0.7` with Bethesda version 34 (`VER_BGS`, `BETHVER_FO3`) later and only after its extra rules (string table, record sizes) are tested on files; **verify** every version rule against the format description, not memory | Morrowind only, forever |
| D8 | Validation on save | A fixed list of checks that fail the save (see "Validation") and a list that only warn | Save whatever the user asks |
| D9 | Where the code lives | The byte-range index and the writers in `components/nif/` (they are NIF-format code), the edit model and commands in the editor (`CSMMesh`), nothing Morrowind-specific in either | Writer inside the editor |

## What the reader keeps and drops (read at the current commit)

This decides what a writer can and cannot do, so it comes from the code, not from memory.

- **The header is not kept as data.** `Reader::parse` (`niffile.cpp`) reads the version string (`nif.getVersionString()`) into a local, checks it starts with "NetImmerse File Format" or "Gamebryo File Format", and drops it. For 4.0.0.2 there is no record-size table (`hasRecordSizes` is true only from 20.2.0.5), no string table (`hasStringTable`, from 20.1.0.1), and the record type names are written before each record (`hasRecTypeListings` is from 5.0.0.1), which is why Morrowind files are simple to patch: record order, count and type names are the only global structure, and a record can change size without moving any index.
- **Record boundaries are not recorded.** The reader streams records one after another and sets `mRecordIndex` (`r->mRecordIndex = i`) but never stores where a record started or ended. Stage a adds that (a byte range per record, taken from the stream position before and after `r->read`), as an option of `Reader` that is off for the game, so the engine's loading is unchanged. **Verify** that `NIFStream` can report its position; it holds the stream and its size (`mStreamSize`) but the position accessor was not found in the header, so a small accessor may be needed (an inherited-code change, named in the stage).
- **Roots** are kept as pointers (`mRoots`), the root indices are re-read at the end of the file.
- **Strings are converted.** `NIFStream` reads strings through `ToUTF8::StatelessUtf8Encoder` when one is given (`nifstream.cpp`, line 69), so a legacy-encoded name in the file becomes UTF-8 in memory. A writer must convert back, and a name that does not survive the round trip is refused. The editor reads the original bytes of a name when it needs to show the file's own spelling.
- **Fields are skipped.** Examples in the current code: `nif->skip(4)` unused words in `NiCamera::read` (`node.cpp`, lines 408 and 410), unknown data after some nodes (`node.cpp`, 441, 605, 749, 755), vertex weights in an extra data record (`extra.cpp`, 36), "additional data" in geometry (`data.cpp`, 165), plus 49 skips in `physics.cpp` and 12 in `property.cpp`. Whatever is skipped is not in memory, so a record type that skips anything can only be rewritten by copying its skipped bytes from the original range. Stage a lists, per supported record type, whether the reader keeps every field; types with any skipped field are editable only through patches that leave those bytes in place.
- **Things the reader changes after reading** (`post()`, pointer resolution to `Record*`, `mUseSkinning`) do not matter to a byte-patching writer because it never serialises the in-memory object.

**Consequence:** the round-trip guarantee is by construction (copy), and the exact-write guarantee is per record type (tested). A record type that is not on the supported list is never rewritten.

## Where the code goes (the compatibility-pack boundary)

NIF is the engine's own format, loaded by `components/nif` for every game that uses it, so the writer and the byte-range index belong beside the reader (`components/nif/write/` or similar; **verify** the layout with toddhow's naming rules in `CI/check_file_names.sh`). They know record layouts and versions, not game rules. The editor core (`CSMMesh`) knows "a field of a record changed" and nothing about what a Morrowind texture folder or a creature skeleton means. Anything that is a Morrowind rule stays out:

- which `.kf` goes with a model (already the viewer's engine-level rule in `createAnimationOptions`), which bone names a creature must have, whether a texture path is valid for the game's `textures/` folder: warnings of that kind come from the compatibility pack through an editor-module validation hook (a later stage; not built here), not from the editor.
- the validation in this task only checks the NIF's own consistency (indices in range, version fields, counts), which is format knowledge.

## Which edits first

Ordered by usefulness and by how easy each is to write exactly. Each row is a supported record type and the fields a patch may change.

| Batch | Record (Morrowind name) | Editable fields | Why it is simple |
| --- | --- | --- | --- |
| 1 | `NiNode`, `NiTriShape` and other `NiAVObject` descendants | name, flags, translation, rotation matrix, scale | fixed-size fields at the start of the record; the name is a length-prefixed string, so the record grows or shrinks, which is fine without a size table |
| 1 | `NiSourceTexture` | external file name | a string; internal pixel data not editable |
| 1 | `NiTexturingProperty` | per-slot clamp and filter flags | small fixed fields (slot presence is not changed) |
| 1 | `NiMaterialProperty` | ambient, diffuse, specular, emissive colours, glossiness, alpha | fixed floats |
| 1 | `NiAlphaProperty`, `NiVertexColorProperty`, `NiZBufferProperty`, `NiSpecularProperty`, `NiWireframeProperty` | flags, test threshold | one or two integer fields |
| 1 | `NiStringExtraData`, `NiTextKeyExtraData` | the string; key times and texts (add, remove, rename) | text keys change the number of keys, and so the record's size, which the format allows |
| 2 | `NiTriShapeData` / `NiTriStripsData` | recalculate normals, set or clear vertex colours, scale UVs | whole-array operations; array length and flags change together; needs the geometry layout tested in detail |
| 3 | Adding or removing records (new node, delete a node) | structural edits | record indices shift, so every reference must be renumbered; needs the full reference list per record type; not in this task |
| Never here | Controllers and keyframe data (`NiKeyframeData`), particle systems, skinning (`NiSkinData`), Havok `bhk*` | | many skipped fields or complex layout; edited with NifSkope; shown read-only |

Edits to a node's name must not silently break links by name: animation text keys, `.kf` targets, bone names and `Bip01` lookups are by name (for example `node.cpp` line 188 compares the root name with "bip01"). The editor warns when renaming a node that is a bone of a skin (named in `NiSkinInstance`) or a node a `NiKeyframeController` targets, and the rename is allowed only after the user accepts.

## Design

**Pure model (stage a and b, tested without a GPU, window or game data).**

- `nifindex.hpp` (library): `struct RecordRange { std::uint32_t index; std::uint64_t begin, end; std::string type; }` and `std::vector<RecordRange> indexRecords(std::span<const unsigned char> bytes)`, produced by reading with the real `Nif::Reader` configured to report ranges (so there is one parser, not two). Also gives the header size (bytes before the first record), the root table's range, and the version triple.
- `nifpatch.hpp` (library): `struct FieldPatch { std::uint32_t record; std::string field; PatchValue value; }` with `PatchValue` a variant of string, double, int64, bool, a three-vector and a rotation matrix, and `struct PatchSet` (ordered, one patch per record and field, the last wins). `PatchSet::apply(bytes, index)` returns the new bytes or a located error (record index, type, field, reason).
- `nifwriter.hpp` (library): for each supported type a function `writeRecord(const Nif::Record&, const PatchSet&, ByteSink&)` with a registry by type name; `bool canWrite(std::string_view type, std::uint32_t version, std::uint32_t bethVersion)`; the file writer copies the header and the untouched record ranges and writes the patched records, then the root table, then the footer.
- `nifvalidate.hpp` (library): the checks in "Validation".
- `CSMMesh::NifDocument` (editor): owns the original bytes, the index, the `PatchSet`, the mapping between the viewer's tree nodes and record indices (`MeshNode` gains an optional `recordIndex`; the scene already stores it with `vsgUtil::setID`, and the viewer reads it when it walks the graph), a dirty flag, and `std::unique_ptr<Command> makeSetCommand(NodeId, field, value)`, the only way the panels change anything.
- `CSMMesh::NifEditCommand : openeden::editor::Command` (editor, uses the Editor Module API's `Command` as the plan's single undo stack, not a Qt command): `redo` sets the patch, `undo` restores the previous patch (or removes it), `mergeId` is the record and field so dragging a value merges into one undo step. After each redo and undo the viewer rebuilds the changed node (below).

**Showing an edit.** The viewer has the scene graph of the model as loaded by the engine, shared and cached by VSG (task 0043: "a shared cached graph"). The viewer must never mutate that shared graph for an edit. After an edit the document builds the edited bytes in memory and the viewer reloads the model from those bytes through the engine's reader (a new, uncached load of the edited file), so what is shown is exactly what the game would load from the saved file. This is also the strongest test that the writer is right. A single-field edit that the engine can show without a reload (a node transform) may be applied to the viewer's own copy as an optimisation later; the reload is the reference.

**Mapping tree nodes to records.** `vsgUtil::setID(*node, nifNode.mRecordIndex)` (`nif.cpp`, 1430) stores the record index on scene nodes. A NIF record index is unique only within one NIF (see `apps/openmw/mwrender/CLAUDE.md`), which is fine here because the document is one file. Nodes the loader merges or drops (for example a shape split by material) may not carry a record index; those tree entries are read-only and say why. **Verify** with three real models (a plain static, a creature with a skin, a model with a text key file) that every editable record type's index reaches the tree.

**Saving.** `File > Save Mesh As...` opens a folder chooser limited to the data folders the document knows (the `data=` and `data-local` paths from `openmw.cfg`, as the editor already reads, and a "Choose folder..." for any other), then writes `<folder>/meshes/<relative path>`. The original VFS file (a loose file or an entry in a BSA) is never opened for writing; a BSA cannot be, and a loose file in the user's Morrowind `Data Files` is not touched unless the user picks that very path and answers the overwrite prompt that names it. The save is atomic (write to a temporary file in the same folder, check, rename), so a crash cannot leave a half-written model. A `.nif.bak` is not made (the original is untouched by default).

**Validation on save.** Fail the save (and show the list): a record's range overlaps another; the root table points to a record that does not exist; any reference index (`NiNode` children, properties, data) is out of range or points to a record of the wrong type (checked by re-reading the saved bytes with the engine's reader, which throws `Nif::Exception` with the file name on bad references: `Reader::parse`); a string that cannot be encoded back to the file's code page; a texture or node name longer than the format's 32-bit length allows; the re-read model's node count differs from the edited document's. Warn only (the user can still save): a texture file that the VFS cannot find (`buildTextureReport`, task 0043); a renamed bone or animation target; a scale of zero; a name equal to another sibling's name.

**Round trip guarantee, and its test.** For every `.nif` in a test set: `save(NifDocument(bytes)) == bytes`, byte for byte, with no patches. With a patch that sets a field to the value it already has, the output is identical too (the writer is exact for supported types). A file that fails the first test must not be offered for editing at all; the viewer shows "this file cannot be edited exactly" with the first differing offset. The test set is generated NIFs built by the test code (a minimal Morrowind NIF writer used only by tests) plus, for toddhow's local check, a script that runs the round trip over his own `Meshes` folder and prints counts; Bethesda files never enter the repository.

## Undo through the Editor Module API

Plan 3.8: one undo stack for the whole editor. The mesh view is part of Eden Studio itself (task 0043), but its edit commands use the same `Command` interface and `EditorContext::pushCommand` that modules use, so a module (CreativeMotion's rig inspector, for example) can later add its own NIF edits to the same stack. Task 0031 records two rules that apply: a step of an asset that was closed or replaced does nothing (`mAssetToken`), and undo marks the asset dirty. Closing the mesh view with unsaved edits asks "Save, Discard or Cancel", as documents do. The undo stack's clean state is set when the file is saved.

## Constraints

- Off means unchanged: without opening a mesh for editing, nothing changes; the engine's loading path gains only an optional, default-off range report in `Nif::Reader` (stage a), and with it off the reader behaves and performs as before (tested by loading a generated NIF both ways and comparing records).
- The original game files are never written. Writes go through one function that refuses a path inside a BSA, refuses an empty or relative path, and refuses to overwrite without the confirmation flag from the dialog.
- No game assets in the repository; tests use generated NIFs.
- No heap allocation constraint for the editor; the writer is not a hot path. The reader's range report adds one `std::vector<RecordRange>` only when asked.
- Determinism: saving the same document twice gives identical bytes; no time stamps or user names in the output.
- Threading: main thread for the document and commands; writing a large file may run on a worker with the bytes copied, and the result is reported on the main thread.
- Strings: Windows file names are converted to UTF-16 at the file API boundary only.

## Stages

- **a. Index and round trip (no editing).** Optional range report in `Nif::Reader`, `indexRecords`, the copy-through writer, the round-trip test over generated NIFs for versions 4.0.0.2 and 4.0.0.0, a command-line check tool for toddhow's own folder (`tools/nif_roundtrip_check`). Review by a fresh reviewer.
- **b. Writers for batch 1 record types.** One writer per type, each tested by "read, write, compare bytes", then "patch, write, read back, compare the field".
- **c. The document and undo.** `NifDocument`, tree-to-record mapping, `NifEditCommand`, reload after edit.
- **d. The panels.** The Properties tab of the Mesh view becomes editable for supported fields (text boxes, number boxes, colour picker, a texture name box with a VFS browse button), unsupported ones greyed with the reason; Save Mesh As.
- **e. Validation and safety.** The checks, the overwrite dialog, atomic write.
- **f. Batch 2 (geometry colours and normals)** as its own reviewed change, only if toddhow wants it after using batch 1.
- **g. New Vegas 20.2.0.7.** The string table, the record-size table and the header author/script strings; **verify** every rule against the format description and test on generated files; real files checked locally by toddhow.

## Acceptance criteria

- [ ] `save(open(file)) == file` for every generated Morrowind NIF in the test set, with no edits, and for a set of generated NIFs of each supported record type, with and without an identity patch.
- [ ] Each batch 1 patch changes only its field: the re-read file differs from the original in that field of that record and nowhere else (tests compare the engine reader's result for every other record).
- [ ] The reader's range report is off by default and the game's load results are unchanged when off.
- [ ] Undo and redo of an edit restore the displayed model and the document's patch set exactly; merged edits make one step; closing the view with edits asks.
- [ ] A file saved from the viewer loads in the game's reader and in the viewer, and the displayed model equals the edited view (frame comparison of one model).
- [ ] The save never writes to the original path unless the user confirms; a BSA path is refused; a crash during a save leaves no half file (a test that fails the write midway).
- [ ] Validation: each failing check has a test that triggers it; a warning does not stop the save.
- [ ] A record type not on the supported list is shown read-only and is never rewritten (a test with a generated record of an unsupported type that has a patch addressed to it: error).
- [ ] The Linux gate (`-Werror`), the whole-tree clang-format check, `check_no_osg.sh`, the extension boundary checks, `components-tests`, `openmw-tests`, `eden-studio-tests` and the 32 Lua tests pass; a fresh reviewer read each stage with extra attention to byte-exactness and string conversion.
Out of scope: adding or deleting records, renumbering references, editing animation keys, particles, skinning and Havok data, a general object model of every NIF record, other games than Morrowind (until stage g), the compatibility pack's validation rules, merging or optimising meshes, converting to glTF or the Eden archive format, editing `.kf` files, macOS.

## Open points to verify (not decided here)

- Whether `NIFStream` can report its read position without a change to inherited code, and whether the engine accepts a nonzero list of skipped trailing bytes in a record on re-read (it should: `Reader::parse` does not check that a record consumed its declared size for 4.0.0.2, since there is none).
- The exact byte layouts of every batch 1 record for 4.0.0.2 and 4.0.0.0 (written from the NIF format description and checked against the engine's reader, which is the second source; no layout is written from memory).
- Which of toddhow's models the engine merges or splits so that tree nodes lose their record index.
- Whether Morrowind itself reads a NIF whose record order or string encoding differs from the original's: this task never reorders and never changes encoding, so it should not matter, but the in-game check (open the saved file in OpenEden and, if toddhow wishes, the original Morrowind engine) is part of acceptance of stage d.

## Stage log

**2026-11-09, stages a, b and c built (headless, Windows, local commits, not pushed; same hand wrote tests and code, no fresh reviewer; toddhow approved the task and said to take the recommended answers D1 to D9).**

- **Inherited code touched (minimal, off by default):** `components/nif/niffile.hpp/.cpp` gain `RecordSpan`, `FileLayout` and a third `Reader::parse(stream, options, FileLayout*)` overload; the two existing overloads forward with `nullptr`, so the game reads exactly as before. `NIFStream::tell()` (inline, no layout change). `ReadOptions` and `Reader`'s members are unchanged (no ODR change for other objects). Morrowind unchanged: `NifMorrowindUnchanged` pinned hash tests pass, and the 7,343 loose Morrowind meshes of the installed game open, save unedited byte-identical.
- **New:** `components/nifedit/` (layer `formats`): `nifindex` (record spans from the engine's own reader), `nifpatch` (field patches by splice: the original bytes are copied and only patched fields rewritten; supported: NiNode-like name/flags/translation/rotation/scale, property names and flags, NiAlphaProperty threshold, NiMaterialProperty colours/glossiness/alpha, NiSourceTexture name and external file, NiStringExtraData string, NiTextKeyExtraData keys), `nifdocument` (Document, SetFieldCommand with mergeable steps, UndoStack with clean state; `buildChecked` re-reads the result with the engine reader and checks records, roots and every patched value). Strings are the file's raw bytes (no NUL); version 4.0.0.0 and 4.0.0.2 only; anything else is an `EditError` naming record, type and field.
- **Facts found by the tests, not memory:** in 4.0.0.x `NiSourceTexture` has a ONE-byte external flag and a one-byte "is static" (the reader's `char` members), not 32-bit booleans. NiStringExtraData "bytes remaining" equals 4 + string length in all 2,672 real records, so it follows the string (only when it was exact); NiTextKeyExtraData's is 0 in real files and is left alone.
- **Tests (red first against a throwing stub: 42 tests, 5 passed by absence, 37 failed; the bytes-remaining test was shown red against the earlier code):** 44 in `apps/components_tests/nifedit/` pass in a private link; two mutations (uint16 encoder, erase-equal-patch) each made named tests fail. Real data (`OPENEDEN_NIF_MORROWIND_DIR`, aggregates only): 7,343 of 7,343 files open; 0 unedited saves differ; 624,429 identity-patched fields give identical bytes; a name-growing and flags-zeroing edit of every supported record re-reads cleanly in all files; 131,883 of 222,935 records editable.
- **Not built:** stage d (Mesh view panels, Save Mesh As, reload of the edited model), the `Command` adapter to the Editor Module API (needs apps/opencs, which another lane was editing), stage e's file side (atomic write, overwrite prompt, BSA refusal), batch 2 (geometry) and stage g (New Vegas). `NiTexturingProperty` (25,871 real records, the commonest unsupported type) is not yet editable.
- **Not run:** the Linux gate (GCC `-Werror`), clang-format 14, `CI/check_layer_rules.sh`, the whole `components-tests`, a fresh review.
