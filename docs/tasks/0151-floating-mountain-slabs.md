# Task 0151: floating mountain slabs in Skyrim exteriors

Status: fix built and shown in game (Windows), existing tests need toddhow's approval. Branch `skytrim`, local commits only.

## Cause

The pack turned Skyrim's three stored angles into a rotation in the wrong ORDER. `game_skyrimse::eulerToQuat` built
`R = Rz * Ry * Rx` (angles negated; a vector turns about X first). Skyrim turns a vector about Z first:
`R = Rx * Ry * Rz`. For an object with only a yaw, or only one tilt, both are the same, which is why task 0086's checks
(pure X, pure Y and Z by frame) did not see it. For an object with a tilt AND a yaw the old order rotates the tilt
direction together with the yaw, so the object tips away from the slope it was placed on.

Object found: `skyrimse:b197124` (MountainTrim01_LightSN, Skyrim.esm 0x000A7D22), Tamriel (104403, 18631.4, -7989.5), stored
angles x 0.073, y 0.382, z 2.943 (yaw about 169 degrees). Land there falls to the west at about 24 degrees; the old order
tipped the slab up toward the west (the slab surface was 140 to 530 units above the ground over its 1800 x 2200 area),
the new order tips it down the slope.

## Evidence (everything measured; Python scratch scripts were outside the repository)

1. Land is not the cause. Heights decoded independently from Skyrim.esm's VHGT (`row*33+col`, x8) agree with the engine's
   `core.land.getHeightAt`, the physics heightmap ray and the rendering ray at 25 sampled points (`TRIM LANDH`, `TRIM col`:
   differences of a few units where no object lies over the point). The ESM data itself puts the trim origins 30 to 170
   units below the land at 247 placements of this base, so the placement is meant to be embedded.
