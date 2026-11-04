# `components/bodynif`: reading body part NIFs into plain arrays, and measuring seams (task 0069)

Part of `components` (it depends on `components/nif` only: no ESM, VFS, scene graph, GPU or `components/body`). Pure
code, no globals, never throws for a bad file. The engine adapter is `apps/openmw/bodyanalyser.*`
(`openmw --analyse-bodies`); the merger of stage b will use the same reader.

- `skinnedshape.*`: `readShapes(root, skinFilter, limits, placement)` mirrors the engine's reader where it matters
  (the skin filter, the 4 largest influences, no renormalising) and says where it differs (equal weights by bone index:
  the engine's `std::sort` leaves their order unspecified). **Placement:** a rigid part's vertices are in its attach
  bone's space, a skinned part's in the skin's space; with a `RestPose` (from the skeleton file) both are brought to
  the skeleton's space, and a part on a bone whose name contains "Left" is mirrored in x first, as `decorate` does.
  The skinned bind-pose formula (bone world x bone skin transform x skin transform) is from the format, not proved:
  stage b must confirm it against `Anim::SoftwareSkin`.
- `seamanalysis.*`: parts, boundary vertices (after welding a part's own coincident vertices), seam pairing inside a
  window, the signed gap along the line between the parts' centres, weight, UV and normal comparison. Definitions are in
  the header and pinned by `apps/components_tests/bodynif/`.
- `analysisreport.*`: the report data model, its canonical JSON and text, and the aggregate table. Deterministic.

Rules and pitfalls:

- **No Bethesda data here, ever.** Tests build synthetic meshes and Nif records in memory. A report names the user's
  own records and models: it stays under `I:\Projects\OpenEden-work\bodies\`.
- **Hostile input:** every count is capped (`ReadLimits`), every index checked, non-finite vertices and weights are
  refused with a located message ("shape 'X': weight for vertex 99 of bone 'Neck' is out of range").
- **A rest pose is first-name-wins and lower-cased**; a missing attach bone is one error per shape and leaves it
  unmoved: never assume a part was placed, check `Shape::mPlaced`.
- **Determinism:** no clock, no unordered iteration; percentiles are nearest rank.
- Findings of stage a: research note 0033 (rigid parts dominate; exact seams only at forearm-wrist and knee-ankle).
