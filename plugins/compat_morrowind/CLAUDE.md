# plugins/compat_morrowind

The Morrowind compatibility pack, the first real plugin. It provides `openeden.game` and the `.kf` → glTF converter contract (`openeden::contracts::KfConverterContract`). Read the root `CLAUDE.md` and `components/plugin/CLAUDE.md` first.

## Dependencies

- **Allowed:**
  - the SDK (`sdk/include`)
  - `components/nif`, to read Morrowind's records
  - small engine utilities: `components/misc` strings and `components/debug` logging
  - nlohmann JSON
  - VSG math types (`vsg::vec3`, `vsg::dquat`) in the pack's own code, as `components/nif` hands them over; never in a contract
- **Not allowed:**
  - no renderer: no `NifOsg`, `SceneUtil`, `vsgAdapters` or `Anim` (task 0011 removed the last of them)
  - no other plugin's headers
- **Game readers belong here** (decision 0001's direction): anything that knows Morrowind's file rules moves into the pack, not into the core.

## The converter

- **`kf_animation.*`** reads a parsed `.kf` (`loadKfAnimation`) into text keys and per-bone `KfTrack`s.
  - It reproduces OpenMW's `NifOsg::Loader::loadKf` and `NifOsg::KeyframeController::getTransform` at OpenEden `cf3dbd3aa7`: constant keys switch past the midpoint, quadratic and TCB vectors use the cubic Hermite spline, quaternions slerp, and X/Y/Z rotations combine in the data's axis order.
  - Any change to that behaviour is a behaviour change. It needs a task spec and the converter's tests.
- **`kf_converter.*`** partitions text keys into clips and resamples tracks at `resampleRateHz`. It writes a GLB plus the `opas.clipsidecar` JSON with the clip events.
- **Coordinates:** the engine frame (+Z up, +Y forward) maps to glTF (+Y up, +Z forward) as (X, Y, Z) → (−X, Z, Y). Units divide by `unitsPerMeter`. Both conversions happen in `toGltfTranslation` and `toGltfRotation`, and nowhere else.

## Tests

`apps/components_tests/plugin/test_compat_morrowind.cpp` builds synthetic Nif records. Never use real game files in tests (guardrail: no game assets).