2. The engine drew exactly what the pack said. The mesh was read independently (BSA v105 + `BSTriShape` vertices, 4 shapes,
   same boxes as the engine's `NIFSCENE` trace). Seven rendering-ray hits on the slab lie on the mesh transformed with the
   pack's old order to 0.21 units mean distance; the next convention is 2.0 units away. So no engine, flattener, terrain or
   LOD fault.
3. The order is wrong. 26,397 tilted Tamriel placements within 250 units of the ground (bases containing Trim, Rock,
   Boulder, Cliff): mean angle between the object's up axis and the land normal is 0.263 rad for `Rx*Ry*Rz` (negated) and
   0.513 rad for the old `Rz*Ry*Rx`; 2,504 MountainTrim placements: 0.242 against 0.617. The signs were already right
   (the same tips: +X angle leans up toward +Y, +Y angle toward -X). With the 247 trims' snow-plane vertices the height
   spread about the land is 176 units RMS against 477 for the old order (best of all 48 order and sign combinations).
4. In game (`TRIM_VARIANT=old|fix`, a Lua re-rotation of that one object with the two orders; the control `old` reproduces
   the baseline ray columns to 0.3 units): the old picture shows dark leaf-shaped slabs hanging in the air; the new one shows
   rocks lying on the slope. Frames: `I:\Projects\OpenEden-work\skytrim\run_old` and `run_fix` (`screenshot000.png`).
   Ray columns over the slab: render height above ground at (0,0): 140 old, 115 new; at (-400, 0): 362 old (slab at -7722.8, ground -8084.6), new: no slab hit (the ray meets another object).

## Change

- `game_packs/skyrimse/src/neutral_world.cpp`: `eulerToQuat` is `qx * qy * qz`; header comment updated.
- `apps/opencs/model/mesh/cellscene.cpp` (`placementMatrix`): the same order for Eden Studio's cell viewer (its own test
  `aPlacementMatrixFromAQuaternionIsTheEulerMatrix` compares it with the pack's quaternion).
- New test `apps/components_tests/skyrimse/testskyrimseneutralrotation.cpp` (3 tests, run red against the old pack object,
  green against the new).
- Scenario `tools/neutral_session_check/check.sh ... trim` (`luatest/scripts/trim`): land grid, column rays, vantage
  screenshots, `TRIM_VARIANT`.

## Reconciliation with task 0086 (which measurement was wrong)

Neither measurement was wrong; the stage log of 0086 READ ITS OWN TOOL'S LABEL WRONG. `tools/skyrimcell_check/rotation_ground.cpp`
builds each candidate with `r = mul(rot(axis, sign * angle), r)` for the axes in the listed order, so the FIRST axis of
"apply ZYX" is applied first: "apply ZYX" is `M = Rx * Ry * Rz`, and "apply XYZ" is `M = Rz * Ry * Rx`. I compiled the tool as it is
(a hand link against the pack library) and ran it on Skyrim.esm:

| tool variant (signs all -1) | M | mean angle of M*+Z to the land normal |
| --- | --- | --- |
| apply ZYX | Rx * Ry * Rz | 0.2022 rad, closer than untilted in 81.4 percent (78,245 placements) |
| apply XYZ | Rz * Ry * Rx | 0.4135 rad, 39.4 percent |
| untilted | | 0.3567 rad |

0086's "0.203 rad" winner is "apply ZYX" = Rx*Ry*Rz (Z first). Its comment line says "applied X first, then Y, then Z" and the pack, the editor and the
unit test followed that sentence, not the tool's output. My own script (independent: ESM walker, VHGT, same population filter: tilt 0.05..0.9,
within 150 units of the ground, slope over 5.7 degrees) gives 0.2023 for Rx*Ry*Rz and 0.4148 for Rz*Ry*Rx, and 0.629 for Rx*Ry*Rz with no sign flipped
(the tool's 0.627). My first survey used another population (bases with Trim/Rock/Boulder/Cliff in the editor id, all slopes), hence 0.263 and 0.513;
the order of the two conventions is the same in both. Both tools take column 2 of M (M * +Z), the same terrain normal
`(-(h(x+64)-h(x-64))/128, -(h(y+64)-h(y-64))/128, 1)`, the raw stored angles.

One concrete placement through the maths (raw angles, land normal from the heights):

| placement | normal | old Rz*Ry*Rx up (angle off) | Rx*Ry*Rz up (angle off) |
| --- | --- | --- | --- |
| b197124 (0xA7D22) 0.073 0.382 2.943 | -0.419 0.202 0.885 | 0.379 0.002 0.925 (48.6 deg) | -0.373 0.068 0.925 (8.4 deg) |
| 0xA7D23 0.033 0.358 0.343 | -0.426 -0.102 0.899 | -0.319 0.149 0.936 (15.8 deg) | -0.351 0.031 0.936 (9.0 deg) |
| 0xA4EC7 -0.323 -0.105 1.109 | 0.259 -0.298 0.919 | -0.240 -0.230 0.943 (29.2 deg) | 0.104 -0.316 0.943 (9.0 deg) |

The engine's `eulerToQuat` applied to (0,0,1) is the "old" column before this change and the "Rx*Ry*Rz" column after (the red/green unit tests compare it with an independent reference).

The Bannered Mare agent's `skymare/upvec.py` ("13.6 degrees, best of 16") fed the ENGINE's converted angles (x 0.0018, y -0.389, z 2.957, what `Misc::Convert::makeOsgQuat` takes) into candidates that expect the RAW Skyrim angles (0.073, 0.382, 2.943);
those are different numbers (the engine's Euler angles are the inverse conversion of the pack's quaternion), so its ranking is meaningless. With the raw angles one object cannot tell the orders apart (Rx*Ry*Rz 6.6 degrees, 5.8 for Rz*Ry*Rx with POSITIVE signs); the 78,000-placement populations can.

## EXISTING TESTS THAT PIN THE OLD ORDER (not edited; toddhow must approve their change)

- `apps/components_tests/skyrimse/testskyrimseneutralworld.cpp`: `SkyrimNeutralEuler.matchesTheReferenceRotationForCombinedAnglesAndIsAUnitQuaternion` and its `reference()` helper build `Rz * Ry * Rx`.
- `apps/opencs_tests/model/mesh/testcellscene.cpp`: `CellScenePlacement.rotationsComposeAsZYX` (and the comment of `positiveXAndYAnglesTipTheUpAxisToward...`, whose assertions still hold).
- Not run, so not known to fail: any others that use both a tilt and a yaw (`testcellsceneneutral.cpp` compares the editor matrix with the pack quaternion and passes only with both changed).

## Not run / not done

- The full build: this tree was not built. The pack's change was compiled and unit-tested by hand-linking `neutral_world.cpp` and the new test
  against another build's objects. A hand-relinked `skyrimse.dll` crashed at start-up even from UNCHANGED source (pipeline artifact, cause not found), so the in-game proof
  uses the Lua re-rotation, not the new DLL.
- Linux gate, clang-format 14, clang-tidy, the Lua tests, `components-tests`/`openmw-tests` as wholes, `opencs_tests`.
- Morrowind unchanged: by construction (the change is in the Skyrim pack and the editor's Skyrim cell viewer only), the `worldobjects_check` dump and fixed Balmora frame were NOT compared.
- LOD object clusters baked from Skyrim placements (`lodneutralobjects`) take the pack's quaternion: packs baked before this change hold the old orientation and must be re-baked.
- Z sign (negated) was left as settled; X/Y signs agree with the data (above).
