# core/format — shared support for data-file loaders and writers

## Rules
- `LoadError` (public) is the one error type every loader returns: file, 1-based line, JSON pointer, message. Loaders never throw.
- `internal/opas/format/JsonSupport.hpp` holds nlohmann/json. Only reach it through `opas::format_json`, linked PRIVATE; never include it from a public header.
- A new format derives a reader from `detail::FileReader` (header, typed fields, arrays, `failAt` for rules checked after reading; `internal/opas/format/FileReader.hpp`) or the bare `detail::JsonReader`, and calls `detail::parseAndRead`; writers build text with `number`, `jsonString`, `vec3`, `quat` so output is canonical.

## Pitfalls
- Negative zero is written `0` (`number()`): nlohmann/json reads `-0` as the integer 0, so writing `-0` broke byte-for-byte round trips.
- Numbers too large for a double (`1e999`) make nlohmann/json throw without a position; `parseAndRead` replaces them with a sentinel and reparses so the error still has a line.
- `readUnitQuat` rejects rotations more than 1e-6 from unit length rather than normalizing them, so files say exactly what is used.
