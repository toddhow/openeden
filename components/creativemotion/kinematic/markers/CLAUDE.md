# kinematic/markers — interaction markers, body measurements (and, from A2, alignment)

Depends on `core/math`, `core/rig`, `kinematic/ik` (alignment), `core/format` (LoadError) and, privately, `opas::format_json` (nlohmann/json). No JSON type may appear in a public header.

## Rules
- File format: `docs/formats/markers.md`. Change it and the loader, writer and round-trip tests together; bump `version` for incompatible changes.
- Loaders never throw and report file, 1-based line and JSON pointer. The writer is canonical: load(write(x)) == x and write(load(canonical)) is byte-identical.
- nlohmann/json is built with `JSON_Diagnostic_Positions` (set in `cmake/OpasDependencies.cmake`); line numbers depend on it.

## Pitfalls
- nlohmann/json rejects a number that overflows a double (e.g. `1e999`) with an `out_of_range` exception that has no position. The loader replaces such literals with a sentinel string and parses again, so the error still has its line and field.
- A helper named `quoted` collides with `std::quoted` through argument-dependent lookup on `std::string_view`; the writer uses `jsonString`.
- Negative zero is written `0` (`number()` in core/format/src/JsonSupport.cpp): nlohmann/json reads `-0` as the integer 0, so writing `-0` broke byte-for-byte round trips.
- Alignment (`alignPair`) copies each actor's pose, bends limbs by turning IK world-space deltas back into local rotations, and recomputes world transforms. Chain bones may have other bones between them (A4c-1): the mid rotation is converted against its real parent after the root is applied, and in-between bones move rigidly with their parents. Chains are solved in mapping order, so a chain rooted above another chain's bones (e.g. at the chest) can move that chain's already-solved end.
- Penetration is a sum over capsule overlaps. The pole search keeps a candidate only if it is strictly better (by 1e-9 m), so on ties the smallest rotation and then the chain's own pole win.
- Shared JSON machinery (moved to `core/format` in M2; `src/JsonSupport.hpp` is now an alias so `detail::` names still work: the reader base class, the parse-and-read driver with line numbers and the non-finite fallback, file reading, canonical number and string writing) serves both marker files and body maps. A new format derives a reader from `detail::JsonReader` and calls `detail::parseAndRead`.
- Body maps (`docs/formats/bodymap.md`) check structure only. Bone names are checked when a map meets a skeleton (`measureBody`, `alignPair`).
- When a host project already provides `nlohmann_json`, CreativeMotion uses it instead of fetching its own. That host build must enable `JSON_Diagnostic_Positions`, or loader errors lose their line numbers.
- Pose clips (`docs/formats/poseclip.md`) and detection profiles (`docs/formats/detection.md`) use the same JSON support. Contact detection (`ContactDetection.cpp`) follows the numbered rules in `docs/tasks/0006`; keep the two in step.
- The validation pass (`Validation.cpp`, tool `tools/validate_markers`) does root alignment only on a flat skeleton built from a clip frame; IK chains are removed (A4c extends it). Rules: `docs/tasks/0007`.
- Capsule fit (`CapsuleFit.cpp`, A5a) returns a copy of the body map with fitted `radiusRatio` values, so the solver needs no API change. It uses the nearest-rank percentile with `std::nth_element`.
- Body map version 2 adds `fingers`. The writer writes version 1 when there are no fingers, so v1 files keep round-tripping; a v2 file with an empty `fingers` array loads but is not canonical.
- Preset manifests (`PresetFile.cpp`, `docs/formats/preset.md`, A6b): the JSON key `requires` is a C++20 keyword, so the member is `requirements`. `chooseMarkerSource` picks whole files: the user's own file, then the highest priority, then the smallest name.
- Automatic markers (`AutoMarkers.cpp`, A7, `docs/tasks/0011`): authoring reuses detection and validation; kept markers are active for the whole node because OStim has no animation time, so only contacts lasting at least half the clip are kept. Surface snapping moves an offset along its contact direction only, to the nearest surface layer's point closest to the line; taking the nearest point along the line alone let tube-edge points pull it sideways.
- A7 v2 (`contactCorrection`): per-frame depth correction. The animation keeps the limb end's position across the body; only the distance along the direction to the region's axis changes, so contacts follow the animation instead of being pinned. Snapping and correction share `nearestLayerAlong`.
