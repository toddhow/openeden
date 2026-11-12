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
- **Materials are value structs** (`ShaderMaterial`, `TextureSet`, `AlphaProperty`; a geometry's `mMaterial`/`mAlpha` index them,
  records shared by shapes are deduplicated). Only the fields the file stores for its stream version and shader type are copied:
  the nif records leave the others uninitialised, and copying them would make the scene differ from run to run. Fallout 4
  material files (`.bgsm`, `.bgem`) are paths only (`mName`); streams 155 and up (and user version 11 and below) are
  not read, with a warning. A shader property of another kind (water, sky, `BSShaderPPLightingProperty`) gives no material, with a warning.
- **NiTriShape skins** (Morrowind, Oblivion, Skyrim LE shapes) come from `NiSkinData`'s per-bone weight lists, turned into per-vertex
  influences in bone order. Later games keep the weights in the skin partition (Skyrim LE's `NiSkinPartition` weights are not read yet).
- **The scene is not sanitised.** Vertex data can hold Inf and NaN (a packed half with exponent 31 decodes to one) and bone
  weights are neither range-checked, summed nor normalised: the builder only checks indices (triangles, bones) and warns
  once per shape about non-finite influence weights (`Scene::mWarnings`). A consumer must filter non-finite values and
  renormalise weights itself before it skins or hashes anything.
- **The scene reader passes its own `Nif::ReadOptions`** (`SceneOptions::mRead`) to `Reader::parse`; it never uses the
  process-wide default (`NifSceneReview.readScene_uses_its_own_options_whatever_the_process_default_is`). Eden Studio's
  `MeshScene::loadScene` calls `readScene` with `SceneOptions{}`, so the same holds there.
- No renderer work lives here. `vsgadapters/nif` still draws `NiGeometry` only.
- Real files are read in place and only aggregates are written (`apps/components_tests/nifscene/testscenereal.cpp`,
  skipped unless `OPENEDEN_NIF_REPORT_DIRS` and `OPENEDEN_NIF_REPORT_OUT` are set). Never copy a game file into the repository.
- **`flatten.*` (task 0110, stage N3a-4a) is the renderer-ready view of a scene and stays pure** (`NifScene::Flat`): static shapes with the
  node transforms baked, materials reduced to a `MaterialKey`, shapes merged by key. It is in this layer because the layer rules let
  nothing below the host include both a format and the renderer; the VSG half is `apps/openmw/mwrender/skyrimmodel.*`. Skyrim's shader
  flag bits are named in `ShaderFlags` and pinned by number in `testflatten.cpp`; the refraction bits (15, 16) were measured on the inn's
  heat planes. `header.*` is `isSkyrimSeHeader` (the decision Eden Studio's `chooseNifReader` also makes).

- **Tree sway (task 0133, `Limits::mTreeSway`, off):** a lighting shape with the Tree_Anim flag (F2 bit 29), Vertex_Colors and colours is marked `MaterialKey::mSway` (never merged with a still shape) and keeps the file's vertex alpha, which is its per-vertex flexibility (0 rigid, 1 free tip; 63 percent of the vertices of the vanilla tree shapes are at 0 to 0.1, 13 percent at 1: `testskyrimsetreesreal.cpp`, aggregates only). The host turns the alpha into the sway weight (`apps/openmw/mwrender/skyrimtreesway.*`). Skinned shapes (the trunks and branches of the 55 models with a BSTreeNode) are not covered.
- **Material mapping (task 0130, `[Game] neutral world materials`, off):** `Limits::mSurfaceMaps` makes `flatten` keep the normal map (slot 1) with turned tangents (`Shape::mTangents`), the glow map (slot 2, only with the Glow_Map flag F2 bit 6), specular colour times strength, glossiness, emissive and the shader type in `MaterialKey`; model-space normals (F1 bit 12) and shapes with a bad tangent get no normal map. Flag meanings, tangent convention and slot use are checked by `testskyrimmaterialsreal.cpp` (gated, aggregates only). Skinned shapes are not covered.
