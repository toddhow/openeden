# components/nifscene

`Nif::Scene` (namespace `NifScene`): the content of a NIF file as plain arrays (task 0082, `docs/tasks/0082-nif-any-version-and-scene.md`).
Formats layer (`CI/layer_rules.txt`): it may include `components/nif`, base utilities and nothing above. `scene.hpp`
holds no NIF, VSG or Jolt type; only `build.*` and `vertexdecode.*` include `components/nif` headers.

Rules and pitfalls:

- **The scene reader does not apply Morrowind's rules.** `SceneOptions::mRead` turns `ReadOptions::mMorrowindRootRule` off,
  so the first `NiNode` keeps its transform. The rule stays on for every other reader (the game, the tools) until the
  Morrowind adapter in `compat_morrowind` supplies it (task 0082, answer 3).
- **Skyrim SE skinned shapes have `Data Size` 0.** Their vertices, weights and bone indices are in `NiSkinPartition`;
  the triangles are the partitions' own lists (`mTrueTriangles`, global indices). `Geometry::mVerticesFromSkinPartition` says so.
- **The packed bone indices are skin bone indices** (research note 0036; confirmed again on 197 Skyrim SE files:
  no influence names a bone outside its partition's list). A partition's bone list is often NOT 0, 1, 2, ...: that is
  counted (`Skin::mNonIdentityPartitionBoneMaps`) and warned, never refused. Refuse only an index outside the skin.
- **Dynamic tri shapes (heads) may have no position attribute in the vertex descriptor.** Positions come from the
  dynamic array whatever the descriptor says.
- **Tangents are glTF-style** (xyz is the NIF's *bitangent*, w is the sign). The test `tangent_sign_*` pins it. Texture
  coordinates are not flipped.
- **The vertex layout is the schema's `BSVertexData`** (`packedVertexStride` has the byte count per attribute). A
  descriptor that disagrees with its own flags is a warning in `Scene::mWarnings`; 197 Skyrim SE and 783 Fallout 4 real
  files give none.
- No renderer work lives here. `vsgadapters/nif` still draws `NiGeometry` only.
- Real files are read in place and only aggregates are written (`apps/components_tests/nifscene/testscenereal.cpp`,
  skipped unless `OPENEDEN_NIF_REPORT_DIRS` and `OPENEDEN_NIF_REPORT_OUT` are set). Never copy a game file into the repository.
