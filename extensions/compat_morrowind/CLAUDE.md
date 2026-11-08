# extensions/compat_morrowind

The Morrowind compatibility pack, the first real extension. It provides `openeden.game` and the `.kf` → glTF converter contract (`openeden::contracts::KfConverterContract`). Read the root `CLAUDE.md` and `components/extension/CLAUDE.md` first.

It is enabled by default in the shipped `openmw.cfg` (task 0018), so anything that would change the game's behaviour here needs its own setting, off by default.

## Dependencies

- **Allowed:**
  - the SDK (`sdk/include`)
  - `components/nif`, to read Morrowind's records, and `components/nifscene` (formats layer) for the scene options
  - small engine utilities: `components/misc` strings and `components/debug` logging
  - nlohmann JSON
  - VSG math types (`vsg::vec3`, `vsg::dquat`) in the pack's own code, as `components/nif` hands them over; never in a contract
- **Not allowed:**
  - no renderer: no `NifOsg`, `SceneUtil`, `vsgAdapters` or `Anim` (task 0011 removed the last of them)
  - no other extension's headers
- **Game readers belong here** (decision 0001's direction): anything that knows Morrowind's file rules moves into the pack, not into the core.

## The converter

- **`kf_animation.*`** reads a parsed `.kf` (`loadKfAnimation`) into text keys and per-bone `KfTrack`s.
  - It reproduces OpenMW's `NifOsg::Loader::loadKf` and `NifOsg::KeyframeController::getTransform` at OpenEden `cf3dbd3aa7`: constant keys switch past the midpoint, quadratic and TCB vectors use the cubic Hermite spline, quaternions slerp, and X/Y/Z rotations combine in the data's axis order.
  - Any change to that behaviour is a behaviour change. It needs a task spec and the converter's tests.
- **`kf_converter.*`** partitions text keys into clips and resamples tracks at `resampleRateHz`. It writes a GLB plus the `opas.clipsidecar` JSON with the clip events.
- **Coordinates:** the engine frame (+Z up, +Y forward) maps to glTF (+Y up, +Z forward) as (X, Y, Z) → (−X, Z, Y). Units divide by `unitsPerMeter`. Both conversions happen in `toGltfTranslation` and `toGltfRotation`, and nowhere else.

## Reading options (task 0082)

- **`scene_options.*`** supplies `morrowindReadOptions()` and `morrowindSceneOptions()`: Morrowind's file versions and the root-node rule
  (the first `NiNode` loses its own transform unless it is called `bip01`, any case). The nif reader and the scene builder name no
  game; the dependency points from the pack to the formats layer only, never back. The core's own default path (`Reader::parse(stream)`)
  keeps the same values as its default, because the core may not depend on an extension; a test pins that the two give the same scene.

## The game's data conventions (task 0085, move 1)

- `MorrowindGameContract` (`openeden.game` 1.1) answers the conventions the generic engine used to name itself: the length unit,
  the root, head and spine bone names, the actor skeleton model prefixes (`base_anim`, `skin`), the first-person body part suffix
  (`.1st`) and the first-person base animation file. `OMW::ExtensionHost::start()` copies them into `Misc::GameConventions`
  (`components/misc/gameconventions.hpp`); generic code reads that snapshot.
- Those values equal the snapshot's own defaults, which are what a session with no pack uses. `getUnitsPerMeter()` is exactly
  `Constants::UnitsPerMeter` (it was a different float once); `apps/components_tests/misc/testgameconventions.cpp` pins both.
  Change a value in both places, or Morrowind plays differently with and without the pack.
- A new convention is: a method at the END of the contract (additive, with a default that means "unspecified"), a field in
  `GameConventions` with the Morrowind default, the copy in `components/extension/game_conventions.hpp`, and a test with another
  game's value at the site. `CI/check_game_conventions.sh` fails if a moved site names one again.

## Tests

`apps/components_tests/extension/test_compat_morrowind.cpp` builds synthetic Nif records. Never use real game files in tests (guardrail: no game assets).
