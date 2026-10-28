# core/rig — skeleton, rig and profile model, with their file formats

Depends on `core/math`, `core/format` (LoadError) and, privately, `opas::format_json` (nlohmann/json). No JSON type may appear in a public header.

## Rules
- Bones are ordered parents-first; `validateSkeleton` enforces it, and `computeWorldRest` relies on it.
- Rest transforms must be finite and rotations unit length (NaN is rejected explicitly: comparisons with NaN are always false, so checks are written as `!(x <= tol)`).
- Character data comes from files, never from code. Tests build synthetic skeletons only — no game-derived bone data.
- Formats: `docs/formats/rig.md`, `docs/formats/profile.md`. Change a format, its loader, writer and round-trip tests together.
- One set of rules: `detail::findRigProblem` / `findProfileProblem` (src/Rig.cpp, src/Profile.cpp) check a complete model and return a JSON pointer. `validateRig`/`validateProfile` print it; the loaders look the pointer up in the parsed document to get the line. Add a rule there, never only in a reader, so programmatic and loaded data follow the same rules. Readers check only types, finiteness and unit rotations.
- Joints are keyed by the child bone and connect to the nearest ancestor *body* (`parentBody`), skipping bones without bodies. Exactly one root body; every other body has a joint.
- Rig types mirror `opas::physics` shapes and `JointDesc`, but core/rig includes no physics header; M3 converts.

## Pitfalls
- `computeWorldRest` and `parentBody` assume a valid skeleton or rig; validate first.
- A rest rotation more than 1e-6 from unit length is rejected; authored data must be normalized.
- Bone parents are names in files and indices in `Skeleton`; the reader resolves them only after every bone is read, so "listed after its child" and "unknown" get different messages.
- A helper named `quoted` collides with `std::quoted` through argument-dependent lookup; these sources use `inQuotes`.
- Rig format version 2 (task 0014): a joint is a cone (symmetric, the only kind in version 1) or a pyramid (its own min and max per swing axis, each containing 0). In memory both use the four min/max fields; a cone stores `-half`, `+half`, and its problems point at the half-cone keys a file would use. Version 1 still loads; `writeRig` always writes version 2, so the round-trip gate covers version 2 files.
- The reader needs the file's version before reading a joint (version 1 has no `swing` key, and the shape decides which keys are allowed), so `RigReader` keeps it from `readHeader`.
- `FileReader::readHeader` is shared with the profile reader, which passes its own newest version (2 since M5, for `feet`).
- Profile `death` (task 0018) is independent of `balance` and the other M5 sections; like them it needs version 2, and version 2 needs `feet`.
- Helper rules (task 0021, Stage C6a): optional `"helpers"` array in rig version 2 for driven bones (`twist`, `swing`, `chain`, `breathing`). Each driven bone is driven at most once, and no driver bone can be driven (no compound driving / cycles). Skeletons without helpers load identically and omit `"helpers"` in canonical `writeRig` output.
