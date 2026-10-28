# core/math — vectors, quaternions, transforms

Headers only, standard library only, `double` precision.

## Rules
- No external math library (guide rule). Everything is a plain struct plus free functions.
- Normalize after composing rotations (`Transform` composition does this); pick the shorter path before comparing or blending (`angleBetween` does).
- Physics uses `float` (`opas::physics`); convert at the boundary, never mix the types inside a module.

## Pitfalls
- `acos` near ±1 is only accurate to about 1e-8 rad. For small angles use `atan2` of sine and cosine parts (see `angleBetween`).
- `normalized` of a zero vector returns zero, and of a zero quaternion returns identity: check for "no direction" yourself.
- `q * r` applies `r` first, then `q`.
